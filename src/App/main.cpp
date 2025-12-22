/**
 * main.cpp - 网易云音乐 Hook SDK 测试程序
 * 
 * 使用 Raylib 显示播放进度 (Glassmorphism UI)
 * v0.1.2: 集成 Netease::API 显示歌词和元数据, 添加日志控制
 */

// v0.1.2: 完全隔离 Windows API，防止宏污染 Raylib 符号
// 参考 MemoryMonitor.cpp 的文件隔离模式
#include <LogRedirect.h>  // 文件日志重定向 (SDK 实现)
#include "raylib.h"
#include "rlgl.h"
#include "NeteaseDriver.h"
#include "NeteaseAPI.h"
#include "AlbumCover.h"
#include "AudioCapture.h"
#include "Visualizer.h"
#include "MemoryMonitor.h"  // 内存监控（Windows API 已隔离）
#include <vector>
#include <string>
#include <optional>
#include <iostream>
#include <chrono>

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#define LOG_TAG "MAIN"
#include "SharedData.hpp"
#include "SimpleLog.h"

// === UI 布局常量 (集中管理魔法数字) ===
namespace UIConstants {
    constexpr float CORNER_ROUNDNESS = 0.06f;      // 圆角比例
    constexpr int CORNER_SEGMENTS = 48;            // 圆角分段数
    constexpr float VINYL_ZONE_RATIO = 0.48f;      // 唱片区占比
    constexpr float DISPLAY_ZONE_RATIO = 0.52f;    // 显示区占比
    constexpr int COMPACT_WIDTH = 420;
    constexpr int COMPACT_HEIGHT = 260;
    constexpr int EXPANDED_WIDTH = 800;
    constexpr int EXPANDED_HEIGHT = 600;
}

// === 辅助函数 ===

/**
 * 从原始 songId 中提取数字部分
 * 输入: "1299570939_MFD4YQ" 或 "1299570939"
 * 输出: 1299570939
 */
long long ParseNumericSongId(const std::string& rawId) {
    if (rawId.empty()) return 0;
    size_t underscorePos = rawId.find('_');
    std::string numericPart = (underscorePos != std::string::npos) 
        ? rawId.substr(0, underscorePos) 
        : rawId;
    try {
        return std::stoll(numericPart);
    } catch (...) {
        return 0;
    }
}

/**
 * 窗口拖拽状态管理
 * 
 * 用于跟踪用户拖拽窗口的状态和位置偏移
 * 配合物理惯性系统实现流畅的窗口移动体验
 */
struct DragState {
    bool isDragging = false;    // 是否正在拖拽
    Vector2 offset = {0, 0};    // 鼠标相对窗口的偏移量
} static g_DragState;

/**
 * === 歌曲变更回调系统 ===
 * 
 * 使用互斥锁和原子变量实现线程安全的歌曲变更通知
 * CDP回调线程通过 g_PendingSongId 传递新歌曲ID
 * 主线程通过 g_HasNewSong 标志检测变更
 */
#include <mutex>
#include <atomic>

std::mutex g_ToastMutex;           // 保护 g_PendingSongId 的互斥锁
std::string g_PendingSongId;        // 待加载的歌曲ID (由CDP回调设置)
std::atomic<bool> g_HasNewSong(false);  // 新歌曲标志 (原子操作)

/**
 * Toast通知状态
 * 
 * 屏幕底部显示的临时通知消息
 * 自动淡入淡出动画，持续3秒后消失
 */
struct ToastState {
    float alpha = 0.0f;         // 当前透明度 (0.0 ~ 1.0)
    std::string message;        // 显示的消息文本
    double startTime = 0;       // 显示开始时间
} static g_Toast;

/**
 * === 高级歌词系统结构 ===
 * 
 * 支持多语言歌词显示，包含时间同步和翻译行
 */
struct LyricLine {
    double timestamp;           // 歌词行开始时间戳 (秒，支持毫秒精度)
    std::string text;           // 原始歌词文本 (主语言)
    std::string translation;    // 翻译文本 (可选，支持中/英翻译)
};

/**
 * 歌词系统管理器
 * 
 * 负责歌词的时间同步、索引更新和平滑滚动
 * 支持自动根据播放进度高亮当前歌词行
 */
struct LyricSystem {
    std::vector<LyricLine> lines;   // 所有歌词行（按时间排序）
    int currentIndex = -1;          // 当前激活的歌词索引 (-1表示未开始)
    float scrollOffset = 0.0f;      // 平滑滚动偏移量（像素，用于动画插值）
    
    /**
     * 清空歌词系统状态
     * 释放所有歌词数据并重置索引
     */
    void Clear() {
        lines.clear();
        currentIndex = -1;
        scrollOffset = 0.0f;
    }
    
    /**
     * 根据播放时间更新当前歌词索引
     * 
     * @param currentTime 当前播放时间 (秒)
     * 
     * 算法: 从前一个索引开始向后扫描，找到最后一个
     *       timestamp <= currentTime 的行作为当前行
     */
    void UpdateIndex(double currentTime) {
        if (lines.empty()) {
            currentIndex = -1;
            return;
        }
        
        int newIndex = -1;
        for (int i = 0; i < (int)lines.size(); i++) {
            if (lines[i].timestamp <= currentTime) {
                newIndex = i;
            } else {
                break;
            }
        }
        currentIndex = newIndex;
    }
};

/**
 * === 歌曲信息缓存系统 ===
 * 
 * 集中管理当前歌曲的所有相关数据，避免重复加载
 * 包含元数据、歌词、封面纹理等资源
 */
struct SongCache {
    long long numericId = 0;        // 数字形式的歌曲ID（用于API请求）
    std::string rawId;              // 原始ID字符串（用于检测歌曲变化）
    std::optional<Netease::SongMetadata> meta;  // 歌曲元数据（标题/艺术家/专辑等）
    std::optional<Netease::LyricData> lyric;    // 原始LRC歌词数据
    LyricSystem lyrics;             // 解析后的歌词系统（时间同步）
    Texture2D coverTexture = {0};   // 专辑封面OpenGL纹理（0表示未加载）
    bool isLoading = false;         // 异步加载标志（防止重复请求）
} static g_SongCache;

/**
 * === 唱片旋转动画系统 ===
 * 
 * 模拟黑胶唱片的物理旋转行为：
 * - 播放时：缓慢加速至33RPM稳定转速
 * - 暂停时：惯性减速至静止（模拟真实摩擦力）
 */
struct DiscRotation {
    float angle = 0.0f;             // 当前旋转角度 (0-360°，循环)
    float angularVelocity = 0.0f;   // 角速度 (°/秒)
    
    // 物理参数 (调整为缓慢的网易云风格)
    static constexpr float TARGET_RPM = 1.2f;      // 目标转速 (33.33 RPM 太快，降低至30)
    static constexpr float TARGET_OMEGA = (TARGET_RPM / 60.0f) * 360.0f;  // 转换为°/s
    static constexpr float ACCEL = 45.0f;           // 加速度 (°/s²)
    static constexpr float DECEL = 30.0f;           // 减速度 (°/s²)
    
    /**
     * 更新旋转状态 (每帧调用)
     * 
     * @param isPlaying 是否正在播放
     * @param deltaTime 帧时间 (秒)
     */
    void Update(bool isPlaying, float deltaTime) {
        float targetVelocity = isPlaying ? TARGET_OMEGA : 0.0f;
        float speedDiff = targetVelocity - angularVelocity;
        
        if (isPlaying) {
            // 加速阶段：线性平滑加速
            if (fabs(speedDiff) < ACCEL * deltaTime) {
                angularVelocity = targetVelocity;
            } else {
                angularVelocity += ((speedDiff > 0.0f) ? 1.0f : -1.0f) * ACCEL * deltaTime;
            }
        } else {
            // 停止阶段：模拟“磁吸摩擦” (Magnetic Brake)
            // 速度越高阻力越小，速度越低阻力越大，产生一种“粘滞”的停转感
            float magneticFactor = 1.0f + (1.0f - (angularVelocity / TARGET_OMEGA)) * 1.5f;
            float decelRate = DECEL * magneticFactor;
            
            if (fabs(angularVelocity) < decelRate * deltaTime) {
                angularVelocity = 0.0f;
            } else {
                angularVelocity -= (angularVelocity > 0.0f ? 1.0f : -1.0f) * decelRate * deltaTime;
            }
        }
        
        // 更新角度
        angle += angularVelocity * deltaTime;
        
        // 归一化到 0-360° 范围
        angle = fmodf(angle, 360.0f);
        if (angle < 0.0f) angle += 360.0f;
    }
} static g_DiscRotation;

/**
 * === 唱针 (Tonearm) 物理动画 ===
 * 
 * 模拟真实黑胶唱机唱针的机械运动：
 * - 播放开始：唱针平滑落下 (ANGLE_DOWN)
 * - 暂停：唱针抬起 (ANGLE_UP)
 * - 移动速度90°/s，提供流畅的动画过渡
 */
struct Tonearm {
    float angle = -15.0f;           // 当前角度 (相对于水平，负值表示向上抬)
    float targetAngle = -15.0f;     // 目标角度
    bool isDown = false;            // 是否已落下
    
    static constexpr float ANGLE_UP = -45.0f;       // 抬起位置 (待机状态)
    static constexpr float ANGLE_DOWN = -15.0f;     // 落下位置 (播放状态)
    static constexpr float MOVE_SPEED = 90.0f;      // 移动速度 (°/s)
    
    /**
     * 更新唱针角度 (每帧调用)
     * 
     * @param isPlaying 是否正在播放
     * @param deltaTime 帧时间 (秒)
     */
    void Update(bool isPlaying, float deltaTime) {
        targetAngle = isPlaying ? ANGLE_DOWN : ANGLE_UP;
        
        float diff = targetAngle - angle;
        if (fabs(diff) < MOVE_SPEED * deltaTime) {
            angle = targetAngle;
        } else {
            angle += ((diff > 0) ? 1.0f : -1.0f) * MOVE_SPEED * deltaTime;
        }
        isDown = (angle == ANGLE_DOWN);
    }
} static g_Tonearm;

/**
 * === 窗口状态与物理布局系统 ===
 */
enum WidgetState {
    STATE_COMPACT,   // 紧凑模式 (420x260，默认)
    STATE_EXPANDED   // 扩展模式 (800x600，显示更多信息)
};

/**
 * 物理布局管理器
 * 
 * 处理窗口尺寸的平滑过渡动画：
 * - 使用弹性插值实现自然的弹性效果
 * - 自动调用SetWindowSize更新实际窗口尺寸
 */
struct PhysicsLayout {
    WidgetState state = STATE_COMPACT;                  // 当前状态
    float currentWidth = (float)UIConstants::COMPACT_WIDTH;    // 当前宽度
    float currentHeight = (float)UIConstants::COMPACT_HEIGHT;  // 当前高度
    float targetWidth = (float)UIConstants::COMPACT_WIDTH;     // 目标宽度
    float targetHeight = (float)UIConstants::COMPACT_HEIGHT;   // 目标高度
    Vector2 velocity = {0, 0};                          // 惯性速度 (用于拖拽)
    
    /**
     * 更新布局 (每帧调用)
     * 
     * 使用弹性插值：current += (target - current) * speed * deltaTime
     * 当差值 < 0.1px 时停止更新以节省性能
     */
    void Update(float deltaTime) {
        // 1. 防止 deltaTime 过大导致计算失控 (限制在 10 FPS 级别的步长)
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        // 2. 平滑尺寸缩放 (使用带限制的增量)
        float speed = 12.0f;
        float stepX = (targetWidth - currentWidth) * speed * deltaTime;
        float stepY = (targetHeight - currentHeight) * speed * deltaTime;

        currentWidth += stepX;
        currentHeight += stepY;
        
        // 3. 强制最小尺寸保护，防止底层断言失败
        if (currentWidth < 1.0f) currentWidth = 1.0f;
        if (currentHeight < 1.0f) currentHeight = 1.0f;
        
        if (fabs(currentWidth - targetWidth) > 0.1f || fabs(currentHeight - targetHeight) > 0.1f) {
            SetWindowSize((int)currentWidth, (int)currentHeight);
        } else {
            // 逼近目标时强制对齐，确保动画完美结束
            currentWidth = targetWidth;
            currentHeight = targetHeight;
            SetWindowSize((int)currentWidth, (int)currentHeight);
        }
    }
} static g_Layout;

static Shader g_GlassShader;
static Shader g_AuroraShader;
static Shader g_MaskShader;


// 解析 LRC 格式字符串并存入 map (time -> text)
static std::map<double, std::string> ParseLrcToMap(const std::string& lrc, double* outOffset = nullptr) {
    std::map<double, std::string> result;
    if (lrc.empty()) return result;
    
    std::stringstream ss(lrc);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        
        // 1. 处理 [offset:X] 标签
        if (outOffset && line.find("[offset:") != std::string::npos) {
            size_t start = line.find(':') + 1;
            size_t end = line.find(']');
            if (end != std::string::npos) {
                try {
                    *outOffset = std::stod(line.substr(start, end - start)) / 1000.0;
                } catch (...) {}
            }
            continue;
        }

        // 2. 查找所有时间标签 [mm:ss.xx]
        std::vector<double> timestamps;
        size_t lastBracketEnd = 0;
        size_t pos = 0;
        
        while (true) {
            size_t bracketStart = line.find('[', pos);
            size_t bracketEnd = line.find(']', pos);
            if (bracketStart == std::string::npos || bracketEnd == std::string::npos || bracketEnd < bracketStart) break;
            
            std::string tag = line.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
            
            // 精确解析 mm:ss.xx (手动处理小数点后的位数)
            int m = 0, s = 0;
            size_t colon = tag.find(':');
            size_t dot = tag.find_first_of(".:"); // 可能是 . 也可能是 :
            if (dot == colon) dot = tag.find_last_of(".:"); // 找到最后一个分隔符
            
            if (colon != std::string::npos) {
                m = std::atoi(tag.substr(0, colon).c_str());
                if (dot != std::string::npos && dot > colon) {
                    s = std::atoi(tag.substr(colon + 1, dot - colon - 1).c_str());
                    std::string fracStr = tag.substr(dot + 1);
                    if (!fracStr.empty()) {
                        double frac = std::atof(("0." + fracStr).c_str());
                        timestamps.push_back(m * 60.0 + s + frac);
                    } else {
                        timestamps.push_back(m * 60.0 + s);
                    }
                } else {
                    s = std::atoi(tag.substr(colon + 1).c_str());
                    timestamps.push_back(m * 60.0 + (double)s);
                }
            }
            
            lastBracketEnd = bracketEnd;
            pos = bracketEnd + 1;
            // 贪婪跳过所有标签继续查找
            size_t nextBracket = line.find('[', pos);
            if (nextBracket == std::string::npos) break;
            
            // 如果中间只有空格，继续处理下一个标签
            std::string between = line.substr(pos, nextBracket - pos);
            bool onlySpace = true;
            for(char c : between) { if(!isspace((unsigned char)c)) { onlySpace = false; break; } }
            if (!onlySpace) break;
            pos = nextBracket;
        }
        
        if (timestamps.empty()) continue;
        
        // 提取文本内容
        std::string text = line.substr(lastBracketEnd + 1);
        text.erase(0, text.find_first_not_of(" \t\r\n"));
        size_t last = text.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) text.erase(last + 1);
        
        if (!text.empty()) {
            for (double ts : timestamps) {
                result[ts] = text;
            }
        }
    }
    return result;
}

// 解析并合并原版和翻译歌词
static LyricSystem ParseLyrics(const std::string& lrc, const std::string& tlrc) {
    LyricSystem system;
    double offset = 0;
    auto lrcMap = ParseLrcToMap(lrc, &offset);
    auto tlrcMap = ParseLrcToMap(tlrc);
    
    // 应用全局偏移
    for (auto const& [time, text] : lrcMap) {
        LyricLine line;
        line.timestamp = time + offset;
        line.text = text;
        
        // 尝试匹配翻译 (容差 0.2s)
        auto it = tlrcMap.lower_bound(time - 0.2);
        if (it != tlrcMap.end() && fabs(it->first - time) < 0.3) {
            line.translation = it->second;
        }
        
        system.lines.push_back(line);
    }
    
    return system;
}

void OnTrackChanged(const std::string& songId) {
    std::lock_guard<std::mutex> lock(g_ToastMutex);
    g_PendingSongId = songId;
    g_HasNewSong = true;
    // 注意：不要在这里直接调用 Raylib 绘图函数，因为这是后台线程
}

// v0.1.2: 自定义 Raylib 日志回调 (用于文件重定向)
static FILE* g_LogFile = nullptr;
void CustomRaylibLogCallback(int logLevel, const char* text, va_list args) {
    if (g_LogFile) {
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), text, args);
        fprintf(g_LogFile, "[RAYLIB] %s\n", buffer);
        fflush(g_LogFile);
    }
}

int main(int argc, char* argv[]) {
    // v0.1.2: 立即禁用所有日志，防止启动时输出
    SetTraceLogLevel(LOG_NONE);                // Raylib 日志
    NeteaseDriver::SetGlobalLogging(false);    // SDK 默认静默
    
    // v0.1.2: 解析命令行参数
    bool verboseMode = false;
    bool helpRequested = false;
    std::string logFilePath;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            helpRequested = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verboseMode = true;
        } else if (arg == "--silent" || arg == "-s") {
            verboseMode = false;
        } else if (arg.find("--log=") == 0) {
            logFilePath = arg.substr(6);
            verboseMode = true; // 日志文件模式隐含开启日志
        }
    }
    
    // 如果请求帮助，输出帮助信息并立即退出（不初始化任何资源）
    if (helpRequested) {
        std::cout << "NeteaseHookSDK Monitor v0.1.2\n";
        std::cout << "\nUsage: NeteaseMonitor.exe [options]\n";
        std::cout << "\nOptions:\n";
        std::cout << "  --verbose, -v      Enable verbose logging\n";
        std::cout << "  --silent, -s       Force silent mode (default)\n";
        std::cout << "  --log=<file>       Redirect logs to file\n";
        std::cout << "  --help, -h         Show this help message\n";
        std::cout << "\nKeyboard Shortcuts:\n";
        std::cout << "  Ctrl+I             Install Hook\n";
        std::cout << "  Ctrl+K             Restart Netease Cloud Music\n";
        std::cout << "  Ctrl+R             Refresh install path\n";
        return 0;
    }
    
    // v0.1.2: 实施静默策略
    if (!verboseMode) {
        // [绝对沉默] 重定向 stderr 到 NUL 并关闭逻辑日志
        SetTraceLogLevel(LOG_NONE); // 同时也关闭 Raylib 内部输出
        NeteaseDriver::SetAbsoluteSilence(true);
    } else {
        // 1. 设置正常日志级别
        SetTraceLogLevel(LOG_INFO);
        NeteaseDriver::SetGlobalLogging(true);
        
        // 2. 如果指定了日志文件，进行重定向
        if (!logFilePath.empty()) {
            if (RedirectStderrToFile(logFilePath.c_str())) {
                g_LogFile = stderr;  // 标记已重定向
                SetTraceLogCallback(CustomRaylibLogCallback);
            }
        }
    }

    LOG_DEBUG("窗口初始化中...");

    // 1. 设置窗口标志：无边框 + 透明 + 顶层
    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TRANSPARENT | FLAG_WINDOW_TOPMOST);
    InitWindow(UIConstants::COMPACT_WIDTH, UIConstants::COMPACT_HEIGHT, "NCM Widget v0.1.2");
    
    // v0.1.2: 窗口初始化完成
    SetTargetFPS(60); 

    // 2. 加载着色器 (修复路径搜索逻辑)
    auto LoadSafeShader = [](const char* vs, const char* fs) -> Shader {
        if (!fs) return Shader{0};
        
        // 多路径搜索 (根据工作目录 C/D/E/*:\...\Cloudmusic)
        const std::string candidates[] = {
            std::string(fs),                                           // 原始路径
            std::string("./netease-hook-sdk/resources/shaders/") + fs, // 从 Cloudmusic 目录
            std::string("./resources/shaders/") + fs,                  // 相对于工作目录
            std::string("../resources/shaders/") + fs,                 // 上一级
            std::string("../netease-hook-sdk/resources/shaders/") + fs,// SDK 路径
            std::string("../../netease-hook-sdk/resources/shaders/") + fs
        };
        
        for (const auto& path : candidates) {
            if (FileExists(path.c_str())) {
                Shader s = LoadShader(vs, path.c_str());
                if (s.id != 0) {
                    LOG_INFO("[Shader] Loaded: " << path);
                    return s;
                }
            }
        }
        
        LOG_ERROR("[Shader] Failed to load from resources/shaders: " << fs);
        return Shader{0};
    };

    g_MaskShader = LoadSafeShader(nullptr, "circle_mask.fs");
    g_GlassShader = LoadSafeShader(nullptr, "glass.fs");
    g_AuroraShader = LoadSafeShader(nullptr, "aurora.fs");

    // 获取 Shader Uniform 位置
    int uTimeMask = GetShaderLocation(g_MaskShader, "uTime");
    int uIntensityGlass = GetShaderLocation(g_GlassShader, "uIntensity");
    int uEnergyAurora = GetShaderLocation(g_AuroraShader, "uEnergy");
    int uTimeAurora = GetShaderLocation(g_AuroraShader, "uTime");
    int uColor1Aurora = GetShaderLocation(g_AuroraShader, "uColor1");
    int uColor2Aurora = GetShaderLocation(g_AuroraShader, "uColor2");
    int uResAurora = GetShaderLocation(g_AuroraShader, "uResolution");
    int uRoundAurora = GetShaderLocation(g_AuroraShader, "uRoundness");
    int uResGlass = GetShaderLocation(g_GlassShader, "uResolution");
    int uRoundGlass = GetShaderLocation(g_GlassShader, "uRoundness");

    // === 入场动画状态 ===
    float entranceOffset = 40.0f; // 起始位移 (从下方滑入)
    float entranceAlpha = 0.0f;   // 起始透明度

    // 颜色定义 (Cyan Theme - Back to original per user request)
    Color THEME_PRIMARY = { 0, 255, 200, 255 };    // 青色高亮
    Color THEME_SECONDARY = { 0, 200, 180, 180 };  // 次要文字
    Color THEME_BG = { 10, 20, 25, 180 };          // 深青空背景
    Color THEME_BAR_BG = { 255, 255, 255, 40 };
    Color THEME_RED = { 255, 58, 58, 255 };        // 保留红作为辅助色或Toast

    // 3. 加载中文字体 
    int codepointCount = 95 + (0x9FFF - 0x4E00 + 1);
    int* codepoints = (int*)malloc(codepointCount * sizeof(int));
    for (int i = 0; i < 95; i++) codepoints[i] = 32 + i;
    for (int i = 0; i < (0x9FFF - 0x4E00 + 1); i++) codepoints[95 + i] = 0x4E00 + i;
    
    // 3. 加载中文字体 - 多重后备机制
    Font font = LoadFontEx("C:/Windows/Fonts/simhei.ttf", 20, codepoints, codepointCount);
    if (font.baseSize == 0) font = LoadFontEx("C:/Windows/Fonts/msyh.ttc", 20, codepoints, codepointCount);
    if (font.baseSize == 0) font = LoadFontEx("C:/Windows/Fonts/simsun.ttc", 20, codepoints, codepointCount);
    if (font.baseSize == 0) font = GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    free(codepoints);

    // 辅助绘制函数
    auto DrawUI = [&](const char* text, int x, int y, int size, Color color) {
        float energyPulse = Netease::Visualizer::Instance().GetEnergyPulse();
        
        // 动态呼吸光 (Pulse Glow) - 仅当有音频脉冲时
        if (energyPulse > 0.2f) {
            float glowAlpha = (energyPulse - 0.2f) * 0.4f * (color.a / 255.0f);
            DrawTextEx(font, text, Vector2{(float)x, (float)y}, (float)size, 1.0f, ColorAlpha(THEME_PRIMARY, glowAlpha));
            // 二次发光增强边缘
            DrawTextEx(font, text, Vector2{(float)x, (float)y}, (float)size, 1.0f, ColorAlpha(WHITE, glowAlpha * 0.5f));
        }

        DrawTextEx(font, text, Vector2{(float)x+1.5f, (float)y+1.5f}, (float)size, 1.0f, ColorAlpha(BLACK, 0.4f * (color.a / 255.0f)));
        DrawTextEx(font, text, Vector2{(float)x, (float)y}, (float)size, 1.0f, color);
    };

    auto DrawUICentered = [&](const char* text, int centerX, int centerY, int size, Color color) {
        Vector2 textSize = MeasureTextEx(font, text, (float)size, 1.0f);
        int x = centerX - (int)textSize.x / 2;
        int y = centerY - (int)textSize.y / 2;
        DrawUI(text, x, y, size, color);
    };

    auto DrawUILeft = [&](const char* text, int x, int y, int size, Color color) {
        DrawUI(text, x, y, size, color);
    };
    
    // 格式化时间 mm:ss
    auto FormatTime = [](double seconds) -> std::string {
        if (seconds < 0) return "00:00";
        int m = (int)(seconds / 60);
        int s = (int)(seconds) % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
        return std::string(buf);
    };

    // 3. 连接驱动
    auto& driver = NeteaseDriver::Instance();
    
    // 注册回调 (必须在 Connect 之前或之后均可，只要 Driver 存在)
    driver.SetTrackChangedCallback(OnTrackChanged);
    
    bool connected = driver.Connect(9222);

    // v0.1.2: 初始化音频采集 (WASAPI Loopback)
    Netease::AudioCapture::Instance().Start();
    
    std::string installPath = NeteaseDriver::GetInstallPath();
    bool hookInstalled = false;
    if (!installPath.empty()) {
        hookInstalled = NeteaseDriver::IsHookInstalled();
    }
    
    // 状态计时器（用于非阻塞操作）
    double restartStartTime = 0;
    bool isRestarting = false;

    while (!WindowShouldClose()) {
        double currentTime = GetTime();

        // --- 拖拽逻辑 (无滞后全平滑版 + 惯性) ---
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            g_DragState.isDragging = true;
            g_DragState.offset = GetMousePosition();
            g_Layout.velocity = {0, 0};
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            g_DragState.isDragging = false;
        }
        if (g_DragState.isDragging) {
            Vector2 mousePos = GetMousePosition();
            Vector2 winPos = GetWindowPosition();
            Vector2 delta = { mousePos.x - g_DragState.offset.x, mousePos.y - g_DragState.offset.y };
            
            // 物理速度跟踪
            g_Layout.velocity.x = delta.x * 0.4f + g_Layout.velocity.x * 0.6f;
            g_Layout.velocity.y = delta.y * 0.4f + g_Layout.velocity.y * 0.6f;
            
            SetWindowPosition((int)(winPos.x + delta.x), (int)(winPos.y + delta.y));
        } else {
            // 惯性滑动 (加阻尼 + 严格边界锁定)
            if (Vector2Length(g_Layout.velocity) > 0.05f) {
                Vector2 winPos = GetWindowPosition();
                int monitor = GetCurrentMonitor();
                int screenW = GetMonitorWidth(monitor);
                int screenH = GetMonitorHeight(monitor);
                
                float nextX = winPos.x + g_Layout.velocity.x;
                float nextY = winPos.y + g_Layout.velocity.y;
                
                if (nextX < 0) { nextX = 0; g_Layout.velocity.x *= -0.4f; }
                if (nextX + g_Layout.currentWidth > screenW) { nextX = (float)screenW - g_Layout.currentWidth; g_Layout.velocity.x *= -0.4f; }
                if (nextY < 0) { nextY = 0; g_Layout.velocity.y *= -0.4f; }
                if (nextY + g_Layout.currentHeight > screenH) { nextY = (float)screenH - g_Layout.currentHeight; g_Layout.velocity.y *= -0.4f; }
                
                SetWindowPosition((int)nextX, (int)nextY);
                g_Layout.velocity.x *= 0.94f; 
                g_Layout.velocity.y *= 0.94f;
            }
        }

        // --- 窗口布局物理更新 (Ease-Out 增强) ---
        g_Layout.Update(GetFrameTime());

        // --- 全局快捷键处理 (需按住 Ctrl) ---
        bool isCtrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        // Ctrl + K: 重启
        if (isCtrlDown && IsKeyPressed(KEY_K) && !isRestarting) {
            NeteaseDriver::RestartApplication(installPath);
            isRestarting = true;
            restartStartTime = currentTime;
            connected = false;
            driver.Disconnect();
        }
        
        // Ctrl + I: 安装 Hook
        if (isCtrlDown && IsKeyPressed(KEY_I) && !isRestarting) {
            // 直接调用 InstallHook，让它使用内置的智能搜索逻辑
            // 它会自动搜索：bin/x86, bin/x64, 当前目录, 模块目录
            if (NeteaseDriver::InstallHook("")) {
                hookInstalled = true;
                // 安装后自动重启
                NeteaseDriver::RestartApplication(installPath);
                isRestarting = true;
                restartStartTime = currentTime;
                connected = false;
                driver.Disconnect();
            }
        }
        
        // Ctrl + R: 刷新路径
        if (isCtrlDown && IsKeyPressed(KEY_R) && installPath.empty()) {
            installPath = NeteaseDriver::GetInstallPath();
            if (!installPath.empty()) {
                hookInstalled = NeteaseDriver::IsHookInstalled();
            }
        }

        // --- 非阻塞重启逻辑 ---
        static double nextRetryTime = 0;
        if (isRestarting) {
            if (currentTime - restartStartTime > 2.0 && currentTime > nextRetryTime) {
                connected = driver.Connect(9222);
                if (connected) {
                    isRestarting = false;
                } else {
                    nextRetryTime = currentTime + 3.0;
                }
            }
        } else {
             // 正常运行时 - 状态在下面统一获取
        }
        
        // 获取最新状态
        IPC::NeteaseState state = driver.GetState();
        
        // === FALLBACK: 轮询歌曲变化 ===
        // CDP 回调可能错过一些歌曲变化，所以我们也轮询
        static double lastPollTime = 0;
        static std::string lastKnownSongId;
        if (currentTime - lastPollTime > 1.0) { // 每秒轮询一次
            std::string currentSongId(state.songId);
            if (!currentSongId.empty() && currentSongId != lastKnownSongId) {
                // 触发歌曲加载逻辑 (静默)
                {
                    std::lock_guard<std::mutex> lock(g_ToastMutex);
                    g_PendingSongId = currentSongId;
                    g_HasNewSong = true;
                }
                lastKnownSongId = currentSongId;
            }
            lastPollTime = currentTime;
        }
        
        // === 更新唱片旋转与唱针 ===
        float deltaTime = GetFrameTime();
        g_DiscRotation.Update(state.isPlaying == 1, deltaTime);
        g_Tonearm.Update(state.isPlaying == 1, deltaTime);
        
        // --- v0.1.2: 更新频谱分析 ---
        auto samples = Netease::AudioCapture::Instance().GetSamples(1024);
        auto magnitudes = Netease::FftHelper::Analyze(samples);
        auto bands = Netease::FftHelper::CalculateBands(magnitudes, 32);
        Netease::Visualizer::Instance().Update(bands, deltaTime);

        // --- 更新入场动画 (Ease-Out Snappier) ---
        if (entranceOffset > 0.1f) {
            entranceOffset += (0.0f - entranceOffset) * 0.15f; // 从 0.1 -> 0.15 提升响应速度
            entranceAlpha += (1.0f - entranceAlpha) * 0.15f;
        } else {
            entranceOffset = 0;
            entranceAlpha = 1.0f;
        }

        // --- 内存监控 (每10秒记录一次) ---
        static double lastMemCheckTime = 0;
        if (currentTime - lastMemCheckTime > 10.0) {
            float memMB = Netease::MemoryMonitor::GetProcessMemoryMB();
            LOG_INFO("[MEM] Working Set: " << std::fixed << std::setprecision(2) << memMB << " MB");
            lastMemCheckTime = currentTime;
        }

        // --- 更新 Shader Uniforms ---
        float timeVal = (float)currentTime;
        float energyVal = Netease::Visualizer::Instance().GetEnergyPulse();
        
        if (g_MaskShader.id != 0) {
            float angleVal = g_DiscRotation.angle * DEG2RAD;
            SetShaderValue(g_MaskShader, uTimeMask, &timeVal, SHADER_UNIFORM_FLOAT);
            int uAngleLoc = GetShaderLocation(g_MaskShader, "uAngle");
            SetShaderValue(g_MaskShader, uAngleLoc, &angleVal, SHADER_UNIFORM_FLOAT);
        }
        
        if (g_AuroraShader.id != 0) {
            SetShaderValue(g_AuroraShader, uTimeAurora, &timeVal, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_AuroraShader, uEnergyAurora, &energyVal, SHADER_UNIFORM_FLOAT);
            Vector3 c1 = { THEME_PRIMARY.r/255.0f, THEME_PRIMARY.g/255.0f, THEME_PRIMARY.b/255.0f };
            Vector3 c2 = { THEME_BG.r/255.0f, THEME_BG.g/255.0f, THEME_BG.b/255.0f }; 
            SetShaderValue(g_AuroraShader, uColor1Aurora, &c1, SHADER_UNIFORM_VEC3);
            SetShaderValue(g_AuroraShader, uColor2Aurora, &c2, SHADER_UNIFORM_VEC3);
            
            // 统一使用 CORNER_ROUNDNESS, 与 DrawRectangleRounded 一致
            Vector2 res = { g_Layout.currentWidth, g_Layout.currentHeight };
            float minDim = (g_Layout.currentHeight < g_Layout.currentWidth) ? g_Layout.currentHeight : g_Layout.currentWidth;
            float roundness = UIConstants::CORNER_ROUNDNESS * minDim; // 像素单位
            SetShaderValue(g_AuroraShader, uResAurora, &res, SHADER_UNIFORM_VEC2);
            SetShaderValue(g_AuroraShader, uRoundAurora, &roundness, SHADER_UNIFORM_FLOAT);
        }
    
        if (g_GlassShader.id != 0) {
            float intensity = 0.8f + energyVal * 0.4f;
            if (intensity > 1.0f) intensity = 1.0f;
            SetShaderValue(g_GlassShader, uIntensityGlass, &intensity, SHADER_UNIFORM_FLOAT);
            SetShaderValue(g_GlassShader, GetShaderLocation(g_GlassShader, "uTime"), &timeVal, SHADER_UNIFORM_FLOAT);
            
            // 统一使用 CORNER_ROUNDNESS
            Vector2 res = { g_Layout.currentWidth, g_Layout.currentHeight };
            float minDim = (g_Layout.currentHeight < g_Layout.currentWidth) ? g_Layout.currentHeight : g_Layout.currentWidth;
            float roundness = UIConstants::CORNER_ROUNDNESS * minDim;
            SetShaderValue(g_GlassShader, uResGlass, &res, SHADER_UNIFORM_VEC2);
            SetShaderValue(g_GlassShader, uRoundGlass, &roundness, SHADER_UNIFORM_FLOAT);
        }
    
        // 预计算透明颜色
        Color transPrimary = ColorAlpha(THEME_PRIMARY, entranceAlpha);
        Color transSecondary = ColorAlpha(THEME_SECONDARY, entranceAlpha);
        Color pulseColor = ColorAlpha(THEME_PRIMARY, energyVal * 0.8f);

        BeginDrawing();
        ClearBackground(BLANK);

        // --- 1. 绘制极光背景 (圆角一致性) ---
        if (g_AuroraShader.id != 0) {
            BeginShaderMode(g_AuroraShader);
            DrawRectangleRounded(Rectangle{0, 0, g_Layout.currentWidth, g_Layout.currentHeight}, 
                UIConstants::CORNER_ROUNDNESS, UIConstants::CORNER_SEGMENTS, WHITE);
            EndShaderMode();
        }

        // --- 处理 Toast 触发 + v0.1.0 加载歌曲信息 ---
        if (g_HasNewSong) {
            std::string newId;
            {
                std::lock_guard<std::mutex> lock(g_ToastMutex);
                newId = g_PendingSongId;
                g_HasNewSong = false;
            }
            
            
            // v0.1.0 解析数字ID并加载元数据/歌词
            long long numericId = ParseNumericSongId(newId);
            
            if (numericId != g_SongCache.numericId && numericId > 0) {
                g_SongCache.numericId = numericId;
                g_SongCache.rawId = newId;
                g_SongCache.isLoading = true;
                
                // 立即清空旧状态
                g_SongCache.meta = std::nullopt;
                g_SongCache.lyric = std::nullopt;
                g_SongCache.lyrics.Clear();
                g_SongCache.coverTexture = {0}; 
                
                auto startTime = std::chrono::high_resolution_clock::now();
                
                // 加载元数据
                auto t1 = std::chrono::high_resolution_clock::now();
                g_SongCache.meta = Netease::API::GetSongDetail(numericId);
                auto t2 = std::chrono::high_resolution_clock::now();
                auto metaDuration = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
                
                // 加载歌词 (包含翻译和罗马音)
                auto t3 = std::chrono::high_resolution_clock::now();
                g_SongCache.lyric = Netease::API::GetLyric(numericId);
                if (g_SongCache.lyric) {
                    g_SongCache.lyrics = ParseLyrics(
                        g_SongCache.lyric->lrc, 
                        g_SongCache.lyric->tlyric + "\n" + g_SongCache.lyric->romalrc
                    );
                } else {
                    g_SongCache.lyrics.Clear();
                }
                auto t4 = std::chrono::high_resolution_clock::now();
                auto lyricDuration = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();
                
                // 加载封面
                auto t5 = std::chrono::high_resolution_clock::now();
                if (g_SongCache.meta && !g_SongCache.meta->albumPicUrl.empty()) {
                    g_SongCache.coverTexture = Netease::AlbumCover::LoadFromUrl(
                        g_SongCache.meta->albumPicUrl, 
                        numericId
                    );
                }
                auto t6 = std::chrono::high_resolution_clock::now();
                auto coverDuration = std::chrono::duration_cast<std::chrono::milliseconds>(t6 - t5).count();
                auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(t6 - startTime).count();
                
                LOG_INFO("[PERF] 加载耗时: Metadata=" << metaDuration 
                          << "ms | Lyric=" << lyricDuration 
                          << "ms | Cover=" << coverDuration 
                          << "ms | Total=" << totalDuration << "ms");
                
                g_SongCache.isLoading = false;
                
                // 更新Toast显示歌曲名
                if (g_SongCache.meta) {
                    g_Toast.message = "♪ " + g_SongCache.meta->title;
                } else {
                    g_Toast.message = "ID: " + std::to_string(numericId);
                }
            } else {
                g_Toast.message = "Switched to: " + newId;
            }
            
            g_Toast.alpha = 1.0f;
            g_Toast.startTime = currentTime;
        }
        
        // v0.1.0 更新歌词系统状态
        g_SongCache.lyrics.UpdateIndex(state.currentProgress);
        
        // 更新 Toast 动画 (Fade out after 3s)
        if (g_Toast.alpha > 0) {
            if (currentTime - g_Toast.startTime > 3.0) {
                g_Toast.alpha -= 0.02f; // 淡出
            }
            if (g_Toast.alpha < 0) g_Toast.alpha = 0;
        }

        // 使用入场动画 Alpha 和 音频能量脉冲
        float energyPulse = Netease::Visualizer::Instance().GetEnergyPulse();
        float pulseAlpha = 0.85f + energyPulse * 0.15f; // 背景透明度随能量波动
        
        Color transparentPrimary = ColorAlpha(THEME_PRIMARY, entranceAlpha);
        Color transparentSecondary = ColorAlpha(THEME_SECONDARY, entranceAlpha);
        Color transparentGold = ColorAlpha(GOLD, entranceAlpha);
        Color transparentWhite = ColorAlpha(WHITE, entranceAlpha);
        Color glassBg = THEME_BG;
        glassBg.a = (unsigned char)(glassBg.a * entranceAlpha * pulseAlpha);

        // --- 2. 绘制 Glass 2.0 背景 (软件圆角, shader 作为备选) ---
        DrawRectangleRounded(Rectangle{0, 0, g_Layout.currentWidth, g_Layout.currentHeight}, 
            UIConstants::CORNER_ROUNDNESS, UIConstants::CORNER_SEGMENTS, glassBg);
        
        // v0.1.0 绘制声学风暴可视化 (覆盖全屏背景，放在玻璃背景之上，文字之下)
        // 回归原本青色主题
        Netease::Visualizer::Instance().Draw((int)g_Layout.currentWidth, (int)g_Layout.currentHeight, THEME_PRIMARY);
        
        DrawRectangleRoundedLines(Rectangle{0, 0, g_Layout.currentWidth, g_Layout.currentHeight}, 
            UIConstants::CORNER_ROUNDNESS, UIConstants::CORNER_SEGMENTS, ColorAlpha(THEME_PRIMARY, 0.15f * entranceAlpha));


        if (isRestarting) {
                DrawUI("⏳ 正在重启网易云...", 110, 50, 20, transparentPrimary);
                DrawUI("请稍候...", 160, 80, 16, ColorAlpha(LIGHTGRAY, entranceAlpha));
        }
        else if (!connected) {
            DrawUI("⚠ 未连接网易云音乐", 20, 20, 24, ORANGE);
            
            if (installPath.empty()) {
                DrawUI("未找到安装路径 ! (按 R 重试)", 20, 60, 18, LIGHTGRAY);
            } else {
                if (hookInstalled) {
                    DrawUI("Hook 已就绪", 20, 60, 18, THEME_PRIMARY);
                    DrawUI("请重启网易云 (按 K 重启)", 20, 90, 18, SKYBLUE);
                } else {
                    DrawUI("Hook 未安装 (按 I 安装)", 20, 60, 18, RED);
                }
            }
        } else {
            // 已连接 - 显示播放信息 (Netease Vinyl Mode 精确布局)
            // 布局参数: 左 48% 为唱片区, 右 52% 为歌词区
            float vinylZoneWidth = g_Layout.currentWidth * UIConstants::VINYL_ZONE_RATIO;
            float displayZoneStart = vinylZoneWidth;
            float displayZoneWidth = g_Layout.currentWidth - vinylZoneWidth;
            
            float discRadius = (g_Layout.state == STATE_EXPANDED) ? g_Layout.currentHeight * 0.28f : 60.0f;
            float discX = (g_Layout.state == STATE_EXPANDED) ? vinylZoneWidth * 0.5f : 80.0f;
            float discY = (g_Layout.state == STATE_EXPANDED) ? g_Layout.currentHeight * 0.48f : 95.0f + entranceOffset;
            Vector2 discCenter = { discX, discY };
            
            float rightX = (g_Layout.state == STATE_EXPANDED) ? displayZoneStart + 20.0f : 185.0f;                
            
            // 歌词区布局: 居中于右侧 52% 区域, Y 始于 45% (避开元数据区域)
            int lyricZoneY = (g_Layout.state == STATE_EXPANDED) ? (int)(g_Layout.currentHeight * 0.45f) : 160 + (int)entranceOffset;
            int lineHeight = (g_Layout.state == STATE_EXPANDED) ? 42 : 30;         
            int lyricCenterX = (g_Layout.state == STATE_EXPANDED) ? (int)(displayZoneStart + displayZoneWidth * 0.5f) : (int)(g_Layout.currentWidth * 0.5f);
            
            // 1. 绘制动态漫反射阴影
            float currentEnergy = Netease::Visualizer::Instance().GetEnergyPulse();
            float pulseScale = 1.0f + currentEnergy * 0.06f; 
            float currentDiscRadius = discRadius * pulseScale;

            DrawCircleGradient((int)discCenter.x, (int)discCenter.y + 6, currentDiscRadius + 15, ColorAlpha(BLACK, 0.25f), BLANK);
            
            // 1. 绘制黑胶底座
            DrawCircleV(discCenter, currentDiscRadius + 4, ColorAlpha(BLACK, 0.98f));
            for (int r = 20; r < (int)currentDiscRadius; r += 8) {
                DrawCircleLinesV(discCenter, (float)r, ColorAlpha(WHITE, 0.04f));
            }
            DrawCircleLinesV(discCenter, currentDiscRadius + 4, ColorAlpha(WHITE, 0.15f));

            // 2. 绘制圆形封面
            if (g_SongCache.coverTexture.id != 0) {
                // 点击封面切换状态
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointCircle(GetMousePosition(), discCenter, currentDiscRadius)) {
                    if (g_Layout.state == STATE_COMPACT) {
                        g_Layout.state = STATE_EXPANDED;
                        g_Layout.targetWidth = (float)UIConstants::EXPANDED_WIDTH;
                        g_Layout.targetHeight = (float)UIConstants::EXPANDED_HEIGHT;
                    } else {
                        g_Layout.state = STATE_COMPACT;
                        g_Layout.targetWidth = (float)UIConstants::COMPACT_WIDTH;
                        g_Layout.targetHeight = (float)UIConstants::COMPACT_HEIGHT;
                    }
                }

                Rectangle src = { 0, 0, (float)g_SongCache.coverTexture.width, (float)g_SongCache.coverTexture.height };
                Rectangle dest = { discCenter.x, discCenter.y, (currentDiscRadius - 1) * 2, (currentDiscRadius - 1) * 2 };
                Vector2 origin = { currentDiscRadius - 1, currentDiscRadius - 1 };
                
                if (g_MaskShader.id != 0) BeginShaderMode(g_MaskShader);
                DrawTexturePro(g_SongCache.coverTexture, src, dest, origin, g_DiscRotation.angle, WHITE);
                if (g_MaskShader.id != 0) EndShaderMode();
                
                // 3. 唱针 (Tonearm) - Netease Vinyl Mode 风格
                // 唱臂锚点在唱片区右上方 (基于 Netease handle SVG)
                float armBaseX = (g_Layout.state == STATE_EXPANDED) ? vinylZoneWidth * 0.85f : discCenter.x + discRadius * 0.6f;
                float armBaseY = 25.0f;
                Vector2 armPivot = { armBaseX, armBaseY }; 
                
                float armAngleRad = DEG2RAD * (115 + g_Tonearm.angle * 0.5f);
                float armLen1 = (g_Layout.state == STATE_EXPANDED) ? 100.0f : discRadius * 0.9f;
                float armLen2 = (g_Layout.state == STATE_EXPANDED) ? 80.0f : discRadius * 0.6f;
                
                Vector2 armJoint = {
                    armPivot.x + cosf(armAngleRad) * armLen1,
                    armPivot.y + sinf(armAngleRad) * armLen1
                };
                Vector2 armEnd = {
                    armJoint.x + cosf(armAngleRad + 0.3f) * armLen2,
                    armJoint.y + sinf(armAngleRad + 0.3f) * armLen2
                };
                
                // 画线 (使用白色，分段感)
                DrawLineEx(armPivot, armJoint, 4.0f, ColorAlpha(WHITE, 0.9f));
                DrawLineEx(armJoint, armEnd, 3.0f, ColorAlpha(WHITE, 0.8f));
                DrawCircleV(armPivot, 8, ColorAlpha(LIGHTGRAY, 0.9f)); 
                DrawCircleV(armJoint, 4, ColorAlpha(WHITE, 0.9f));
                DrawCircleV(armEnd, 5, WHITE); // 针头
                
            } else {
                DrawCircleV(discCenter, discRadius - 5, ColorAlpha(GRAY, 0.2f));
                DrawUI("♪", (int)discCenter.x - 10, (int)discCenter.y - 15, 30, ColorAlpha(WHITE, 0.3f));
            }

            // --- 右侧内容区域 (Netease Vinyl Mode 布局) ---
            int metaStartY = (g_Layout.state == STATE_EXPANDED) ? 45 : (int)(g_Layout.currentHeight * 0.15f);
            if (state.isPlaying) {
                DrawUILeft("NOW PLAYING", (int)rightX, metaStartY, 12, THEME_PRIMARY);
            } else {
                DrawUILeft("PAUSED", (int)rightX, metaStartY, 12, GOLD);
            }

            if (g_SongCache.meta) {
                DrawUILeft(g_SongCache.meta->title.c_str(), (int)rightX, metaStartY + 22, 22, WHITE);
                
                std::string artistStr;
                for (size_t i = 0; i < g_SongCache.meta->artists.size(); i++) {
                    if (i > 0) artistStr += " / ";
                    artistStr += g_SongCache.meta->artists[i];
                }
                if (!artistStr.empty()) {
                    DrawUILeft(artistStr.c_str(), (int)rightX, metaStartY + 55, 14, THEME_SECONDARY);
                }
            } else {
                DrawUILeft(TextFormat("ID: %s", state.songId), (int)rightX, metaStartY + 25, 16, WHITE);
            }
            
            // 为歌词增加渐变遮罩边缘效果 (只在右侧显示区域)
            BeginBlendMode(BLEND_ADDITIVE);
            int fadeStartX = (g_Layout.state == STATE_EXPANDED) ? (int)displayZoneStart : 0;
            int fadeWidth = (g_Layout.state == STATE_EXPANDED) ? (int)displayZoneWidth : (int)g_Layout.currentWidth;
            DrawRectangleGradientV(fadeStartX, lyricZoneY - 60, fadeWidth, 30, BLANK, ColorAlpha(THEME_BG, 0.15f));
            DrawRectangleGradientV(fadeStartX, lyricZoneY + 120, fadeWidth, 30, ColorAlpha(THEME_BG, 0.15f), BLANK);
            EndBlendMode();
            
            float targetScroll = 0;
            if (g_SongCache.lyrics.currentIndex >= 0) {
                targetScroll = g_SongCache.lyrics.currentIndex * (float)lineHeight;
            }
            g_SongCache.lyrics.scrollOffset += (targetScroll - g_SongCache.lyrics.scrollOffset) * 0.1f;
            
            if (g_SongCache.isLoading) {
                DrawUICentered("正在获取歌词...", lyricCenterX, lyricZoneY, 15, ColorAlpha(THEME_PRIMARY, 0.6f));
            } else if (g_SongCache.lyrics.lines.empty()) {
                DrawUICentered("暂无歌词", lyricCenterX, lyricZoneY, 16, ColorAlpha(WHITE, 0.4f));
            } else {
                int linesToShow = (g_Layout.state == STATE_EXPANDED) ? 7 : 5;
                int half = linesToShow / 2;
                for (int i = -half; i <= half; i++) { 
                    int lineIdx = g_SongCache.lyrics.currentIndex + i;
                    if (lineIdx < 0 || lineIdx >= (int)g_SongCache.lyrics.lines.size()) continue;
                    
                    const auto& line = g_SongCache.lyrics.lines[lineIdx];
                    float drawY = (float)lyricZoneY + (i * lineHeight) - (targetScroll - g_SongCache.lyrics.scrollOffset);
                    float distFromCenter = fabs(drawY - lyricZoneY);
                    
                    // 动态调整透明度：边缘更淡
                    float opacity = 1.0f - powf(distFromCenter / (lineHeight * 2.8f), 1.3f);
                    if (opacity < 0) opacity = 0;
                    
                    Color textColor = (i == 0) ? WHITE : ColorAlpha(WHITE, 0.5f * opacity);
                    int fontSize = (i == 0) ? 17 : 14;
                    
                    DrawUICentered(line.text.c_str(), lyricCenterX, (int)drawY, fontSize, textColor);
                if (i == 0 && !line.translation.empty()) {
                    DrawUICentered(line.translation.c_str(), lyricCenterX, (int)drawY + 20, 11, ColorAlpha(textColor, 0.8f));
                }

                // --- KTV 逐字进度显示 ---
                if (i == 0) {
                    double nextTimestamp = (lineIdx + 1 < (int)g_SongCache.lyrics.lines.size()) 
                                          ? g_SongCache.lyrics.lines[lineIdx + 1].timestamp 
                                          : state.totalDuration;
                                          
                    double lineDuration = nextTimestamp - line.timestamp;
                    if (lineDuration > 0) {
                        double lineProgress = (state.currentProgress - line.timestamp) / lineDuration;
                        lineProgress = fmin(fmax(lineProgress, 0.0), 1.0);
                        
                        Vector2 textSize = MeasureTextEx(font, line.text.c_str(), (float)fontSize, 1.0f);
                        int textX = lyricCenterX - (int)textSize.x / 2;
                        int textY = (int)drawY - (int)textSize.y / 2;
                            int fillWidth = (int)(textSize.x * lineProgress);
                            
                            BeginScissorMode(textX, textY, fillWidth, (int)textSize.y + 2);
                            DrawUI(line.text.c_str(), textX, textY, fontSize, THEME_PRIMARY);
                            EndScissorMode();
                        }
                    }
                }
            }
            
            // --- 进度条 (精细化网易红进度条) ---
            float barPadding = (g_Layout.state == STATE_EXPANDED) ? g_Layout.currentWidth * 0.1f : 20.0f;
            float barY = g_Layout.currentHeight - (g_Layout.state == STATE_EXPANDED ? 40.0f : 20.0f);
            float barWidth = g_Layout.currentWidth - barPadding * 2;
            double duration = (state.totalDuration > 0.1) ? state.totalDuration : 1.0; // 防止除零
            float progress = (float)(state.currentProgress / duration);
            progress = fmaxf(0.0f, fminf(progress, 1.0f)); // 严格限制 [0, 1]

            // 背景轨道
            DrawRectangleRounded(Rectangle{barPadding, barY, barWidth, 4}, 0.5f, 10, THEME_BAR_BG);
            // 播放进度 (回归原本主题色)
            DrawRectangleRounded(Rectangle{barPadding, barY, barWidth * progress, 4}, 0.5f, 10, THEME_PRIMARY);
            // 进度滑块点
            float knobX = barPadding + barWidth * progress;
            DrawCircleV(Vector2{knobX, barY + 2}, 4, WHITE);
            DrawCircleV(Vector2{knobX, barY + 2}, 2, THEME_PRIMARY);
            
            std::string timeStr = FormatTime(state.currentProgress) + " / " + FormatTime(state.totalDuration);
            DrawTextEx(font, timeStr.c_str(), Vector2{barPadding, barY - 15}, 11, 1.0f, THEME_SECONDARY);
            
            // --- 3. 响应式频谱装饰 ---
            float specX = (g_Layout.state == STATE_EXPANDED) ? rightX + 85 : g_Layout.currentWidth - 80;
            float specY = (g_Layout.state == STATE_EXPANDED) ? (g_Layout.currentHeight * 0.15f + 4) : 20;

            for (int i=0; i<6; i++) {
                float h = 4 + sinf((float)currentTime * 12 + i * 0.7f) * 6;
                if (!state.isPlaying) h = 2;
                DrawRectangleRounded(Rectangle{specX + i*6, specY - h/2, 3, h}, 0.5f, 10, ColorAlpha(THEME_PRIMARY, 0.6f));
            }
        }
        
        // --- 绘制 Toast ---
        if (g_Toast.alpha > 0.01f) {
            Color toastBg = { 50, 50, 50, (unsigned char)(200 * g_Toast.alpha) };
            Color toastText = { 255, 255, 255, (unsigned char)(255 * g_Toast.alpha) };
            
            int textWidth = (int)MeasureTextEx(font, g_Toast.message.c_str(), 18, 1).x;
            int toastW = textWidth + 40;
            int toastX = ((int)g_Layout.currentWidth - toastW) / 2;
            int toastY = (int)g_Layout.currentHeight - 80;  // 底部显示 (避开进度条)
            
            DrawRectangleRounded(Rectangle{(float)toastX, (float)toastY, (float)toastW, 28}, 0.5f, 10, toastBg);
            DrawTextEx(font, g_Toast.message.c_str(), Vector2{(float)(toastX + 20), (float)(toastY + 5)}, 18, 1, toastText);
        }

        // --- DEBUG: 内存使用显示 ---
        #ifdef _DEBUG
        static float displayMemMB = 0;
        static double lastMemUpdateTime = 0;
        if (currentTime - lastMemUpdateTime > 1.0) { // 每秒更新显示
            displayMemMB = Netease::MemoryMonitor::GetProcessMemoryMB();
            lastMemUpdateTime = currentTime;
        }
        DrawText(TextFormat("MEM: %.1f MB", displayMemMB), 10, 10, 14, YELLOW);
        DrawText(TextFormat("FPS: %d", GetFPS()), 10, 28, 14, LIME);
        #endif

        EndDrawing();
    }
    
    // === Cleanup: 正确释放所有资源 (VRAM Leak Prevention) ===
    Netease::AudioCapture::Instance().Stop();
    Netease::AlbumCover::ClearTextureCache();
    
    // 卸载可能残留的封面纹理
    if (g_SongCache.coverTexture.id != 0) UnloadTexture(g_SongCache.coverTexture);
    
    // 释放所有 Shader
    if (g_MaskShader.id != 0) UnloadShader(g_MaskShader);
    if (g_GlassShader.id != 0) UnloadShader(g_GlassShader);
    if (g_AuroraShader.id != 0) UnloadShader(g_AuroraShader);
    
    // 释放字体
    UnloadFont(font);
    
    CloseWindow();
    return 0;
}
