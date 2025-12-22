/**
 * AlbumCover.cpp - 专辑封面下载与缓存实现
 * 
 * NeteaseHookSDK v0.1.2
 */

// #define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define LOG_TAG "COVER"
#include "AlbumCover.h"
#include "SimpleLog.h"
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <Windows.h>
#include <WinINet.h>
#include <fstream>
#include <filesystem>
#include <map>
#include <list>

#pragma comment(lib, "WinINet.lib")

namespace fs = std::filesystem;

namespace Netease {

// 静态成员初始化
std::map<long long, AlbumCover::TextureCacheEntry> AlbumCover::s_textureCache;
std::list<long long> AlbumCover::s_lruList;

std::string AlbumCover::GetCacheDir() {
    // 使用 AppData\Local\NeteaseHookSDK\covers
    char* buffer = nullptr;
    size_t size = 0;
    std::string cacheDir = ".\\covers";
    
    if (_dupenv_s(&buffer, &size, "LOCALAPPDATA") == 0 && buffer != nullptr) {
        cacheDir = std::string(buffer) + "\\NeteaseHookSDK\\covers";
        free(buffer);
    }
    
    try {
        fs::create_directories(cacheDir);
    } catch (const fs::filesystem_error& e) {
        // 如果创建缓存目录失败，回退到临时目录
        try {
            cacheDir = fs::temp_directory_path().string() + "\\NeteaseHookSDK_covers";
            fs::create_directories(cacheDir);
        } catch (...) {
            // 如果创建临时目录失败，回退到当前目录
            cacheDir = ".\\covers";
            try { fs::create_directories(cacheDir); } catch (...) {}
        }
    }
    
    return cacheDir;
}

std::string AlbumCover::GetCachePath(long long songId) {
    if (songId <= 0) {
        LOG_ERROR("无效的 songId: " << songId);
        return ""; // 无效 songId, 返回空路径
    }
    return GetCacheDir() + "\\" + std::to_string(songId) + ".jpg";
}

bool AlbumCover::IsCached(long long songId) {
    return fs::exists(GetCachePath(songId));
}

bool AlbumCover::DownloadFile(const std::string& url, const std::string& localPath) {
    HINTERNET hInternet = InternetOpenA(
        "NeteaseHookSDK/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL, NULL, 0
    );
    if (!hInternet) {
        LOG_ERROR("InternetOpenA 失败: 错误码 " << GetLastError());
        return false;
    }
    
    // 设置 10 秒超时以防止 UI 冻结
    DWORD timeout = 10000; // 10 seconds
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hUrl = InternetOpenUrlA(
        hInternet,
        url.c_str(),
        NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
        0
    );
    if (!hUrl) {
        LOG_ERROR("InternetOpenUrlA 失败: URL=" << url << " 错误码=" << GetLastError());
        InternetCloseHandle(hInternet);
        return false;
    }

    // 读取数据 (限制最大50MB防止OOM)
    const size_t MAX_DOWNLOAD_SIZE = 50 * 1024 * 1024; // 50MB
    std::vector<char> buffer;
    buffer.reserve(1024 * 1024); // 预分配 1MB
    char readBuffer[8192];
    DWORD bytesRead;
    
    while (InternetReadFile(hUrl, readBuffer, sizeof(readBuffer), &bytesRead) && bytesRead > 0) {
        if (buffer.size() + bytesRead > MAX_DOWNLOAD_SIZE) {
            LOG_WARN("下载文件超过50MB限制: " << url);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }
        buffer.insert(buffer.end(), readBuffer, readBuffer + bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (buffer.empty()) {
        LOG_WARN("下载了0字节: URL=" << url);
        return false;
    }

    // 写入本地文件
    std::ofstream file(localPath, std::ios::binary);
    if (!file) {
        LOG_ERROR("打开文件失败: " << localPath);
        return false;
    }
    
    file.write(buffer.data(), buffer.size());
    if (!file.good()) {
        LOG_ERROR("文件写入失败: " << localPath);
        file.close();
        fs::remove(localPath); // 清理部分文件
        return false;
    }
    
    file.close();
    if (!file.good()) {
        LOG_ERROR("文件关闭失败: " << localPath);
        fs::remove(localPath); // 清理部分文件
        return false;
    }

    LOG_INFO("已下载 " << buffer.size() << " 字节到: " << fs::path(localPath).filename().string());
    return true;
}

Texture2D AlbumCover::LoadFromCache(long long songId) {
    std::string cachePath = GetCachePath(songId);
    if (!fs::exists(cachePath)) {
        return Texture2D{0};
    }
    
    return LoadTextureFromFile(cachePath, songId);
}

Texture2D AlbumCover::LoadFromUrl(const std::string& url, long long songId) {
    if (url.empty() || songId <= 0) {
        LOG_WARN("无效参数: url=" << url << " songId=" << songId);
        return Texture2D{0};
    }
    
    LOG_INFO("加载 Cover: songId=" <<songId);
    
    std::string cachePath = GetCachePath(songId);
    
    // 优先使用缓存
    if (IsCached(songId)) {
        return LoadFromCache(songId);
    }
    
    // 下载并缓存
    if (!DownloadFile(url, cachePath)) {
        return Texture2D{0};
    }
    
    return LoadFromCache(songId);
}

int AlbumCover::CleanOldCache(int keepCount) {
    std::string cacheDir = GetCacheDir();
    if (!fs::exists(cacheDir)) return 0;
    
    // 收集所有缓存文件
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& entry : fs::directory_iterator(cacheDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
            files.emplace_back(entry.last_write_time(), entry.path());
        }
    }
    
    if ((int)files.size() <= keepCount) return 0;
    
    // 按时间排序 (最旧的在前)
    std::sort(files.begin(), files.end());
    
    // 删除最旧的
    int deleted = 0;
    int toDelete = files.size() - keepCount;
    for (int i = 0; i < toDelete; i++) {
        fs::remove(files[i].second);
        deleted++;
    }
    
    return deleted;
}

// === 内存Texture缓存实现 (LRU) ===

void AlbumCover::UpdateLRU(long long songId) {
    // 移除旧位置
    auto it = s_textureCache.find(songId);
    if (it != s_textureCache.end()) {
        s_lruList.erase(it->second.lruIterator);
    }
    
    // 添加到最前面 (最新)
    s_lruList.push_front(songId);
}

void AlbumCover::EvictOldest() {
    if (s_lruList.empty()) return;
    
    long long oldestId = s_lruList.back();
    s_lruList.pop_back();
    
    auto it = s_textureCache.find(oldestId);
    if (it != s_textureCache.end()) {
        // 卸载GPU纹理
        UnloadTexture(it->second.texture);
        s_textureCache.erase(it);
        LOG_INFO("从 Cache 驱逐 Texture: songId=" << oldestId);
    }
}

void AlbumCover::ClearTextureCache() {
    for (auto& pair : s_textureCache) {
        UnloadTexture(pair.second.texture);
    }
    s_textureCache.clear();
    s_lruList.clear();
    LOG_INFO("Texture Cache 已清空");
}

Texture2D AlbumCover::LoadTextureFromFile(const std::string& filePath, long long songId) {
    // 1. 检查内存缓存
    auto it = s_textureCache.find(songId);
    if (it != s_textureCache.end()) {
        UpdateLRU(songId);
        it->second.lruIterator = s_lruList.begin();
        LOG_INFO("Texture Cache 命中: songId=" << songId);
        return it->second.texture;
    }
    
    // 2. 从文件加载
    LOG_INFO("从文件加载 Texture: " << fs::path(filePath).filename().string());
    int width, height, channels;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        LOG_ERROR("stbi_load 失败: " << stbi_failure_reason());
        return Texture2D{0};
    }
    
    // 3. 创建纹理
    Image image;
    image.data = data;
    image.width = width;
    image.height = height;
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    
    Texture2D texture = LoadTextureFromImage(image);
    stbi_image_free(data);
    
    if (texture.id == 0) {
        LOG_ERROR("LoadTextureFromImage 失败");
        return Texture2D{0};
    }
    
    // 4. 加入缓存
    if (s_textureCache.size() >= MAX_CACHE_SIZE) {
        EvictOldest();
    }
    
    UpdateLRU(songId);
    TextureCacheEntry entry;
    entry.texture = texture;
    entry.lruIterator = s_lruList.begin();
    s_textureCache[songId] = entry;
    
    LOG_INFO("已加入 Texture Cache (" << s_textureCache.size() << "/" << MAX_CACHE_SIZE << ")");
    return texture;
}

} // namespace Netease
