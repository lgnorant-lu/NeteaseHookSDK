#pragma once
#include <string>
#include <vector>
#include <optional>

/**
 * NeteaseAPI.h - 网易云音乐数据获取工具
 * 
 * 网易云音乐 Hook SDK v0.1.2
 * 
 * 功能：
 * - 从本地缓存读取歌词
 * - 从网易云 API 获取歌词（原版/翻译/罗马音）
 * - 从网易云 API 获取歌曲详细信息（标题/艺术家/专辑/封面）
 * - 智能缓存管理（自动写入本地，兼容网易云格式）
 * 
 * 设计原则：
 * - 轻量化：不依赖第三方 JSON 库，手动解析
 * - 容错性：网络请求失败时返回 std::nullopt
 * - 兼容性：支持 x86/x64，使用 WinINet 库
 * - 缓存优先：减少网络请求，提升性能
 */

namespace Netease {

    /**
     * 歌曲元数据结构
     * 
     * 包含从网易云 API 获取的歌曲基本信息
     */
    struct SongMetadata {
        long long songId;           // 歌曲 ID
        std::string title;          // 歌曲标题
        std::vector<std::string> artists;  // 艺术家列表
        std::string album;          // 专辑名称
        std::string albumPicUrl;    // 专辑封面 URL
        long long duration;         // 时长（毫秒）
    };

    /**
     * 歌词数据结构
     * 
     * 包含从网易云 API 或本地缓存获取的歌词信息
     */
    struct LyricData {
        std::string lrc;            // 原版歌词（LRC 格式）
        std::string tlyric;         // 翻译歌词（LRC 格式，可能为空）
        std::string romalrc;        // 罗马音歌词（LRC 格式，可能为空）
        bool fromCache = false;     // 是否来自本地缓存
        
        /**
         * 合并原版和翻译歌词
         * 
         * @return 合并后的 LRC 格式字符串（原文 / 翻译）
         */
        std::string GetMergedLyric() const;
        
        /**
         * 是否包含有效歌词
         */
        bool IsValid() const { return !lrc.empty(); }
    };

    /**
     * 网易云音乐 API 工具类
     * 
     * 提供歌曲元数据和歌词获取功能
     * 所有方法均为线程安全的静态方法
     */
    class API {
    public:
        // ====================================================================
        // 主要接口（推荐使用）
        // ====================================================================

        /**
         * 智能获取歌词（推荐）
         * 
         * 工作流程：
         * 1. 先查询本地缓存
         * 2. 缓存未命中 -> 在线获取
         * 3. 在线获取成功 -> 自动写入本地缓存
         * 4. 返回结果
         * 
         * @param songId 歌曲 ID（纯数字，如 2047103213）
         * @param useCache 是否使用缓存（默认 true）
         * @param cookie 可选的 Cookie 字符串（某些 VIP 歌曲需要）
         * @return 成功返回歌词数据，失败返回 nullopt
         * 
         * @note 缓存写入失败不影响返回结果
         * @note 第二次访问相同歌曲时几乎瞬时返回
         * 
         * @example
         * // 普通使用
         * auto lyric = API::GetLyric(2047103213);
         * if (lyric) {
         *     std::cout << lyric->GetMergedLyric();
         * }
         * 
         * // 强制刷新（跳过缓存）
         * auto fresh = API::GetLyric(2047103213, false);
         */
        static std::optional<LyricData> GetLyric(
            long long songId, 
            bool useCache = true,
            const std::string& cookie = ""
        );

        /**
         * 获取歌曲详细信息
         * 
         * @param songId 歌曲 ID（纯数字）
         * @return 成功返回歌曲元数据，失败返回 nullopt
         * 
         * @note 不需要登录
         * @note 暂不缓存（元数据较少变化，后续版本可添加）
         */
        static std::optional<SongMetadata> GetSongDetail(long long songId);

        // ====================================================================
        // 高级接口（精细控制）
        // ====================================================================

        /**
         * 从本地缓存获取歌词（不联网）
         * 
         * 查找路径：
         * - %LOCALAPPDATA%\\Netease\\CloudMusic\\webdata\\lyric\\{songId}
         * - %LOCALAPPDATA%\\Netease\\CloudMusic\\Download\\Lyric\\{songId}
         * - %LOCALAPPDATA%\\NeteaseHookSDK\\cache\\lyric\\{songId}（SDK 降级路径）
         * - UWP 版本路径（自动探测）
         * 
         * @param songId 歌曲 ID（纯数字）
         * @return 成功返回歌词内容（LRC 格式），失败返回 nullopt
         * 
         * @note 支持 JSON 格式（{"lyric": "..."}）和纯文本格式
         * @note 自动处理 \\n 转义符
         */
        static std::optional<LyricData> GetLocalLyric(long long songId);

        /**
         * 从网易云 API 强制在线获取歌词（跳过缓存）
         * 
         * API 端点：https://music.163.com/api/song/lyric
         * 
         * @param songId 歌曲 ID（纯数字）
         * @param cookie 可选的 Cookie 字符串（某些 VIP 歌曲需要）
         * @param autoCache 是否自动写入缓存（默认 true）
         * @return 成功返回歌词数据（包含原版/翻译/罗马音），失败返回 nullopt
         * 
         * @note 不需要登录即可获取大部分歌词
         * @note 网络请求失败或歌曲无歌词时返回 nullopt
         * @note 用于刷新缓存或获取最新歌词
         */
        static std::optional<LyricData> FetchLyricOnline(
            long long songId, 
            const std::string& cookie = "",
            bool autoCache = true
        );

        // ====================================================================
        // 缓存管理接口
        // ====================================================================

        /**
         * 手动缓存歌词到本地
         * 
         * @param songId 歌曲 ID
         * @param data 歌词数据
         * @return 是否成功写入
         * 
         * @note 会尝试写入网易云标准路径，失败则降级到 SDK 路径
         * @note 使用临时文件 + 原子替换保证数据完整性
         */
        static bool CacheLyric(long long songId, const LyricData& data);

        /**
         * 清除指定歌曲的缓存
         * 
         * @param songId 歌曲 ID
         * @return 是否成功删除
         */
        static bool ClearLyricCache(long long songId);

        /**
         * 清除所有歌词缓存
         * 
         * @return 成功删除的文件数量
         * 
         * @note 仅清除 SDK 自己的缓存路径，不清除网易云原有缓存
         */
        static int ClearAllCache();

        // ====================================================================
        // 工具函数
        // ====================================================================

        /**
         * 合并原版和翻译歌词
         * 
         * 将同一时间戳的原版和翻译歌词合并为 "原文 / 翻译" 格式
         * 
         * @param lrc 原版歌词（LRC 格式）
         * @param tlyric 翻译歌词（LRC 格式）
         * @return 合并后的歌词
         * 
         * @example
         * 输入：
         *   lrc    = "[00:10.00]Hello world"
         *   tlyric = "[00:10.00]你好世界"
         * 输出：
         *   "[00:10.00]Hello world / 你好世界"
         */
        static std::string MergeLyrics(const std::string& lrc, const std::string& tlyric);
        
    private:
        /**
         * 发送 HTTP GET 请求
         * 
         * @param url 完整的 URL
         * @param cookie 可选的 Cookie 字符串
         * @return HTTP 响应体，失败时返回空字符串
         * 
         * @note 使用 WinINet 实现，仅支持 Windows
         * @note 超时时间：8 秒
         */
        static std::string HttpGet(const std::string& url, const std::string& cookie = "");

        /**
         * 获取网易云音乐歌词缓存目录列表
         * 
         * @return 可能存在的缓存目录路径列表
         * 
         * @note 自动探测 PC 版、UWP 版和 SDK 降级路径
         * @note 仅返回实际存在的目录
         */
        static std::vector<std::string> GetLyricCacheDirs();

        /**
         * 获取 SDK 缓存目录（降级路径）
         * 
         * @return SDK 缓存根目录路径
         * @note 不存在时自动创建
         */
        static std::string GetSDKCacheDir();

        /**
         * 从 JSON 字符串中提取键值
         * 
         * @param json JSON 字符串
         * @param key 键名
         * @return 提取的值（字符串形式），未找到时返回空字符串
         * 
         * @note 简单实现，仅支持字符串值提取
         * @note 不处理嵌套对象
         */
        static std::string ExtractJsonValue(const std::string& json, const std::string& key);

        /**
         * 解析网易云本地缓存文件
         * 
         * @param filePath 缓存文件路径
         * @return 解析后的歌词数据
         * 
         * @note 支持 JSON 和纯文本两种格式
         * @note 自动处理 \n 转义
         */
        static std::optional<LyricData> ParseCacheFile(const std::string& filePath);

        /**
         * 将歌词数据序列化为网易云兼容的 JSON 格式
         * 
         * @param data 歌词数据
         * @return JSON 字符串
         */
        static std::string SerializeLyricToJson(const LyricData& data);
    };

} // namespace Netease

