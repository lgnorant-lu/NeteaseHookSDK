/**
 * c_demo.c - 网易云音乐 Hook SDK C-API 使用示例
 * 
 * 演示如何加载 DLL 并使用导出的 C 函数。
 * 可作为 Python (ctypes), C# (P/Invoke), Go (cgo) 等语言调用的参考。
 */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <stdbool.h>

// ============================================================
// 函数指针定义
// ============================================================

// 结构体定义必须匹配 C++ IPC::NeteaseState 布局
typedef struct {
    double currentProgress;
    double totalDuration;
    bool isPlaying;
    char songId[32];
    char _padding[336]; // 填充以匹配 384 字节（如有需要），或依赖对齐
} NeteaseState;

// 回调类型
typedef void (*TrackChangedCallback)(const char* songId);
typedef void (*LogCallback)(const char* level, const char* msg);

// 函数原型
typedef bool (*Fn_Connect)(int);
typedef void (*Fn_Disconnect)();
typedef bool (*Fn_GetState)(NeteaseState*);
typedef void (*Fn_SetTrackChangedCallback)(TrackChangedCallback);
typedef void (*Fn_SetLogCallback)(LogCallback);
typedef int  (*Fn_GetInstallPath)(char*, int);
typedef bool (*Fn_RestartApplication)(const char*);

// ============================================================
// 实现
// ============================================================

void MyTrackChanged(const char* songId) {
    printf("\n[回调] 歌曲已变更: %s\n> ", songId);
}

void MyLog(const char* level, const char* msg) {
    printf("[SDK 日志] [%s] %s\n", level, msg);
}

int main() {
    // 设置控制台代码页为 UTF-8 以支持中文显示
    SetConsoleOutputCP(65001);
    
    printf("正在加载 NeteaseDriver.dll...\n");
    
    // 加载 DLL
    HMODULE hDll = LoadLibraryA("NeteaseDriver.dll"); // 确保 DLL 在 PATH 或当前目录下
    if (!hDll) {
        // 开发环境的回退路径
        hDll = LoadLibraryA("bin/NeteaseDriver.dll");
        if (!hDll) hDll = LoadLibraryA("build_chk/bin/NeteaseDriver.dll"); // 尝试 build_chk 路径
    }
    
    if (!hDll) {
        printf("无法加载 NeteaseDriver.dll (错误码: %lu)\n", GetLastError());
        return 1;
    }

    // 获取函数指针
    Fn_Connect Connect = (Fn_Connect)GetProcAddress(hDll, "Netease_Connect");
    Fn_Disconnect Disconnect = (Fn_Disconnect)GetProcAddress(hDll, "Netease_Disconnect");
    Fn_GetState GetState = (Fn_GetState)GetProcAddress(hDll, "Netease_GetState");
    Fn_SetTrackChangedCallback SetTrackCB = (Fn_SetTrackChangedCallback)GetProcAddress(hDll, "Netease_SetTrackChangedCallback");
    Fn_SetLogCallback SetLogCB = (Fn_SetLogCallback)GetProcAddress(hDll, "Netease_SetLogCallback");
    Fn_GetInstallPath GetPath = (Fn_GetInstallPath)GetProcAddress(hDll, "Netease_GetInstallPath");

    if (!Connect || !GetState) {
        printf("无法定位所需的导出函数。\n");
        return 1;
    }

    // 1. 设置日志
    if (SetLogCB) {
        printf("正在设置日志回调...\n");
        SetLogCB(MyLog);
    }

    // 2. 检查安装
    if (GetPath) {
        char path[1024] = {0};
        GetPath(path, 1024);
        printf("网易云安装路径: %s\n", path);
    }

    // 3. 连接
    printf("正在连接到网易云音乐...\n");
    if (!Connect(9222)) {
        printf("连接失败。请确保网易云音乐正在运行。\n");
        // 可选: 如果导出了 RestartApplication，可在此尝试重启
    } else {
        printf("连接成功！\n");
    }

    // 4. 注册回调
    if (SetTrackCB) {
        SetTrackCB(MyTrackChanged);
    }

    // 5. 主循环
    printf("开始监控... (按 Ctrl+C 退出)\n");
    printf("> ");
    
    NeteaseState state;
    int ticks = 0;
    
    while (1) {
        if (GetState(&state)) {
            // 每 2 秒打印一次状态，避免刷屏 (或者完全依赖回调)
            if (ticks % 4 == 0) {
                // printf("\r[%s] %.1f / %.1f s", state.isPlaying ? "播放中" : "暂停", state.currentProgress, state.totalDuration);
            }
        }
        Sleep(500);
        ticks++;
    }

    // 清理
    Disconnect();
    FreeLibrary(hDll);
    return 0;
}
