#pragma once

#include <raylib.h>
#include <string>
#include <vector>
#include <set>
#include <memory>

namespace Netease {

/**
 * 字体质量等级
 */
enum class FontQuality {
    LOW,      // 仅 ASCII (95 chars)
    MEDIUM,   // ASCII + 常用标点 (159 chars)
    HIGH,     // 动态加载歌曲内容 (200-600 chars)
    ULTRA     // 预加载 6000+ 常用汉字 (实验性)
};

/**
 * 字体加载结果
 */
struct FontLoadResult {
    bool success = false;
    int codepoints_requested = 0;
    int glyphs_loaded = 0;
    std::string font_path;
    float coverage = 0.0f;
    
    /**
     * 健康检查：覆盖率 > 70% 且非 Raylib Bug (224 glyphs)
     */
    bool IsHealthy() const {
        return success && 
               coverage > 0.7f && 
               glyphs_loaded != 224;
    }
    
    std::string GetStatusString() const;
};

/**
 * 字体管理器 - 集中管理所有字体加载/渲染逻辑
 * 
 * 设计目标：
 * - 单一职责：仅负责字体管理
 * - 可测试性：所有方法可通过 Mock 测试
 * - 可配置性：字体路径可动态配置
 * - 健壮性：自动检测并绕过 Raylib TTC Bug
 * 
 * 使用示例：
 *   FontManager mgr;
 *   mgr.Initialize(FontQuality::HIGH);
 *   mgr.UpdateDynamic(title, artist, lyrics);
 *   mgr.DrawText("测试", pos, 24, WHITE);
 */
class FontManager {
public:
    FontManager();
    ~FontManager();
    
    // 禁用拷贝/移动（RAII 管理 Font 资源）
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    FontManager(FontManager&&) = delete;
    FontManager& operator=(FontManager&&) = delete;
    
    /**
     * 初始化基础字体（应用启动时调用一次）
     * @param quality 字体质量等级
     * @return 是否成功加载基础字体
     */
    bool Initialize(FontQuality quality = FontQuality::MEDIUM);
    
    /**
     * 更新动态字体（切歌时调用）
     * @param title 歌曲标题
     * @param artist 歌手名
     * @param lyrics 歌词行列表
     * @return 加载结果详情
     */
    FontLoadResult UpdateDynamic(
        const std::string& title,
        const std::string& artist,
        const std::vector<std::string>& lyrics
    );
    
    /**
     * 获取当前激活字体（动态字体优先，否则返回基础字体）
     */
    const Font& GetActiveFont() const;
    
    /**
     * UTF-8 安全的文本渲染（绕过 Raylib LoadCodepoints Bug）
     */
    void DrawTextSafe(const char* text, Vector2 position, float fontSize, float spacing, Color tint);
    
    /**
     * UTF-8 安全的文本测量
     */
    Vector2 MeasureTextSafe(const char* text, float fontSize, float spacing);
    
    /**
     * 健康检查
     */
    bool IsHealthy() const;
    float GetDynamicCoverage() const { return dynamic_coverage_; }
    
    /**
     * 调试控制
     */
    void SetVerboseLogging(bool enabled) { verbose_logging_ = enabled; }
    bool IsVerboseLogging() const { return verbose_logging_; }
    
    /**
     * 配置管理
     */
    void SetFontPaths(const std::vector<std::string>& paths);
    void SetPreferTTF(bool prefer) { prefer_ttf_ = prefer; }
    
    /**
     * 从 JSON 配置文件加载设置
     */
    bool LoadConfig(const std::string& config_path);

private:
    // 核心字体资源
    Font base_font_;
    Font dynamic_font_;
    bool base_ready_ = false;
    bool dynamic_ready_ = false;
    float dynamic_coverage_ = 0.0f;
    
    // 配置参数
    std::vector<std::string> font_paths_;
    bool prefer_ttf_ = true;
    bool verbose_logging_ = false;  // 详细日志模式
    int base_font_size_ = 24;
    int dynamic_font_size_ = 32;
    
    // 内部辅助方法
    FontLoadResult LoadFontWithValidation(
        const std::vector<int>& codepoints, 
        int size,
        const char* context
    );
    
    std::vector<int> CollectCodepoints(const std::vector<std::string>& texts);
    std::vector<int> TextToCodepoints(const char* text);
    
    bool ValidateFont(const Font& font, int requested_count, const char* path);
    void InitializeDefaultFontPaths();
};

} // namespace Netease
