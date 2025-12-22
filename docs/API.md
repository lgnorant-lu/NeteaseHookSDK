# API 参考手册 (API Reference) [v0.1.2]

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

## 4. 日志控制接口 (Logging Control) [v0.1.2+]

### `Netease_SetGlobalLogging`
```c
void Netease_SetGlobalLogging(bool enabled);
```
全局开关：启用或禁用 SDK 内部日志输出。
*   **enabled**: `true` 启用日志，`false` 禁用日志
*   **默认值**: `false` (静默模式)

> **重要**: SDK 默认不输出任何日志，确保不干扰宿主程序的标准输出。

### `Netease_SetGlobalLogLevel`
```c
void Netease_SetGlobalLogLevel(int level);
```
设置日志过滤级别。
*   **level**: `0`=ERROR, `1`=WARN, `2`=INFO, `3`=DEBUG
*   **默认值**: `2` (INFO)

### `Netease_SetAbsoluteSilence`
```c
void Netease_SetAbsoluteSilence(bool enable);
```
物理级静默控制。
*   **enable**: `true` 将 `stderr` 重定向到 `NUL` (彻底压制所有输出，包括第三方库噪音)；`false` 恢复标准输出。
*   **适用场景**: 生产环境集成，需要 100% 洁净的控制台。
*   **注意**: 开启后，所有调试日志（即使已开启）都将不可见。

### 示例: 调试模式
```c
// 开启调试日志
Netease_SetGlobalLogging(true);
Netease_SetGlobalLogLevel(3);  // 显示所有日志，包括 DEBUG

Netease_Connect(9222);
// ... 调试完成后
Netease_SetGlobalLogging(false);  // 恢复静默
```

### 示例: 生产环境静默
```c
// 完全静默模式（推荐用于自动化工具）
Netease_SetAbsoluteSilence(true);  // 物理级压制所有输出
Netease_Connect(9222);
// ... 即使 WebSocket 连接失败，也不会有任何终端输出
```

## 5. 辅助与安装接口 (Utility & Installer)

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

## 6. C++ 工具模块 (Netease::API)

自 v0.1.2 起，SDK 提供了 `Netease::API` 静态类，封装了网易云音乐 WebAPI，用于直接获取歌词和元数据。此功能不依赖 Hook，而是直接进行 HTTP 请求。

**头文件**: `#include <NeteaseAPI.h>`  
**命名空间**: `Netease`

### 5.1 数据结构

```cpp
struct SongMetadata {
    long long songId;           // 歌曲 ID
    std::string title;          // 歌曲标题
    std::vector<std::string> artists;  // 艺术家列表
    std::string album;          // 专辑名称
    std::string albumPicUrl;    // 专辑封面 URL
    long long duration;         // 时长（毫秒）
};

struct LyricData {
    std::string lrc;            // 原版歌词
    std::string tlyric;         // 翻译歌词
    std::string romalrc;        // 罗马音
    bool fromCache;             // 是否来自本地缓存
    
    // 合并 LRC 与 翻译 (例如: "Hello / 你好")
    std::string GetMergedLyric() const;
};
```

### 5.2 核心方法

#### `API::GetLyric`
```cpp
static std::optional<LyricData> GetLyric(long long songId, bool useCache = true, const std::string& cookie = "");
```
智能获取歌词。采用 **Cache-Aside** 策略：
1.  优先查询本地缓存（涵盖网易云官方目录和 SDK 目录）。
2.  缓存未命中或 `useCache=false` 时，发起在线请求。
3.  请求成功后自动回写缓存。

#### `API::GetSongDetail`
```cpp
static std::optional<SongMetadata> GetSongDetail(long long songId);
```
通过 SongID 查询歌曲详情（标题、封面、专辑等）。不需要 Cookie。

#### `API::GetLocalLyric`
```cpp
static std::optional<LyricData> GetLocalLyric(long long songId);
```
仅查询本地缓存。自动处理 JSON 转义和格式解析。

#### `API::FetchLyricOnline`
```cpp
static std::optional<LyricData> FetchLyricOnline(long long songId, const std::string& cookie = "", bool autoCache = true);
```
强制在线获取。`autoCache=true` 时会自动更新本地缓存。

### 5.3 缓存管理

#### `API::CacheLyric`
```cpp
static bool CacheLyric(long long songId, const LyricData& data);
```
手动写入缓存。

#### `API::ClearLyricCache`
```cpp
static bool ClearLyricCache(long long songId);
```
清除指定歌曲的 SDK 缓存。

#### `API::ClearAllCache`
```cpp
static int ClearAllCache();
```
清除 SDK 生成的所有歌词缓存文件。
