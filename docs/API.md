# API 参考手册 (API Reference)

## 1. 概述

本 SDK 导出标准 C 接口 (`cdecl`)，依赖动态链接库 `NeteaseDriver.dll`。
所有字符串均使用 UTF-8 编码。

## 2. 数据结构

### `NeteaseState`
```c
#pragma pack(push, 8)
typedef struct {
    double currentProgress;   // 当前播放进度 (秒)
    double totalDuration;     // 总时长 (秒)
    char songId[64];          // 歌曲唯一标识符
    bool isPlaying;           // 播放状态标志
    wchar_t songName[64];     // [保留字段] 歌曲标题
    wchar_t artistName[64];   // [保留字段] 艺术家
} NeteaseState;
#pragma pack(pop)
```

## 3. 核心功能接口 (Core Functions)

### `Netease_Connect`
```c
bool Netease_Connect(int port);
```
启动 SDK 驱动并建立连接。
*   **port**: 远程调试端口，通常为 `9222`。
*   **Return**: `true` 表示连接请求已发送（异步建立），`false` 表示参数错误或驱动未初始化。

### `Netease_Disconnect`
```c
void Netease_Disconnect();
```
断开连接，释放后台线程与网络资源。

### `Netease_GetState`
```c
bool Netease_GetState(NeteaseState* outState);
```
获取当前播放状态的原子快照。
*   **outState**: 用户分配的结构体指针。
*   **Return**: `true` 表示数据读取成功且有效。

### `Netease_SetTrackChangedCallback`
```c
typedef void (*Netease_Callback)(const char* songId);
void Netease_SetTrackChangedCallback(Netease_Callback callback);
```
注册曲目变更事件的回调函数。
*   **注意**: 回调函数将在 SDK 的后台线程中执行。请勿在回调中执行耗时操作或阻塞操作。

### `Netease_SetLogCallback`
```c
typedef void (*Netease_LogCallback)(const char* level, const char* msg);
void Netease_SetLogCallback(Netease_LogCallback callback);
```
接管 SDK 内部日志输出。

## 4. 辅助与安装接口 (Utility & Installer)

### `Netease_GetInstallPath`
```c
int Netease_GetInstallPath(char* buffer, int maxLen);
```
尝试自动定位网易云音乐安装路径（通过注册表或运行中的进程）。
*   **buffer**: 接收路径的缓冲区。
*   **maxLen**: 缓冲区大小。
*   **Return**: 返回路径字符串的长度。

### `Netease_IsHookInstalled`
```c
bool Netease_IsHookInstalled();
```
检查 `version.dll` 是否已存在于网易云音乐安装目录下。

### `Netease_InstallHook`
```c
bool Netease_InstallHook(const char* dllPath);
```
部署代理 DLL (`version.dll`) 到目标目录。
*   **dllPath**: 源 DLL 路径（若为 NULL 则默认为 "version.dll"）。
*   **Return**: `true` 表示复制成功。

### `Netease_RestartApplication`
```c
bool Netease_RestartApplication(const char* installPath);
```
强制重启网易云音乐进程以应用 Hook。
*   **installPath**: 指定安装路径（若为 NULL 则自动获取）。
