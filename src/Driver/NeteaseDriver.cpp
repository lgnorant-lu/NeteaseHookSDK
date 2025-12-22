/**
 * NeteaseDriver.cpp - 网易云音乐播放状态监控 SDK 实现
 * 
 * 网易云音乐 Hook SDK v0.1.2
 */

#define LOG_TAG "DRIVER"
#include "NeteaseDriver.h"
#include "CDPController.h"
#include "SimpleLog.h"
#include "LogRedirect.h"
#include <iostream>
#include <cstring>
#include <atomic>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shlwapi.h> // PathFileExists
#pragma comment(lib, "Shlwapi.lib")

// ============================================================
// 构造/析构
// ============================================================

NeteaseDriver::NeteaseDriver() 
    : m_CDP(nullptr)
    , m_ListenerRegistered(false)
    , m_Monitoring(false)
    , m_LastTime(0)
    , m_LastDuration(0)
    , m_LastUpdateTimestamp(0) 
{
}

NeteaseDriver::~NeteaseDriver() {
    Disconnect();
}

NeteaseDriver& NeteaseDriver::Instance() {
    static NeteaseDriver instance;
    return instance;
}

// ============================================================
// 日志系统
// ============================================================

void NeteaseDriver::Log(const std::string& level, const std::string& msg) const {
    std::lock_guard<std::mutex> lock(m_LogMutex);
    
    // 始终发送到回调（如果存在）
    if (m_LogCallback) {
        m_LogCallback(level, msg);
    } 
    
    // 同时也使用标准日志宏输出到控制台
    if (level == "ERROR") {
        LOG_ERROR(msg);
    } else if (level == "WARN") {
        LOG_WARN(msg);
    } else if (level == "DEBUG") {
        LOG_DEBUG(msg);
    } else {
        LOG_INFO(msg);
    }
}

void NeteaseDriver::SetLogCallback(LogCallback callback) {
    std::lock_guard<std::mutex> lock(m_LogMutex);
    m_LogCallback = callback;
}

// ============================================================
// v0.1.2: 全局日志控制内部存储 (跨模块单例存储在 DLL 中)
// ============================================================

static std::atomic<bool> g_LogEnabled{false};
static std::atomic<int> g_LogLevel{2}; // INFO
static bool g_AbsoluteSilence = false;

extern "C" {
    bool Netease_IsLogEnabled() { return g_LogEnabled.load(); }
    int Netease_GetLogLevel() { return g_LogLevel.load(); }
}

void NeteaseDriver::SetGlobalLogging(bool enabled) {
    g_LogEnabled.store(enabled);
}

void NeteaseDriver::SetGlobalLogLevel(int level) {
    g_LogLevel.store(level);
}

void NeteaseDriver::SetAbsoluteSilence(bool enable) {
    if (enable == g_AbsoluteSilence) return;
    
    if (enable) {
        if (RedirectStderrToFile("NUL")) {
            g_AbsoluteSilence = true;
            g_LogEnabled.store(false); // 同时也关闭内部逻辑日志
        }
    } else {
        RestoreStderr();
        g_AbsoluteSilence = false;
    }
}

// ============================================================
// 连接/断开
// ============================================================

bool NeteaseDriver::Connect(int port) {
    std::lock_guard<std::mutex> lock(m_Mutex); // 必须加锁

    if (m_CDP) {
        if (m_CDP->IsConnected()) return true;
        delete m_CDP;
        m_CDP = nullptr;
    }
    
    Log("INFO", "正在连接到网易云音乐 (端口 " + std::to_string(port) + ")...");
    
    m_CDP = new CDPController(port);
    
    if (!m_CDP->Connect()) {
        Log("ERROR", "连接失败! 请确保网易云已启动并带有参数: --remote-debugging-port=" + std::to_string(port));
        delete m_CDP;
        m_CDP = nullptr;
        return false;
    }
    
    // 注册事件监听
    if (!m_CDP->RegisterProgressListener()) {
        Log("WARN", "注册播放进度监听失败");
    }
    
    m_ListenerRegistered = true;
    
    // 启动后台监控线程 (如果未启动)
    if (!m_Monitoring) {
        m_Monitoring = true;
        m_MonitorThread = std::thread(&NeteaseDriver::MonitorLoop, this);
        Log("INFO", "连接成功! 后台监控已启动.");
    } else {
        Log("INFO", "连接成功! (监控线程已在运行)");
    }
    
    return true;
}

void NeteaseDriver::Disconnect() {
    // 1. 停止监控线程
    m_Monitoring = false;
    if (m_MonitorThread.joinable()) {
        m_MonitorThread.join();
    }

    // 2. 断开连接并清理
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_CDP) {
        m_CDP->Disconnect();
        delete m_CDP;
        m_CDP = nullptr;
    }
    m_ListenerRegistered = false;
}

bool NeteaseDriver::IsConnected() const {
    return m_CDP && m_CDP->IsConnected();
}

// ============================================================
// 获取播放状态
// ============================================================

IPC::NeteaseState NeteaseDriver::GetState() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    IPC::NeteaseState state = {};
    
    if (!m_CDP || !m_CDP->IsConnected()) {
        return state;
    }
    
    double time = 0;
    double duration = 0;
    std::string songId;
    
    if (m_CDP->PollProgress(time, duration, songId)) {
        state.currentProgress = time;
        state.totalDuration = duration;
        
        // 状态平滑逻辑 (State Smoothing)
        // 只有当进度实际上涨时，或者距离上次更新时间很短时，才认为在播放
        if (time != m_LastTime) {
            state.isPlaying = true;
            m_LastTime = time;
            m_LastUpdateTimestamp = GetTickCount64();
        } else {
            // 进度未变化，检查时间差
            // 减少宽容度：800ms -> 400ms
            // 60FPS 下，一帧约 16ms。400ms 约 25 帧，足够容忍网络波动，同时让暂停响应更灵敏
            if (GetTickCount64() - m_LastUpdateTimestamp < 400) {
                state.isPlaying = true;
            } else {
                state.isPlaying = false;
            }
        }
        
        // 缓存 Duration
        // JS 层已经做了单位归一化 (全部转为秒)，直接使用
        if (duration > 0.1) {
            m_LastDuration = duration;
            state.totalDuration = duration;
        } else {
            // 如果读取失败，使用缓存
            state.totalDuration = m_LastDuration;
        }
        
        // 复制 songId
        strncpy_s(state.songId, sizeof(state.songId), songId.c_str(), _TRUNCATE);
        m_LastSongId = songId;
    } else {
        // 使用缓存的值
        state.currentProgress = m_LastTime;
        state.totalDuration = m_LastDuration;
        state.isPlaying = false;
        strncpy_s(state.songId, sizeof(state.songId), m_LastSongId.c_str(), _TRUNCATE);
    }
    
    return state;
}

// ============================================================
// 自动部署 API Implementation
// ============================================================

std::string NeteaseDriver::GetInstallPath() {
    // 策略1: 优先从运行进程获取（最准确）
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (_stricmp(pe32.szExeFile, "cloudmusic.exe") == 0) {
                    // 使用 PROCESS_QUERY_LIMITED_INFORMATION 权限（更宽松）
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
                    if (hProcess) {
                        char path[MAX_PATH] = {0};
                        DWORD pathLen = MAX_PATH;
                        
                        // 使用 QueryFullProcessImageNameA 替代 GetModuleFileNameExA
                        // 优点：不需要 PROCESS_VM_READ，更可靠
                        if (QueryFullProcessImageNameA(hProcess, 0, path, &pathLen)) {
                            CloseHandle(hProcess);
                            CloseHandle(hSnapshot);
                            
                            // 去掉文件名，只留目录
                            std::string sPath = path;
                            size_t lastSlash = sPath.find_last_of("\\/");
                            if (lastSlash != std::string::npos) {
                                LOG_INFO("[Installer] 从进程定位: " << sPath.substr(0, lastSlash));
                                return sPath.substr(0, lastSlash);
                            }
                        }
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
    
    // 策略2: 从注册表获取（支持64位和32位视图，多种键名）
    const char* regPaths[] = {
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\网易云音乐",      // 64位应用
        "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\网易云音乐", // 32位应用在64位系统
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CloudMusic",
        "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CloudMusic",
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Netease Cloud Music",
        "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Netease Cloud Music"
    };
    
    for (const char* regPath : regPaths) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char path[MAX_PATH] = {0};
            DWORD size = sizeof(path);
            if (RegQueryValueExA(hKey, "InstallLocation", NULL, NULL, (LPBYTE)path, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                std::string sPath = path;
                // 清理引号和尾随斜杠
                if (!sPath.empty() && sPath.front() == '"') sPath.erase(0, 1);
                if (!sPath.empty() && sPath.back() == '"') sPath.pop_back();
                if (!sPath.empty() && sPath.back() == '\\') sPath.pop_back();
                
                if (!sPath.empty()) {
                    LOG_INFO("[Installer] 从注册表定位: " << sPath);
                    return sPath;
                }
            }
            RegCloseKey(hKey);
        }
    }
    
    LOG_WARN("[Installer] 警告: 无法自动定位网易云音乐");
    return "";
}

bool NeteaseDriver::IsHookInstalled() {
    std::string installPath = GetInstallPath();
    if (installPath.empty()) return false;
    
    std::string dllPath = installPath + "\\version.dll";
    return PathFileExistsA(dllPath.c_str());
}

// 辅助函数：检查 DLL 架构是否匹配
static bool IsDllArchMatch(const std::string& dllPath, bool targetIsX64) {
    HANDLE hFile = CreateFileA(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead;
    bool isMatch = false;

    if (ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL) && dosHeader.e_magic == IMAGE_DOS_SIGNATURE) {
        SetFilePointer(hFile, dosHeader.e_lfanew, NULL, FILE_BEGIN);
        DWORD peSignature;
        IMAGE_FILE_HEADER fileHeader;
        if (ReadFile(hFile, &peSignature, sizeof(peSignature), &bytesRead, NULL) && 
            ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL)) {
            
            bool dllIsX64 = (fileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
            bool dllIsX86 = (fileHeader.Machine == IMAGE_FILE_MACHINE_I386);
            
            if (targetIsX64 && dllIsX64) isMatch = true;
            if (!targetIsX64 && dllIsX86) isMatch = true;
        }
    }
    CloseHandle(hFile);
    return isMatch;
}

// ============================================================
// 智能安装 Hook (v0.0.3 - Robust Search & Verify)
// ============================================================
bool NeteaseDriver::InstallHook(const std::string& srcDllPath) {
    std::string installPath = GetInstallPath();
    if (installPath.empty()) {
        LOG_ERROR("无法定位网易云音乐安装路径");
        return false;
    }
    
    std::string targetExe = installPath + "\\cloudmusic.exe";
    
    // 1. 检测目标进程架构
    HANDLE hFile = CreateFileA(targetExe.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LOG_ERROR("无法打开 cloudmusic.exe");
        return false;
    }
    
    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead;
    
    // 读取并验证 DOS 头
    if (!ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL) || 
        dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(hFile);
        LOG_ERROR("无效的 PE 文件 (DOS Header)");
        return false;
    }
    
    // 跳转到 PE 头并读取
    SetFilePointer(hFile, dosHeader.e_lfanew, NULL, FILE_BEGIN);
    DWORD peSig;
    IMAGE_FILE_HEADER fileHeader;
    
    if (!ReadFile(hFile, &peSig, sizeof(peSig), &bytesRead, NULL) ||
        !ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL) ||
        peSig != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        LOG_ERROR("无效的 PE 文件 (NT Header)");
        return false;
    }
    
    CloseHandle(hFile);

    bool targetIsX64 = (fileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    const char* archName = targetIsX64 ? "x64" : "x86";
    
    // 2. 构建搜索路径列表
    std::vector<std::string> candidates;
    
    // A. 显式传入的路径
    if (!srcDllPath.empty()) candidates.push_back(srcDllPath);
    
    // B. 标准发布结构: bin/{arch}/version.dll
    candidates.push_back(std::string("bin/") + archName + "/version.dll");
    
    // C. 当前目录: ./version.dll
    candidates.push_back("version.dll");
    
    // D. 模块同级目录 (解决开发环境路径问题)
    char modulePath[MAX_PATH] = {0};
    HMODULE hModule = NULL;
    
    // 使用 InstallHook 函数本身的地址来获取模块句柄
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, 
                          (LPCSTR)InstallHook, &hModule) && hModule) {
        if (GetModuleFileNameA(hModule, modulePath, MAX_PATH) > 0) {
            std::string modDir = modulePath;
            size_t lastSlash = modDir.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                modDir = modDir.substr(0, lastSlash);
                candidates.push_back(modDir + "\\version.dll");
            }
        }
    }

    // 3. 遍历搜索并验证架构
    std::string validSourceDll;
    for (const auto& path : candidates) {
        if (PathFileExistsA(path.c_str())) {
            // 验证架构是否匹配
            if (IsDllArchMatch(path, targetIsX64)) {
                validSourceDll = path;
                break; // 找到完美匹配
            }
        }
    }
    
    if (validSourceDll.empty()) {
        LOG_ERROR("未找到架构为 " << archName << " 的 version.dll");
        LOG_INFO("已尝试路径: ");
        for (const auto& p : candidates) LOG_INFO("  - " << p);
        return false;
    }
    
    // 4. 执行安装
    std::string targetDllPath = installPath + "\\version.dll";
    
    if (PathFileExistsA(targetDllPath.c_str())) {
        MoveFileExA(targetDllPath.c_str(), (targetDllPath + ".bak").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    
    if (!CopyFileA(validSourceDll.c_str(), targetDllPath.c_str(), FALSE)) {
        LOG_ERROR("安装失败 代码: " << GetLastError());
        return false;
    }
    
    LOG_INFO("[OK] Hook 已安装 (" << archName << "): " << targetDllPath);
    LOG_INFO("     源文件: " << validSourceDll);
    return true;
}




bool NeteaseDriver::RestartApplication(const std::string& providedPath) {
    // 获取安装路径（优先使用传入的路径，避免杀掉进程后无法检测）
    std::string installPath = providedPath.empty() ? GetInstallPath() : providedPath;
    if (installPath.empty()) {
        LOG_ERROR("[Installer] 无法获取安装路径，重启失败");
        return false;
    }
    
    // 查找并终止所有 cloudmusic.exe 进程
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (_stricmp(pe32.szExeFile, "cloudmusic.exe") == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                    if (hProcess) {
                        TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                        LOG_INFO("[Installer] 已终止进程 PID=" << pe32.th32ProcessID);
                    }
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }
    
    // 等待进程完全退出
    Sleep(1000);
    
    // 启动新进程
    std::string exePath = installPath + "\\cloudmusic.exe";
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    
    // 此时不需要加参数，Hook 会自动处理
    if (CreateProcessA(exePath.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, installPath.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOG_INFO("[Installer] 已重启网易云音乐");
        return true;
    }
    
    LOG_ERROR("[Installer] 重启失败，错误码: " << GetLastError());
    return false;
}

// ============================================================
// Callbacks & Monitor
// ============================================================

void NeteaseDriver::SetTrackChangedCallback(TrackChangedCallback callback) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Callback = callback;
}

void NeteaseDriver::MonitorLoop() {
    std::string currentSongId = "";
    
    // 初始化 currentSongId
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        currentSongId = m_LastSongId;
    }

    while (m_Monitoring) {
        // 每秒检查一次
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        if (!m_Monitoring) break;

        // Auto-Reconnect Logic
        bool isConnected = false;
        {
             std::lock_guard<std::mutex> lock(m_Mutex);
             isConnected = (m_CDP && m_CDP->IsConnected());
        }

        if (!isConnected) {
            // 尝试重连 (不需要调用 Connect(port) 因为会有锁冲突)
            // 实际上 Connect() 是公有 API，会尝试获取锁。我们在这里是在线程内部。
            // 最佳实践：在此处调用 Connect 可能会导致锁顺序问题吗？
            // MonitorLoop 不持有锁，所以调用 Connect 是安全的。
            Log("WARN", "检测到断开连接，尝试自动重连...");
            if (Connect(9222)) { // 默认端口
                Log("INFO", "自动重连成功!");
            } else {
                // v0.1.2: 失败后增加退避时间，避免紧凑死循环
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            }
            continue;
        }

        std::string songId;
        double t, d;
        
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            // double check
            if (m_CDP && m_CDP->IsConnected()) {
                if (m_CDP->PollProgress(t, d, songId)) {
                    // 更新部分缓存
                    if (d > 0.1) m_LastDuration = d;
                    m_LastSongId = songId;

                    // 检查歌曲变更
                    if (!songId.empty() && songId != currentSongId) {
                        currentSongId = songId;
                        if (m_Callback) {
                            m_Callback(songId);
                        }
                    }
                }
            }
        }
    }
}

// ============================================================
// C API Implementation
// ============================================================

extern "C" {
    bool NETEASE_API Netease_Connect(int port) {
        return NeteaseDriver::Instance().Connect(port);
    }

    void NETEASE_API Netease_Disconnect() {
        NeteaseDriver::Instance().Disconnect();
    }

    bool NETEASE_API Netease_GetState(IPC::NeteaseState* outState) {
        if (!outState) return false;
        
        // 简单直接调用，因为 GetState 内部已经处理了未连接情况
        *outState = NeteaseDriver::Instance().GetState();
        return true;
    }

    // 定义 C 风格的回调函数指针类型
    typedef void (*Netease_Callback)(const char* songId);
    
    // 全局静态变量保存 C 回调，用于转接
    static Netease_Callback g_CCallback = nullptr;

    void NETEASE_API Netease_SetTrackChangedCallback(Netease_Callback callback) {
        g_CCallback = callback;
        if (callback) {
            // 注册 C++ 回调，转接给 C 回调
            NeteaseDriver::Instance().SetTrackChangedCallback([](const std::string& songId) {
                if (g_CCallback) {
                    g_CCallback(songId.c_str());
                }
            });
        } else {
            NeteaseDriver::Instance().SetTrackChangedCallback(nullptr);
        }
    }

    // --- 新增 C-API 导出 ---

    typedef void (*Netease_LogCallback)(const char* level, const char* msg);
    static Netease_LogCallback g_CLogCallback = nullptr;

    void NETEASE_API Netease_SetLogCallback(Netease_LogCallback callback) {
        g_CLogCallback = callback;
        if (callback) {
            NeteaseDriver::Instance().SetLogCallback([](const std::string& level, const std::string& msg) {
                if (g_CLogCallback) {
                    g_CLogCallback(level.c_str(), msg.c_str());
                }
            });
        } else {
            NeteaseDriver::Instance().SetLogCallback(nullptr);
        }
    }

    int NETEASE_API Netease_GetInstallPath(char* buffer, int maxLen) {
        std::string path = NeteaseDriver::GetInstallPath();
        if (buffer && maxLen > 0) {
            strncpy_s(buffer, maxLen, path.c_str(), _TRUNCATE);
        }
        return (int)path.length();
    }

    bool NETEASE_API Netease_IsHookInstalled() {
        return NeteaseDriver::IsHookInstalled();
    }

    bool NETEASE_API Netease_InstallHook(const char* dllPath) {
        std::string path = dllPath ? dllPath : "version.dll";
        return NeteaseDriver::InstallHook(path);
    }

    bool NETEASE_API Netease_RestartApplication(const char* installPath) {
        std::string path = installPath ? installPath : "";
        return NeteaseDriver::RestartApplication(path);
    }
}
