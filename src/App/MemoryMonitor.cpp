/**
 * MemoryMonitor.cpp - Windows 内存监控实现
 * 
 * 此文件包含 Windows.h，不能与 Raylib 在同一编译单元
 */

#define LOG_TAG "MEM"
#include "MemoryMonitor.h"
#include "SimpleLog.h"

// Windows API (仅在此文件中包含)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

namespace Netease {

float MemoryMonitor::GetProcessMemoryMB() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), 
                             (PROCESS_MEMORY_COUNTERS*)&pmc, 
                             sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024.0f * 1024.0f);
    }
    return 0.0f;
}

} // namespace Netease
