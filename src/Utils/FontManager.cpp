#include "Utils/FontManager.h"
#include "SimpleLog.h"  // 使用项目标准日志系统
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace Netease {

// ==================== FontLoadResult ====================

std::string FontLoadResult::GetStatusString() const {
    std::ostringstream ss;
    ss << (success ? "✓" : "✗") << " "
       << font_path << " ("
       << glyphs_loaded << "/" << codepoints_requested
       << " = " << static_cast<int>(coverage * 100) << "%)";
    return ss.str();
}

// ==================== FontManager ====================

FontManager::FontManager() {
    InitializeDefaultFontPaths();
}

FontManager::~FontManager() {
    if (dynamic_ready_ && dynamic_font_.texture.id != 0) {
        UnloadFont(dynamic_font_);
    }
    if (base_ready_ && base_font_.texture.id != 0) {
        UnloadFont(base_font_);
    }
}

void FontManager::InitializeDefaultFontPaths() {
    // 优先 TTF，兼容性最好
    font_paths_ = {
        "C:/Windows/Fonts/simhei.ttf",     // 黑体 (推荐)
        "C:/Windows/Fonts/msyh.ttc",       // 微软雅黑 (有 Raylib Bug)
        "C:/Windows/Fonts/simsun.ttc",     // 宋体兜底
        "C:/Windows/Fonts/kaiu.ttf",       // 楷体
        "C:/Windows/Fonts/arial.ttf"       // 英文最终兜底
    };
}

bool FontManager::Initialize(FontQuality quality) {
    // 1. 确定基础字符集
    std::set<int> baseSet;
    
    // ASCII (32-126)
    for (int i = 32; i <= 126; i++) {
        baseSet.insert(i);
    }
    
    // CJK 标点 (0x3000-0x303F)
    if (quality >= FontQuality::MEDIUM) {
        for (int i = 0x3000; i <= 0x303F; i++) {
            baseSet.insert(i);
        }
    }
    
    // --- 添加UI硬编码字符串 (解决启动界面乱码) ---
    const std::vector<std::string> uiStrings = {
        "⏳ 正在重启网易云...",
        "请稍候...",
        "⚠ 未连接网易云音乐",
        "未找到安装路径 ! (啃臭 R 重试)",
        "Hook 已就绪",
        "请重启网易云 (啃臭 K 重启)",
        "Hook 未安装 (啃臭 I 安装)"
    };
    
    for (const auto& str : uiStrings) {
        auto uiCodepoints = TextToCodepoints(str.c_str());
        for (int cp : uiCodepoints) {
            baseSet.insert(cp);
        }
    }
    
    std::vector<int> codepoints(baseSet.begin(), baseSet.end());
    
    if (verbose_logging_) {
        LOG_INFO("[Font] 基础字体加载 " << codepoints.size() << " 个码点 (ASCII+标点+UI字符集)");
    }
    
    // 2. 尝试加载字体
    auto result = LoadFontWithValidation(codepoints, base_font_size_, "基础");
    
    if (result.success) {
        // base_font_ 已在 LoadFontWithValidation 中正确设置，无需再赋值
        base_ready_ = true;
        LOG_INFO("[Font] " << result.GetStatusString());
        return true;
    }
    
    // 降级到默认字体
    LOG_ERROR("[Font] 所有字体失败，使用 Raylib 默认字体");
    base_font_ = GetFontDefault();
    base_ready_ = true;
    return false;
}

FontLoadResult FontManager::UpdateDynamic(
    const std::string& title,
    const std::string& artist,
    const std::vector<std::string>& lyrics)
{
    LOG_INFO("[Font] 动态字体更新: title=" << title.size() 
             << "B, artist=" << artist.size() 
             << "B, lyrics=" << lyrics.size() << " 行");
    
    // 1. 卸载旧字体
    if (dynamic_ready_ && dynamic_font_.texture.id != 0) {
        UnloadFont(dynamic_font_);
        dynamic_font_ = Font{};
        dynamic_ready_ = false;
    }
    
    // 2. 收集码点
    std::vector<std::string> all_texts = lyrics;
    all_texts.push_back(title);
    all_texts.push_back(artist);
    
    std::vector<int> codepoints = CollectCodepoints(all_texts);
    
    LOG_INFO("[Font] 码点收集完成: " << codepoints.size() << " 个唯一码点");
    
    // 3. 加载字体
    auto result = LoadFontWithValidation(codepoints, dynamic_font_size_, "动态");
    
    if (result.IsHealthy()) {
        dynamic_ready_ = true;
        dynamic_coverage_ = result.coverage;
        LOG_INFO("[Font] " << result.GetStatusString());
    } else {
        LOG_WARN("[Font] 动态字体加载失败，将使用基础字体");
    }
    
    return result;
}

FontLoadResult FontManager::LoadFontWithValidation(
    const std::vector<int>& codepoints,
    int size,
    const char* context)
{
    FontLoadResult result;
    result.codepoints_requested = static_cast<int>(codepoints.size());
    
    for (const auto& path : font_paths_) {
        // 检查文件是否存在
        if (!FileExists(path.c_str())) {
            continue;
        }
        
        // 加载字体
        Font font = LoadFontEx(path.c_str(), size, 
                              const_cast<int*>(codepoints.data()), 
                              static_cast<int>(codepoints.size()));
        
        // 验证加载结果
        if (!ValidateFont(font, result.codepoints_requested, path.c_str())) {
            if (font.texture.id != 0) {
                UnloadFont(font);
            }
            continue;
        }
        
        // 成功！
        result.success = true;
        result.glyphs_loaded = font.glyphCount;
        result.font_path = path;
        result.coverage = static_cast<float>(font.glyphCount) / result.codepoints_requested;
        
        // 设置字体（根据 context 判断是基础还是动态）
        if (std::string(context) == "基础") {
            base_font_ = font;
        } else {
            dynamic_font_ = font;
        }
        
        SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        return result;
    }
    
    result.success = false;
    result.font_path = "NONE";
    return result;
}

bool FontManager::ValidateFont(const Font& font, int requested_count, const char* path) {
    // 1. 基础验证
    if (font.baseSize <= 0 || font.glyphCount <= 0) {
        LOG_WARN("[Font] " << path << " 加载失败: 无效字体");
        return false;
    }
    
    // 2. 检测 Raylib Bug (TTC 文件返回恰好 224 glyphs)
    if (requested_count > 200 && font.glyphCount == 224) {
        LOG_WARN("[Font] " << path << " 检测到 Raylib Bug: "
                 << "请求 " << requested_count << " 但返回 224 (默认字符集 fallback)");
        return false;
    }
    
    // 3. 覆盖率检查 (至少 70%)
    float coverage = static_cast<float>(font.glyphCount) / requested_count;
    if (coverage < 0.7f) {
        LOG_WARN("[Font] " << path << " 覆盖率不足: " 
                 << font.glyphCount << "/" << requested_count 
                 << " = " << static_cast<int>(coverage * 100) << "%");
        return false;
    }
    
    return true;
}

std::vector<int> FontManager::CollectCodepoints(const std::vector<std::string>& texts) {
    std::set<int> uniqueSet;
    
    // 1. 基础字符集 (ASCII + CJK 标点)
    for (int i = 32; i <= 126; i++) uniqueSet.insert(i);
    for (int i = 0x3000; i <= 0x303F; i++) uniqueSet.insert(i);
    int baseSize = static_cast<int>(uniqueSet.size());
    
    // 2. 从文本提取（带详细日志）
    for (const auto& text : texts) {
        if (text.empty()) continue;
        
        int before = static_cast<int>(uniqueSet.size());
        auto cps = TextToCodepoints(text.c_str());
        uniqueSet.insert(cps.begin(), cps.end());
        int after = static_cast<int>(uniqueSet.size());
        
        // 详细日志：匹配 main.cpp 格式
        if (verbose_logging_) {
            LOG_INFO("[Font] 手动提取: cp_total=" << cps.size() 
                     << ", cp_new=" << (after - before) 
                     << " | text=\"" << text.substr(0, std::min(12, (int)text.size())) << "...\"");
        }
    }
    
    if (verbose_logging_) {
        LOG_INFO("[Font] 码点收集: 基础=" << baseSize 
                 << ", 歌曲新增=" << (uniqueSet.size() - baseSize) 
                 << ", 总计=" << uniqueSet.size());
    }
    
    return std::vector<int>(uniqueSet.begin(), uniqueSet.end());
}

std::vector<int> FontManager::TextToCodepoints(const char* text) {
    if (!text || !text[0]) return {};
    
    std::vector<int> result;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    
    while (*p) {
        int codepoint = 0;
        int len = 0;
        
        // UTF-8 解码
        if ((*p & 0x80) == 0) {
            codepoint = *p;
            len = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = ((*p & 0x1F) << 6) | (*(p + 1) & 0x3F);
            len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = ((*p & 0x0F) << 12) | ((*(p + 1) & 0x3F) << 6) | (*(p + 2) & 0x3F);
            len = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = ((*p & 0x07) << 18) | ((*(p + 1) & 0x3F) << 12) 
                       | ((*(p + 2) & 0x3F) << 6) | (*(p + 3) & 0x3F);
            len = 4;
        } else {
            p++;
            continue;
        }
        
        // 验证后续字节
        if (len > 1) {
            bool valid = true;
            for (int i = 1; i < len; i++) {
                if (*(p + i) == '\0' || (*(p + i) & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                p++;
                continue;
            }
        }
        
        // 过滤 BMP 范围
        if (codepoint >= 32 && codepoint <= 0xFFFF) {
            result.push_back(codepoint);
        }
        
        p += len;
    }
    
    return result;
}

const Font& FontManager::GetActiveFont() const {
    return dynamic_ready_ ? dynamic_font_ : base_font_;
}

void FontManager::DrawTextSafe(const char* text, Vector2 position, 
                               float fontSize, float spacing, Color tint) {
    auto codepoints = TextToCodepoints(text);
    if (codepoints.empty()) return;
    
    // DEBUG: 首次渲染中文时记录（匹配 main.cpp）
    static bool firstChinese = true;
    if (verbose_logging_ && firstChinese && !codepoints.empty() && codepoints[0] > 0x4E00) {
        const Font& font = GetActiveFont();
        int glyphIndex = GetGlyphIndex(font, codepoints[0]);
        LOG_INFO("[Render] FirstCJK: cp_count=" << codepoints.size() 
                 << ", first_cp=0x" << std::hex << codepoints[0] << std::dec
                 << ", glyph_idx=" << glyphIndex
                 << ", font_glyphs=" << font.glyphCount
                 << ", text=\"" << std::string(text).substr(0, std::min(15, (int)std::strlen(text))) << "...\"");
        firstChinese = false;
    }
    
    const Font& font = GetActiveFont();
    DrawTextCodepoints(font, codepoints.data(), static_cast<int>(codepoints.size()), 
                      position, fontSize, spacing, tint);
}

Vector2 FontManager::MeasureTextSafe(const char* text, float fontSize, float spacing) {
    auto codepoints = TextToCodepoints(text);
    if (codepoints.empty()) return Vector2{0, 0};
    
    const Font& font = GetActiveFont();
    return MeasureTextEx(font, text, fontSize, spacing);
}

bool FontManager::IsHealthy() const {
    return base_ready_ && (dynamic_ready_ ? dynamic_coverage_ > 0.7f : true);
}

void FontManager::SetFontPaths(const std::vector<std::string>& paths) {
    font_paths_ = paths;
}

bool FontManager::LoadConfig(const std::string& config_path) {
    // TODO: Phase 2 - 实现 JSON 配置加载
    LOG_WARN("[FontMgr] LoadConfig 尚未实现，请等待 Phase 2");
    return false;
}

} // namespace Netease
