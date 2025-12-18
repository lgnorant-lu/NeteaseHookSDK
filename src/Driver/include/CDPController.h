#pragma once
#include <string>
#include <functional>

/**
 * CDP 控制器 - Chrome DevTools Protocol 客户端
 * 
 * 用于连接到网易云音乐的调试端口，注入 JavaScript 并接收播放数据
 * 
 * 技术原理：
 * 1. 通过 HTTP 请求 /json 端点获取可调试页面列表
 * 2. 找到 orpheus:// 开头的内核页面
 * 3. 通过 WebSocket 连接到该页面
 * 4. 使用 Runtime.evaluate 执行 JavaScript
 * 5. 注册 channel.registerCall 事件监听获取播放进度
 */
class CDPController {
public:
    /**
     * 构造函数
     * @param port CDP 调试端口（默认 9222）
     */
    CDPController(int port = 9222);
    
    /**
     * 析构函数 - 自动断开连接
     */
    ~CDPController();

    /**
     * 连接到 NCM 内核页面
     * @return 是否成功连接
     */
    bool Connect();
    
    /**
     * 断开连接
     */
    void Disconnect();
    
    /**
     * 执行 JavaScript 并返回结果
     * @param expression JavaScript 表达式
     * @return JSON 格式的执行结果
     */
    std::string Evaluate(const std::string& expression);
    
    /**
     * 注册播放进度事件监听
     * 调用 channel.registerCall("audioplayer.onPlayProgress", ...)
     * @return 是否成功注册
     */
    bool RegisterProgressListener();
    
    /**
     * 轮询获取最新播放进度
     * @param outTime 输出：当前播放时间（秒）
     * @param outDuration 输出：总时长（秒）
     * @param outSongId 输出：歌曲 ID
     * @return 是否成功获取到有效数据
     */
    bool PollProgress(double& outTime, double& outDuration, std::string& outSongId);
    
    /**
     * 检查是否已连接
     */
    bool IsConnected() const { return m_Connected; }

private:
    // 获取内核页面的 WebSocket URL
    std::string GetKernelPageWSUrl();
    
    // 发送 CDP 命令并等待响应
    std::string SendCommand(const std::string& method, const std::string& params);

private:
    int m_Port;                // CDP 端口
    bool m_Connected;          // 连接状态
    void* m_WebSocket;         // WebSocket 连接 (easywsclient::WebSocket*)
    int m_MessageId;           // 消息 ID 计数器
    std::string m_LastResponse;// 最后收到的响应
};
