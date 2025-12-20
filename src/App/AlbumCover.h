#pragma once
/**
 * AlbumCover.h - 专辑封面下载与缓存模块
 * 
 * NeteaseHookSDK v0.1.0
 * 
 * 功能：
 * - 从 URL 下载封面图片
 * - 本地文件缓存（磁盘）
 * - 内存 Texture 缓存（LRU 策略，最多10个）
 * - 转换为 Raylib Texture2D
 */

#include "raylib.h"
#include <string>
#include <optional>
#include <map>
#include <list>

namespace Netease {

/**
 * 专辑封面管理器
 */
class AlbumCover {
public:
    /**
     * 下载并加载封面
     * 
     * @param url 封面 URL (如 https://p1.music.126.net/...)
     * @param songId 歌曲 ID (用于缓存文件名)
     * @return Raylib Texture2D，失败返回空纹理 (texture.id == 0)
     */
    static Texture2D LoadFromUrl(const std::string& url, long long songId);
    
    /**
     * 从本地缓存加载
     * 
     * @param songId 歌曲 ID
     * @return Raylib Texture2D，不存在返回空纹理
     */
    static Texture2D LoadFromCache(long long songId);
    
    /**
     * 检查缓存是否存在
     */
    static bool IsCached(long long songId);
    
    /**
     * 获取缓存目录
     */
    static std::string GetCacheDir();
    
    /**
     * 清理过期缓存 (保留最近 N 张)
     */
    static int CleanOldCache(int keepCount = 50);
    
    /**
     * 清理内存Texture缓存 (应用退出时调用)
     */
    static void ClearTextureCache();

private:
    /**
     * 下载文件到本地
     */
    static bool DownloadFile(const std::string& url, const std::string& localPath);
    
    /**
     * 获取缓存文件路径
     */
    static std::string GetCachePath(long long songId);
    
    /**
     * 从文件加载Texture (带内存缓存)
     */
    static Texture2D LoadTextureFromFile(const std::string& filePath, long long songId);
    
    // === 内存Texture缓存 (LRU) ===
    struct TextureCacheEntry {
        Texture2D texture;
        std::list<long long>::iterator lruIterator;
    };
    
    static std::map<long long, TextureCacheEntry> s_textureCache;
    static std::list<long long> s_lruList;  // 最近使用顺序 (最新在前)
    static const size_t MAX_CACHE_SIZE = 10; // 最多缓存10个Texture
    
    static void UpdateLRU(long long songId);
    static void EvictOldest();
};

} // namespace Netease
