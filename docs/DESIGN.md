# 设计与架构说明书 (Technical Design Document)

## 1. 架构总览 (System Architecture)

本项目旨在构建一个高可用、去耦合的进程间通信 (IPC) 桥梁，用于从外部进程实时获取 Electron 应用（网易云音乐）的内部运行时状态。

系统由三个在不同层级运作的模块组成：

1.  **Loader Layer (Agent)**: 负责环境预设，注入调试参数。
2.  **Transport Layer (Driver)**: 负责建立 WebSocket 隧道，管理协议握手。
3.  **Application Layer (SDK)**: 提供业务逻辑封装与 API 导出。

**版本**: 0.0.1 (Alpha)

## 2. 模块详解 (Module Deep Dive)

### 2.0 安装器层 (Installer Layer)

SDK 核心静态库内置了全自动的部署逻辑 (`src/Driver/NeteaseDriver.cpp` -> Static Methods)，使得第三方开发者无需编写任何文件复制或注册表查询代码即可实现“一键安装”。

*   **路径发现**: 通过读取注册表 `HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\CloudMusic` 或扫描运行中的 `cloudmusic.exe` 进程路径。
*   **原子部署**: 将 `version.dll` (Agent) 复制到目标目录，若文件被占用则尝试重命名旧文件。
*   **生命周期管理**: 提供 `RestartApplication` 接口，终止进程树并以原参数（Agent 会自动追加 Debug 参数）重启应用。

### 2.1 引导模块 (src/Agent)

该模块编译为 `version.dll`，驻留于目标进程的内存空间。

*   **Hook 技术**: 使用 MinHook (Trampoline Hook) 库。
*   **挂钩点**: `kernel32.dll!GetCommandLineW`。
*   **逻辑流**:
    1.  进程启动，加载器加载伪造的 `version.dll`。
    2.  `DllMain` 触发，初始化 Hook。
    3.  Electron 主进程调用 `GetCommandLineW` 获取启动参数。
    4.  Hook 函数拦截调用，分配新缓冲区，将原始命令行复制并追加 ` --remote-debugging-port=9222`。
    5.  返回修改后的命令行指针。

### 2.2 驱动模块 (src/Driver)

该模块运行在宿主进程（用户程序）中，通过 TCP/IP Loopback 接口与目标通信。

*   **服务发现**:
    *   发送 HTTP GET `http://localhost:9222/json`。
    *   解析返回的 JSON，寻找 `type: "page"` 且 `url` 匹配应用主页面的条目。
    *   提取 `webSocketDebuggerUrl`。

*   **Payload 注入**:
    *   建立 WebSocket 连接。
    *   构造 CDP 帧: `id: 1, method: "Runtime.evaluate", params: { expression: "..." }`。
    *   注入的 JavaScript 代码利用 `window.channel.registerCall` 挂钩 `audioplayer.onPlayProgress` 事件。

*   **数据模型**:
    *   **Shared State**: `NeteaseState` 结构体由 `std::mutex` 保护，支持多线程并发读。
    *   **Events**: 采用异步回调机制。当 WebSocket 收到 JSON 消息时，解析 payload，若检测到 songId 变更，在监控线程上下文中直接触发用户注册的 C 函数指针。

*   **Duration 提取策略 (Duration Fallback Strategy)**:
    由于 `audioplayer.onPlayProgress` 事件仅包含 `currentTime`，SDK 采用了一种复合策略来获取总时长 (`duration`)：
    1.  **Direct DOM**: 尝试读取进度条滑块 (`input[type="range"]`) 的 `max` 属性。
    2.  **React Fiber**: 如果 DOM 属性不可用，遍历 DOM 节点的内部属性 (`__reactInternalInstance$`, `__reactFiber$`)，直接从 React 组件的 `pendingProps.max` 或 `memoizedProps.max` 中提取数值。
    3.  **Cache**: 如果以上尝试均失败，保留最后一次成功获取的有效 Duration。

## 3. 线程与并发模型 (Concurrency Model)

SDK 内部维护一个独立的守护线程 (`MonitorLoop`)。

*   **主线程**:
    *   执行 `Netease_Connect` (初始化资源)。
    *   执行 `Netease_GetState` (读取原子状态)。
*   **守护线程**:
    *   **职责**:
        1.  WebSocket I/O (阻塞式/轮询式读取)。
        2.  执行心跳保活。
        3.  执行自动重连逻辑 (Exponential Backoff 策略 - 简易版每秒重试)。
    *   **资源竞争**:
        *   对 `m_CDP` 指针的访问受到互斥锁保护。
        *   回调函数的执行发生在守护线程，用户需注意回调内的线程安全。

## 4. 关键协议 (Protocol Specs)

### 4.1 CDP (Chrome DevTools Protocol)
仅使用 `Runtime` 域。不开启 `Network` 或 `DOM` 域以降低性能开销。

### 4.2 NCM Internal IPC
目标应用使用自定义的事件总线。
*   **Event**: `audioplayer.onPlayProgress`
*   **Payload**: `{ type: "progress", songId: "...", progress: 123.45 }` (结构经 SDK 归一化处理)
