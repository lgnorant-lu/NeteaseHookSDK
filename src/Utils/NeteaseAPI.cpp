/**
 * NeteaseAPI.cpp - 网易云音乐数据获取工具实现
 * 
 * 网易云音乐 Hook SDK v0.1.2
 */

#include "NeteaseAPI.h"
#include <Windows.h>
#include <wininet.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <map>
#include <set>
#include <iostream>

#define LOG_TAG "API"
#include "SimpleLog.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;

namespace Netease {

// ============================================================================
// LyricData 成员实现
// ============================================================================

std::string LyricData::GetMergedLyric() const {
    if (lrc.empty()) return tlyric;
    if (tlyric.empty()) return lrc;
    
    return API::MergeLyrics(lrc, tlyric);
}

// ============================================================================
// API 公共接口实现
// ============================================================================

std::optional<LyricData> API::GetLyric(long long songId, bool useCache, const std::string& cookie) {
    // 1. 尝试从缓存获取
    if (useCache) {
        if (auto cached = GetLocalLyric(songId)) {
            cached->fromCache = true;
            return cached;
        }
    }
    
    // 2. 在线获取 (始终尝试更新缓存)
    auto online = FetchLyricOnline(songId, cookie, true);
    if (online) {
        online->fromCache = false;
    }
    
    return online;
}

std::optional<SongMetadata> API::GetSongDetail(long long songId) {
    // 构造 URL
    std::string url = "http://music.163.com/api/song/detail?id=" + std::to_string(songId) + 
                      "&ids=[" + std::to_string(songId) + "]";
    
    // 发送 HTTP 请求
    std::string response = HttpGet(url);
    if (response.empty()) {
        return std::nullopt;
    }
    
    // 解析 JSON 响应
    // 提取 songs 数组中的第一个元素
    SongMetadata meta;
    meta.songId = songId;
    
    // 提取 name (歌曲名)
    meta.title = ExtractJsonValue(response, "name");
    
    // 提取 album 信息
    meta.album = ExtractJsonValue(response, "album");
    
    // 提取封面 - 必须从 album 对象内提取
    // JSON结构: songs[0].album.picUrl (正确的专辑封面)
    // 而 songs[0].artists[0].picUrl 是艺术家头像 (错误)
    size_t albumKeyPos = response.find("\"album\"");
    if (albumKeyPos != std::string::npos) {
        size_t albumObjStart = response.find('{', albumKeyPos);
        if (albumObjStart != std::string::npos) {
            // 找到album对象结束位置 (查找匹配的'}'，考虑嵌套)
            int braceCount = 1;
            size_t searchPos = albumObjStart + 1;
            while (searchPos < response.length() && braceCount > 0) {
                if (response[searchPos] == '{') braceCount++;
                else if (response[searchPos] == '}') braceCount--;
                searchPos++;
            }
            if (braceCount == 0) {
                std::string albumObject = response.substr(albumObjStart, searchPos - albumObjStart);
                meta.albumPicUrl = ExtractJsonValue(albumObject, "picUrl");
            }
        }
    }
    
    // 提取时长
    std::string durationStr = ExtractJsonValue(response, "duration");
    meta.duration = durationStr.empty() ? 0 : std::stoll(durationStr);
    
    // 提取 artists 数组（简化处理：提取所有 name 字段）
    size_t pos = response.find("\"artists\"");
    if (pos != std::string::npos) {
        size_t arrayStart = response.find('[', pos);
        size_t arrayEnd = response.find(']', arrayStart);
        if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
            std::string artistsArray = response.substr(arrayStart, arrayEnd - arrayStart);
            
            // 提取所有 "name":"..." 字段
            std::regex namePattern("\"name\"\\s*:\\s*\"([^\"]*)\"");
            std::smatch match;
            std::string::const_iterator searchStart(artistsArray.cbegin());
            while (std::regex_search(searchStart, artistsArray.cend(), match, namePattern)) {
                meta.artists.push_back(match[1].str());
                searchStart = match.suffix().first;
            }
        }
    }
    
    return meta.title.empty() ? std::nullopt : std::make_optional(meta);
}

std::optional<LyricData> API::GetLocalLyric(long long songId) {
    auto dirs = GetLyricCacheDirs();
    std::string songIdStr = std::to_string(songId);
    
    for (const auto& dir : dirs) {
        std::string filePath = dir + "\\" + songIdStr;
        if (PathFileExistsA(filePath.c_str())) {
            return ParseCacheFile(filePath);
        }
    }
    
    return std::nullopt;
}

std::optional<LyricData> API::FetchLyricOnline(long long songId, const std::string& cookie, bool autoCache) {
    // 构造 URL
    std::string url = "https://music.163.com/api/song/lyric?id=" + std::to_string(songId) + 
                      "&lv=-1&kv=-1&tv=-1";
    
    // 发送 HTTP 请求
    std::string response = HttpGet(url, cookie);
    if (response.empty()) {
        return std::nullopt;
    }
    
    // 检查响应状态
    std::string code = ExtractJsonValue(response, "code");
    if (!code.empty() && code != "200") {
        return std::nullopt;
    }
    
    // 检查是否无歌词
    if (ExtractJsonValue(response, "nolyric") == "true" || 
        ExtractJsonValue(response, "uncollected") == "true") {
        return std::nullopt;
    }
    
    // 解析 JSON 响应
    LyricData data;
    data.fromCache = false;
    
    // 提取 lrc.lyric
    size_t lrcPos = response.find("\"lrc\"");
    if (lrcPos != std::string::npos) {
        size_t lyricStart = response.find("\"lyric\"", lrcPos);
        if (lyricStart != std::string::npos) {
            data.lrc = ExtractJsonValue(response.substr(lrcPos), "lyric");
        }
    }
    
    // 提取 tlyric.lyric
    size_t tlyricPos = response.find("\"tlyric\"");
    if (tlyricPos != std::string::npos) {
        size_t lyricStart = response.find("\"lyric\"", tlyricPos);
        if (lyricStart != std::string::npos) {
            data.tlyric = ExtractJsonValue(response.substr(tlyricPos), "lyric");
        }
    }
    
    // 提取 romalrc.lyric（如果存在）
    size_t romalrcPos = response.find("\"romalrc\"");
    if (romalrcPos != std::string::npos) {
        size_t lyricStart = response.find("\"lyric\"", romalrcPos);
        if (lyricStart != std::string::npos) {
            data.romalrc = ExtractJsonValue(response.substr(romalrcPos), "lyric");
        }
    }
    
    // 如果没有歌词，返回 nullopt
    if (data.lrc.empty()) {
        return std::nullopt;
    }
    
    // 自动缓存
    if (autoCache) {
        CacheLyric(songId, data);
    }
    
    return data;
}

bool API::CacheLyric(long long songId, const LyricData& data) {
    std::string songIdStr = std::to_string(songId);
    std::string jsonContent = SerializeLyricToJson(data);
    
    // 尝试写入网易云标准路径
    char localAppData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
        std::string neteaseDir = std::string(localAppData) + "\\Netease\\CloudMusic\\webdata\\lyric";
        
        // 创建目录（如果不存在）
        try {
            fs::create_directories(neteaseDir);
            
            std::string filePath = neteaseDir + "\\" + songIdStr;
            std::string tmpPath = filePath + ".tmp";
            
            // 写入临时文件
            std::ofstream ofs(tmpPath, std::ios::binary);
            if (ofs) {
                ofs << jsonContent;
                ofs.close();
                
                // 原子替换
                if (MoveFileExA(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    return true;
                }
            }
        } catch (...) {
            // 失败时降级到 SDK 路径
        }
    }
    
    // 降级：写入 SDK 缓存目录
    try {
        std::string sdkCacheDir = GetSDKCacheDir() + "\\lyric";
        fs::create_directories(sdkCacheDir);
        
        std::string filePath = sdkCacheDir + "\\" + songIdStr;
        std::string tmpPath = filePath + ".tmp";
        
        std::ofstream ofs(tmpPath, std::ios::binary);
        if (ofs) {
            ofs << jsonContent;
            ofs.close();
            
            if (MoveFileExA(tmpPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                return true;
            }
        }
    } catch (...) {
        return false;
    }
    
    return false;
}

bool API::ClearLyricCache(long long songId) {
    std::string songIdStr = std::to_string(songId);
    bool deleted = false;
    
    auto dirs = GetLyricCacheDirs();
    for (const auto& dir : dirs) {
        std::string filePath = dir + "\\" + songIdStr;
        if (DeleteFileA(filePath.c_str())) {
            deleted = true;
        }
    }
    
    return deleted;
}

int API::ClearAllCache() {
    int count = 0;
    std::string sdkCacheDir = GetSDKCacheDir() + "\\lyric";
    
    try {
        if (fs::exists(sdkCacheDir)) {
            for (const auto& entry : fs::directory_iterator(sdkCacheDir)) {
                if (entry.is_regular_file()) {
                    fs::remove(entry.path());
                    count++;
                }
            }
        }
    } catch (...) {
        // 忽略错误
    }
    
    return count;
}

std::string API::MergeLyrics(const std::string& lrc, const std::string& tlyric) {
    if (lrc.empty()) return tlyric;
    if (tlyric.empty()) return lrc;
    
    // 解析时间戳和文本的映射
    auto parseLyric = [](const std::string& lyric) -> std::map<std::string, std::string> {
        std::map<std::string, std::string> result;
        std::istringstream iss(lyric);
        std::string line;
        
        std::regex timePattern("\\[(\\d{2}:\\d{2}\\.\\d{2})\\]");
        
        while (std::getline(iss, line)) {
            std::smatch match;
            if (std::regex_search(line, match, timePattern)) {
                std::string timestamp = match[1].str();
                size_t textStart = line.find(']') + 1;
                std::string text = (textStart < line.length()) ? line.substr(textStart) : "";
                result[timestamp] = text;
            }
        }
        
        return result;
    };
    
    auto lrcMap = parseLyric(lrc);
    auto tlyricMap = parseLyric(tlyric);
    
    // 合并
    std::ostringstream oss;
    std::set<std::string> allTimestamps;
    
    for (const auto& [ts, _] : lrcMap) allTimestamps.insert(ts);
    for (const auto& [ts, _] : tlyricMap) allTimestamps.insert(ts);
    
    for (const auto& ts : allTimestamps) {
        std::string original = lrcMap[ts];
        std::string translation = tlyricMap[ts];
        
        oss << "[" << ts << "]";
        
        if (!original.empty() && !translation.empty()) {
            oss << original << " / " << translation;
        } else if (!original.empty()) {
            oss << original;
        } else {
            oss << translation;
        }
        
        oss << "\n";
    }
    
    return oss.str();
}

// ============================================================================
// API 私有辅助函数实现
// ============================================================================

std::string API::HttpGet(const std::string& url, const std::string& cookie) {
    HINTERNET hInternet = InternetOpenA(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64)", 
        INTERNET_OPEN_TYPE_PRECONFIG, 
        NULL, NULL, 0
    );
    
    if (!hInternet) return "";
    
    // 设置超时
    DWORD timeout = 8000; // 8 秒
    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    
    // 添加 Cookie
    std::string headers = "Referer: https://music.163.com/\r\n";
    if (!cookie.empty()) {
        headers += "Cookie: " + cookie + "\r\n";
    }
    
    HINTERNET hConnect = InternetOpenUrlA(
        hInternet, 
        url.c_str(), 
        headers.c_str(), 
        headers.length(), 
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 
        0
    );
    
    std::string response;
    
    if (hConnect) {
        char buffer[4096];
        DWORD bytesRead;
        
        while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            response.append(buffer, bytesRead);
        }
        
        InternetCloseHandle(hConnect);
    }
    
    InternetCloseHandle(hInternet);
    return response;
}

std::vector<std::string> API::GetLyricCacheDirs() {
    std::vector<std::string> dirs;
    
    char localAppData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) != S_OK) {
        return dirs;
    }
    
    std::string baseDir = localAppData;
    
    // PC 版路径
    std::vector<std::string> candidates = {
        baseDir + "\\Netease\\CloudMusic\\webdata\\lyric",
        baseDir + "\\Netease\\CloudMusic\\Download\\Lyric"
    };
    
    // UWP 版路径（模糊匹配）
    try {
        std::string packagesDir = baseDir + "\\Packages";
        if (fs::exists(packagesDir)) {
            for (const auto& entry : fs::directory_iterator(packagesDir)) {
                std::string name = entry.path().filename().string();
                if (name.find("1F8B0F94") != std::string::npos) {
                    // 可能的 UWP 网易云包
                    std::string lyricPath = entry.path().string() + "\\LocalState\\Lyric";
                    if (fs::exists(lyricPath)) {
                        candidates.push_back(lyricPath);
                    }
                }
            }
        }
    } catch (...) {
        // 忽略探测错误
    }
    
    // SDK 降级路径
    std::string sdkCache = GetSDKCacheDir() + "\\lyric";
    candidates.push_back(sdkCache);
    
    // 过滤存在的目录
    for (const auto& dir : candidates) {
        if (fs::exists(dir) && fs::is_directory(dir)) {
            dirs.push_back(dir);
        }
    }
    
    return dirs;
}

std::string API::GetSDKCacheDir() {
    char localAppData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) != S_OK) {
        return ".\\cache"; // 降级：当前目录
    }
    
    std::string cacheDir = std::string(localAppData) + "\\NeteaseHookSDK\\cache";
    
    // 确保目录存在
    try {
        fs::create_directories(cacheDir);
    } catch (...) {
        // 静默失败
    }
    
    return cacheDir;
}

std::string API::ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string keyPattern = "\"" + key + "\"";
    size_t keyPos = json.find(keyPattern);
    
    // 如果没找到，尝试不带引号的 key (虽然标准 JSON 不允许，但容错)
    if (keyPos == std::string::npos) {
        keyPos = json.find(key);
        if (keyPos == std::string::npos) return "";
    }
    
    // 找到冒号
    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";
    
    // 跳过冒号后的空白
    size_t valueStart = colonPos + 1;
    while (valueStart < json.length() && std::isspace(json[valueStart])) {
        valueStart++;
    }
    
    if (valueStart >= json.length()) return "";
    
    // 字符串值
    if (json[valueStart] == '"') {
        std::string value;
        bool escaped = false;
        
        for (size_t i = valueStart + 1; i < json.length(); ++i) {
            char c = json[i];
            
            if (escaped) {
                switch (c) {
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case '/': value += '/'; break;
                    case 'b': value += '\b'; break;
                    case 'f': value += '\f'; break;
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    default: value += c; break;
                }
                escaped = false;
            } else {
                if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    return value; // 结束
                } else {
                    value += c;
                }
            }
        }
    }
    // 数字值 或 布尔值
    else {
        size_t endPos = valueStart;
        while (endPos < json.length() && (isdigit(json[endPos]) || json[endPos] == '.' || isalpha(json[endPos]))) {
            endPos++;
        }
        return json.substr(valueStart, endPos - valueStart);
    }
    
    return "";
}

std::optional<LyricData> API::ParseCacheFile(const std::string& filePath) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs) return std::nullopt;
    
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    
    if (content.empty()) return std::nullopt;
    
    LyricData data;
    
    // 尝试 JSON 解析
    if (content.find("{") == 0) {
        data.lrc = ExtractJsonValue(content, "lyric");
        data.tlyric = ExtractJsonValue(content, "translateLyric");
        data.romalrc = ExtractJsonValue(content, "romalrc");
    } else {
        // 纯文本格式
        data.lrc = content;
    }
    
    data.fromCache = true;
    
    return data.lrc.empty() ? std::nullopt : std::make_optional(data);
}

std::string API::SerializeLyricToJson(const LyricData& data) {
    // 转义 JSON 字符串
    auto escapeJson = [](const std::string& str) -> std::string {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '\"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    };
    
    std::ostringstream oss;
    oss << "{";
    oss << "\"lyric\":\"" << escapeJson(data.lrc) << "\",";
    oss << "\"translateLyric\":\"" << escapeJson(data.tlyric) << "\"";
    
    if (!data.romalrc.empty()) {
        oss << ",\"romalrc\":\"" << escapeJson(data.romalrc) << "\"";
    }
    
    oss << "}";
    return oss.str();
}

} // namespace Netease
