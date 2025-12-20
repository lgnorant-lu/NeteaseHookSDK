#pragma once
/**
 * SimpleLog.h - 轻量级日志系统
 * 
 * 使用示例:
 *   LOG_ERROR("下载失败: " << url);
 *   LOG_INFO("Cache 命中: ID=" << songId);
 */

#include <iostream>
#include <chrono>
#include <iomanip>

#ifndef LOG_TAG
#define LOG_TAG "GLOBAL"
#endif

#define LOG_TIMESTAMP() \
    do { \
        auto now = std::chrono::system_clock::now(); \
        auto time = std::chrono::system_clock::to_time_t(now); \
        std::cerr << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "] "; \
    } while(0)

#define LOG_ERROR(msg) \
    do { LOG_TIMESTAMP(); std::cerr << "[ERROR][" << LOG_TAG << "] " << msg << std::endl; } while(0)

#define LOG_WARN(msg) \
    do { LOG_TIMESTAMP(); std::cerr << "[WARN][" << LOG_TAG << "] " << msg << std::endl; } while(0)

#define LOG_INFO(msg) \
    do { LOG_TIMESTAMP(); std::cerr << "[INFO][" << LOG_TAG << "] " << msg << std::endl; } while(0)

// 条件式 DEBUG 日志（仅 Debug 构建启用）
#ifdef _DEBUG
#define LOG_DEBUG(msg) \
    do { LOG_TIMESTAMP(); std::cerr << "[DEBUG][" << LOG_TAG << "] " << msg << std::endl; } while(0)
#else
#define LOG_DEBUG(msg) do {} while(0)
#endif
