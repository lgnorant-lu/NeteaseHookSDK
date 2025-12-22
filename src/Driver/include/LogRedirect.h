#pragma once

// LogRedirect.h - 文件日志重定向辅助工具（Windows API 隔离）
// 用于将标准错误输出重定向到文件，避免在 main.cpp 中包含 <io.h> 等 Windows 头文件

#ifdef __cplusplus
extern "C" {
#endif

// 重定向 stderr 到指定文件
// 返回: true = 成功, false = 失败
bool RedirectStderrToFile(const char* filepath);

// 恢复 stderr 到控制台（如果需要）
void RestoreStderr();

#ifdef __cplusplus
}
#endif
