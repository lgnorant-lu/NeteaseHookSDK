#pragma once
/**
 * SimpleLog.h - 轻量级日志系统 (v0.1.2 - Runtime Control)
 * 
 * 使用示例:
 *   LOG_ERROR("下载失败: " << url);
 *   LOG_INFO("Cache 命中: ID=" << songId);
 * 
 * 控制接口:
 *   LogControl::SetEnabled(true);   // 开启日志
 *   LogControl::SetEnabled(false);  // 关闭日志 (默认)
 */

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <atomic>

#ifndef LOG_TAG
#define LOG_TAG "GLOBAL"
#endif

// ============================================================
// 日志控制命名空间 (SDK-Safe Runtime Control)
// ============================================================

// 宏控制：如果是 SDK 内部编译，直接访问变量；如果是外部使用，通过 DLL 接口访问
#ifdef NETEASE_DRIVER_EXPORTS
    #define LOG_INTERNAL_API __declspec(dllexport)
#else
    #define LOG_INTERNAL_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif
    LOG_INTERNAL_API bool Netease_IsLogEnabled();
    LOG_INTERNAL_API int Netease_GetLogLevel();
#ifdef __cplusplus
}
#endif

namespace LogControl {
    inline bool IsEnabled() { return Netease_IsLogEnabled(); }
    inline int GetLevel() { return Netease_GetLogLevel(); }

    // 线程安全互斥锁 (仅用于排队 std::cerr 输出)
    inline std::mutex& GetMutex() {
        static std::mutex g_Mutex;
        return g_Mutex;
    }
}

// ============================================================
// 内部辅助宏 (线程安全输出)
// ============================================================
#define LOG_IMPL(levelStr, levelNum, msg) \
    do { \
        if (LogControl::IsEnabled() && LogControl::GetLevel() >= levelNum) { \
            std::ostringstream _oss; \
            auto _now = std::chrono::system_clock::now(); \
            auto _time = std::chrono::system_clock::to_time_t(_now); \
            _oss << "[" << std::put_time(std::localtime(&_time), "%H:%M:%S") << "] "; \
            _oss << "[" levelStr "][" << LOG_TAG << "] " << msg << std::endl; \
            { \
                std::lock_guard<std::mutex> _lock(LogControl::GetMutex()); \
                std::cerr << _oss.str(); \
            } \
        } \
    } while(0)

// ============================================================
// 公共日志宏
// ============================================================
#define LOG_ERROR(msg) LOG_IMPL("ERROR", 0, msg)
#define LOG_WARN(msg)  LOG_IMPL("WARN",  1, msg)
#define LOG_INFO(msg)  LOG_IMPL("INFO",  2, msg)

// 条件式 DEBUG 日志（仅 Debug 构建启用）
#ifdef _DEBUG
#define LOG_DEBUG(msg) LOG_IMPL("DEBUG", 3, msg)
#else
#define LOG_DEBUG(msg) do {} while(0)
#endif
