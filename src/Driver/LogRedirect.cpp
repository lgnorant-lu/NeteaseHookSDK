// LogRedirect.cpp - 文件日志重定向实现（Windows API 隔离）

#include "LogRedirect.h"
#include <cstdio>

// Windows-specific includes (隔离在此文件)
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static FILE* g_RedirectedFile = nullptr;
static int g_OriginalStderr = -1;

bool RedirectStderrToFile(const char* filepath) {
    if (!filepath) return false;
    
    // 打开日志文件
    g_RedirectedFile = fopen(filepath, "w");
    if (!g_RedirectedFile) {
        return false;
    }
    
#ifdef _WIN32
    // 保存原始 stderr 文件描述符
    g_OriginalStderr = _dup(_fileno(stderr));
    
    // 重定向 stderr 到文件
    if (_dup2(_fileno(g_RedirectedFile), _fileno(stderr)) != 0) {
        fclose(g_RedirectedFile);
        g_RedirectedFile = nullptr;
        return false;
    }
    
    // 禁用 stderr 缓冲，确保实时写入
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif
    
    return true;
}

void RestoreStderr() {
#ifdef _WIN32
    if (g_OriginalStderr != -1) {
        _dup2(g_OriginalStderr, _fileno(stderr));
        _close(g_OriginalStderr);
        g_OriginalStderr = -1;
    }
#endif
    
    if (g_RedirectedFile) {
        fclose(g_RedirectedFile);
        g_RedirectedFile = nullptr;
    }
}
