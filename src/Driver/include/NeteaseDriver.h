#pragma once
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include "SharedData.hpp"

// 前向声明
class CDPController;

// 调用约定宏 (Calling Convention)
// 确保跨编译器/跨架构兼容性，特别是 x86 环境
#ifdef _MSC_VER
    #define NETEASE_API __cdecl
#else
    #define NETEASE_API
#endif


/**
 * NeteaseDriver - 网易云音乐播放状态监控 SDK
 * 
 * 这是 SDK 的主入口类，提供简单易用的 API 获取播放进度
 * 
 * 使用前提：
 * - 网易云音乐客户端需以 --remote-debugging-port=9222 参数启动
 * 
 * 使用示例：
 * ```cpp
 * NeteaseDriver driver;
 * if (driver.Connect()) {
 *     while (true) {
 *         auto state = driver.GetState();
 *         if (state.isPlaying) {
 *             printf("当前进度: %.2f 秒\n", state.currentProgress);
 *         }
 *         Sleep(500);
 *     }
 * }
 * ```
 */
class NeteaseDriver {
public:
    // 回调函数类型定义
    using TrackChangedCallback = std::function<void(const std::string&)>;
    using LogCallback = std::function<void(const std::string& level, const std::string& msg)>;

    /**
     * 构造函数
     */
    // 禁止拷贝
    NeteaseDriver(const NeteaseDriver&) = delete;
    NeteaseDriver& operator=(const NeteaseDriver&) = delete;

    /**
     * 获取单例实例
     */
    static NeteaseDriver& Instance();

    /**
     * 连接到网易云音乐
     * 
     * @param port CDP 调试端口（默认 9222）
     * @return 是否成功连接
     */
    bool Connect(int port = 9222);
    
    /**
     * 断开连接
     */
    void Disconnect();
    
    /**
     * 获取当前播放状态
     * 线程安全
     * 
     * @return 播放状态结构体，包含进度、歌曲ID等
     */
    IPC::NeteaseState GetState();

    /**
     * 设置歌曲变更回调
     * 当检测到 songId 变化时触发
     * 
     * @param callback 回调函数
     */
    void SetTrackChangedCallback(TrackChangedCallback callback);

    /**
     * 设置日志回调
     * 重定向 SDK 内部日志
     */
    void SetLogCallback(LogCallback callback);

private:
    /**
     * 私有构造函数
     */
    NeteaseDriver();
    
    /**
     * 私有析构函数
     */
    ~NeteaseDriver();
    
    /**
     * 检查是否已连接
     */
    bool IsConnected() const;

    /**
     * 后台监控循环
     */
    void MonitorLoop();

    // 内部日志辅助函数
    void Log(const std::string& level, const std::string& msg) const;

private:
    CDPController* m_CDP;          // CDP 控制器
    bool m_ListenerRegistered;     // 是否已注册事件监听
    
    // 线程安全与并发控制
    mutable std::mutex m_Mutex;           // 保护 m_CDP 和 状态数据
    mutable std::mutex m_LogMutex;        // 保护日志回调
    std::thread m_MonitorThread;          // 后台轮询线程
    std::atomic<bool> m_Monitoring;       // 线程控制标志
    TrackChangedCallback m_Callback;      // 歌曲变更回调
    LogCallback m_LogCallback;            // 日志回调

    // 缓存最新状态（用于判断是否正在播放）
    double m_LastTime;
    double m_LastDuration;
    unsigned long long m_LastUpdateTimestamp; // 上次状态变化的时间戳 (GetTickCount64)
    std::string m_LastSongId;

public:
    // =======================================================
    // 自动部署 API (Installer)
    // =======================================================
    // ... (Static methods remain unchanged)
    static std::string GetInstallPath();
    static bool IsHookInstalled();
    static bool InstallHook(const std::string& dllPath = "version.dll");
    static bool RestartApplication(const std::string& installPath = "");
};
