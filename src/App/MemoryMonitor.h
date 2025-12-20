#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

namespace Netease {
    /**
     * 内存监控工具类 (隔离 Windows API)
     * 
     * 注意：此类的实现文件包含 Windows.h，
     * 因此不能在同一个编译单元中与 Raylib 混用
     */
    class MemoryMonitor {
    public:
        /**
         * 获取当前进程内存使用 (MB)
         * @return 工作集大小（物理内存占用）
         */
        static float GetProcessMemoryMB();
    };
}

#endif // MEMORY_MONITOR_H
