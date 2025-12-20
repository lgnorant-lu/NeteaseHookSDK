#include <windows.h>
#include <string>
#include <vector>
#include "MinHook.h"

// 转发导出函数到系统 version.dll
// 这种方式需要确保路径正确，通常 C:\Windows\System32\version.dll 是安全的
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA=C:\\Windows\\System32\\version.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

// 全局变量保存修改后的命令行
std::wstring g_NewCmdLine;

// 原始 GetCommandLineW 函数指针
typedef LPWSTR(WINAPI* GetCommandLineW_t)();
GetCommandLineW_t fpGetCommandLineW = NULL;

#include <ctime>
#include <iomanip>
#include <sstream>

// 简单的文件日志
void Log(const std::wstring& msg) {
    FILE* fp = _wfopen(L"ncm_hook.log", L"a+");
    if (fp) {
        time_t now = time(nullptr);
        tm tstruct;
        localtime_s(&tstruct, &now);
        wchar_t timeBuf[20];
        wcsftime(timeBuf, sizeof(timeBuf)/sizeof(wchar_t), L"%H:%M:%S", &tstruct);
        
        fwprintf(fp, L"[%s][Proxy] %s\n", timeBuf, msg.c_str());
        fclose(fp);
    }
}

// Detour 函数
LPWSTR WINAPI DetourGetCommandLineW() {
    // 第一次调用时初始化
    if (g_NewCmdLine.empty()) {
        // 获取原始命令行
        LPWSTR oldCmd = fpGetCommandLineW();
        if (oldCmd) {
            g_NewCmdLine = oldCmd;
            
            Log(L"[Proxy] Original Cmd: " + std::wstring(oldCmd));

            // 检查是否已经包含调试端口参数
            if (g_NewCmdLine.find(L"--remote-debugging-port") == std::wstring::npos) {
                // 追加参数
                g_NewCmdLine += L" --remote-debugging-port=9222";
                Log(L"[Proxy] Injected --remote-debugging-port=9222");
            } else {
                Log(L"[Proxy] Already in debug mode");
            }
        }
    }
    return (LPWSTR)g_NewCmdLine.c_str();
}

// 初始化 Hook
void InitHook() {
    Log(L"[Proxy] InitHook...");
    if (MH_Initialize() != MH_OK) {
        Log(L"[Proxy] MH_Initialize failed");
        return; 
    }

    if (MH_CreateHook(GetCommandLineW, &DetourGetCommandLineW, reinterpret_cast<LPVOID*>(&fpGetCommandLineW)) != MH_OK) {
        Log(L"[Proxy] MH_CreateHook failed");
        return;
    }

    if (MH_EnableHook(GetCommandLineW) != MH_OK) {
        Log(L"[Proxy] MH_EnableHook failed");
        return;
    }
    Log(L"[Proxy] Hook installed success!");
}

// DLL 入口点
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InitHook();
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        MH_Uninitialize();
        break;
    }
    return TRUE;
}
