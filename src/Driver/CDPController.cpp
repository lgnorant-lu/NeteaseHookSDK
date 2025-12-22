/**
 * CDPController.cpp - Chrome DevTools Protocol 控制器实现
 * 
 * 网易云音乐 Hook SDK v0.0.1
 * 
 * 依赖库：
 * - cpp-httplib (HTTP 客户端)
 * - easywsclient (WebSocket 客户端)
 */

#define LOG_TAG "CDP"
#include "CDPController.h"
#include "SimpleLog.h"

// Windows 网络库
#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

// 第三方库 - httplib 不使用 OpenSSL
#define CPPHTTPLIB_NO_EXCEPTIONS 1
#include "httplib.h"
#include "easywsclient.hpp"
// 注意：easywsclient.cpp 单独编译，不在这里包含

#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <regex>

// ============================================================
// JavaScript 载荷
// ============================================================

// 注册播放进度事件监听
// 关键发现：onPlayProgress 只传 currentTime，duration 在 currentTrack 对象中
static const char* REGISTER_PAYLOAD = R"(
(function() {
    if (!window.channel || !window.channel.registerCall) {
        return { success: false, error: "NO_CHANNEL" };
    }
    
    // onPlayProgress 只传当前时间
    window.channel.registerCall("audioplayer.onPlayProgress", function(songId, currentTime) {
        window.__NCM_PROGRESS__ = window.__NCM_PROGRESS__ || {};
        window.__NCM_PROGRESS__.songId = String(songId || '');
        window.__NCM_PROGRESS__.currentTime = Number(currentTime) || 0;
        window.__NCM_PROGRESS__.timestamp = Date.now();
    });
    
    return { success: true };
})();
)";

// 轮询获取播放数据 + 自动重注册 + 从真正的input元素提取 Duration
static const char* POLL_PAYLOAD = R"(
(function() {
    // 检查并自动重新注册（解决时序问题）
    if (!window.__NCM_PROGRESS__ || (Date.now() - (window.__NCM_PROGRESS__.timestamp || 0) > 5000)) {
        if (window.channel && window.channel.registerCall) {
            window.channel.registerCall("audioplayer.onPlayProgress", function(songId, currentTime) {
                window.__NCM_PROGRESS__ = window.__NCM_PROGRESS__ || {};
                window.__NCM_PROGRESS__.songId = String(songId || '');
                window.__NCM_PROGRESS__.currentTime = Number(currentTime) || 0;
                window.__NCM_PROGRESS__.timestamp = Date.now();
            });
        }
    }
    
    var p = window.__NCM_PROGRESS__ || {};
    var currentTime = p.currentTime || 0;
    var songId = p.songId || '';
    var duration = 0;
    
    try {
        // 先找slider容器
        var slider = document.querySelector('[class*="slider"][class*="StyledSliderContainer"]');
        if (!slider) slider = document.querySelector('[class*="slider"]');
        
        if (slider) {
            // 在容器内部查找真正的input元素
            var input = slider.querySelector('input[type="range"]');
            if (!input) input = slider.querySelector('input');
            
            if (input) {
                // 方案1: 直接从HTML属性读取（最可靠）
                if (input.max) {
                    duration = parseFloat(input.max);
                }
                // 方案2: 从React Fiber读取
                else {
                    for (var key in input) {
                        if (key.startsWith('__reactInternalInstance') || 
                            key.startsWith('__reactFiber')) {
                            var fiber = input[key];
                            if (fiber) {
                                var props = fiber.pendingProps || fiber.memoizedProps;
                                if (props && typeof props.max === 'number') {
                                    duration = props.max;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch(e) {}
    
    return { 
        songId: songId,
        currentTime: currentTime,
        duration: duration
    };
})();
)";


// ============================================================
// 构造/析构
// ============================================================

CDPController::CDPController(int port)
    : m_Port(port)
    , m_Connected(false)
    , m_WebSocket(nullptr)
    , m_MessageId(0)
{
#ifdef _WIN32
    // 初始化 Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

CDPController::~CDPController() {
    Disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

// ============================================================
// 获取内核页面 WebSocket URL
// ============================================================

std::string CDPController::GetKernelPageWSUrl() {
    httplib::Client client("127.0.0.1", m_Port);
    client.set_connection_timeout(5);
    
    auto res = client.Get("/json");
    if (!res || res->status != 200) {
        LOG_ERROR("无法访问 /json 端点 (端口 " << m_Port << ")");
        return "";
    }
    
    std::string body = res->body;
    
    // 查找 orpheus:// 开头的内核页面 (更宽松的匹配)
    // v0.1.2: 允许大小写不敏感或部分匹配，应对不同版本的 NCM
    std::string bodyLower = body;
    for (auto &c : bodyLower) c = tolower(c);
    
    size_t orpheusPos = bodyLower.find("orpheus://");
    
    if (orpheusPos == std::string::npos) {
        // v0.1.2: 冲突检测 - 如果端口通但没有 orpheus，看看是不是被别人占了
        if (body.find("\"url\"") != std::string::npos) {
            LOG_ERROR("[CRITICAL] 端口 " << m_Port << " 被非网易云程序占用!");
            std::string snippet = (body.length() > 300) ? body.substr(0, 300) : body;
            LOG_ERROR("占用程序响应片段: " << snippet);
        } else {
            LOG_ERROR("未找到 orpheus:// 内核页面. /json 响应长度: " << body.length());
        }
        return "";
    }
    
    // 恢复到原始大小写的 body 进行后续处理（防止 URL 损坏）
    // 注意：orpheusPos 在两者中是一致的
    
    // 找到页面对象的边界
    size_t objStart = body.rfind("{", orpheusPos);
    size_t objEnd = body.find("}", orpheusPos);
    
    if (objStart == std::string::npos || objEnd == std::string::npos) {
        return "";
    }
    
    std::string pageObj = body.substr(objStart, objEnd - objStart + 1);
    
    // 提取 webSocketDebuggerUrl
    std::regex wsRegex("\"webSocketDebuggerUrl\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    
    if (std::regex_search(pageObj, match, wsRegex) && match.size() > 1) {
        return match[1].str();
    }
    
    return "";
}

// ============================================================
// 连接/断开
// ============================================================

bool CDPController::Connect() {
    if (m_Connected) {
        return true;
    }
    
    std::string wsUrl = GetKernelPageWSUrl();
    if (wsUrl.empty()) {
        return false;
    }
    
    LOG_INFO("连接到: " << wsUrl);
    
    using easywsclient::WebSocket;
    m_WebSocket = WebSocket::from_url(wsUrl);
    
    if (!m_WebSocket) {
        LOG_ERROR("WebSocket 连接失败");
        return false;
    }
    
    m_Connected = true;
    LOG_INFO("连接成功!");
    return true;
}

void CDPController::Disconnect() {
    if (m_WebSocket) {
        auto ws = static_cast<easywsclient::WebSocket*>(m_WebSocket);
        ws->close();
        delete ws;
        m_WebSocket = nullptr;
    }
    m_Connected = false;
}

// ============================================================
// 发送 CDP 命令
// ============================================================

std::string CDPController::SendCommand(const std::string& method, const std::string& params) {
    if (!m_WebSocket) {
        return "";
    }
    
    auto ws = static_cast<easywsclient::WebSocket*>(m_WebSocket);
    
    m_MessageId++;
    
    // 构建 CDP 消息
    std::ostringstream cmd;
    cmd << "{\"id\":" << m_MessageId << ",\"method\":\"" << method << "\"";
    if (!params.empty()) {
        cmd << ",\"params\":" << params;
    }
    cmd << "}";
    
    ws->send(cmd.str());
    
    // 等待响应
    m_LastResponse = "";
    std::string targetId = "\"id\":" + std::to_string(m_MessageId);
    
    // 优化：减少阻塞时间，提高 UI 响应速度
    // 旧配置: 50 * 100ms = 5s (导致 UI 卡顿)
    // 新配置: 200 * 1ms = 200ms (足够本地 IPC 响应，且不明显卡顿)
    int timeout = 200;  
    while (timeout-- > 0) {
        // 使用 0 或 1ms 所谓的 "非阻塞" 轮询
        // easywsclient 在 Windows 下使用 select，超时精度尚可
        ws->poll(1);
        ws->dispatch([this, &targetId](const std::string& msg) {
            if (msg.find(targetId) != std::string::npos) {
                m_LastResponse = msg;
            }
        });
        
        if (!m_LastResponse.empty()) {
            break;
        }
    }
    
    return m_LastResponse;
}

// ============================================================
// JavaScript 执行
// ============================================================

std::string CDPController::Evaluate(const std::string& expression) {
    // 转义表达式中的特殊字符
    std::ostringstream params;
    params << "{\"expression\":\"";
    
    for (char c : expression) {
        switch (c) {
            case '"': params << "\\\""; break;
            case '\\': params << "\\\\"; break;
            case '\n': params << "\\n"; break;
            case '\r': params << "\\r"; break;
            case '\t': params << "\\t"; break;
            default: params << c;
        }
    }
    
    params << "\",\"returnByValue\":true}";
    
    return SendCommand("Runtime.evaluate", params.str());
}

// ============================================================
// 播放进度监听
// ============================================================

bool CDPController::RegisterProgressListener() {
    std::string result = Evaluate(REGISTER_PAYLOAD);
    
    // 检查是否成功
    if (result.find("\"success\":true") != std::string::npos ||
        result.find("\"success\": true") != std::string::npos) {
        LOG_INFO("播放进度监听已注册!");
        return true;
    }
    
    LOG_ERROR("注册失败: " << result);
    return false;
}

bool CDPController::PollProgress(double& outTime, double& outDuration, std::string& outSongId) {
    std::string result = Evaluate(POLL_PAYLOAD);
    
    if (result.empty()) {
        return false;
    }
    
    // 解析 currentTime
    std::regex timeRegex("\"currentTime\"\\s*:\\s*([0-9.]+)");
    std::smatch timeMatch;
    
    if (std::regex_search(result, timeMatch, timeRegex) && timeMatch.size() > 1) {
        try {
            outTime = std::stod(timeMatch[1].str());
        } catch (...) {
            outTime = 0;
        }
    } else {
        return false;
    }
    
    // 解析 duration
    std::regex durationRegex("\"duration\"\\s*:\\s*([0-9.]+)");
    std::smatch durationMatch;
    
    if (std::regex_search(result, durationMatch, durationRegex) && durationMatch.size() > 1) {
        try {
            outDuration = std::stod(durationMatch[1].str());
        } catch (...) {
            outDuration = 0;
        }
    } else {
        outDuration = 0;
    }
    
    // 解析 songId
    std::regex songRegex("\"songId\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch songMatch;
    
    if (std::regex_search(result, songMatch, songRegex) && songMatch.size() > 1) {
        outSongId = songMatch[1].str();
    }
    
    return outTime > 0;
}
