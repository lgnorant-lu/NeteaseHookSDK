/**
 * main.cpp - 网易云音乐 Hook SDK 测试程序
 * 
 * 使用 Raylib 显示播放进度 (Glassmorphism UI)
 */

#include "raylib.h"
#include "NeteaseDriver.h"
#include <vector>
#include <string>

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

// 窗口拖拽状态
struct DragState {
    bool isDragging = false;
    Vector2 offset = {0, 0};
} static g_DragState;

// --- 回调 & Toast 状态 ---
#include <mutex>
#include <atomic>

std::mutex g_ToastMutex;
std::string g_PendingSongId;
std::atomic<bool> g_HasNewSong(false);

struct ToastState {
    float alpha = 0.0f;
    std::string message;
    double startTime = 0;
} static g_Toast;

void OnTrackChanged(const std::string& songId) {
    std::lock_guard<std::mutex> lock(g_ToastMutex);
    g_PendingSongId = songId;
    g_HasNewSong = true;
    // 注意：不要在这里直接调用 Raylib 绘图函数，因为这是后台线程
}

int main() {
    // 1. 设置窗口标志：无边框 + 透明 + 顶层
    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TRANSPARENT | FLAG_WINDOW_TOPMOST);
    InitWindow(420, 160, "NCM Widget"); // 稍微加宽一点
    SetTargetFPS(60); 

    // 2. 加载中文字体 
    int codepointCount = 95 + (0x9FFF - 0x4E00 + 1);
    int* codepoints = (int*)malloc(codepointCount * sizeof(int));
    for (int i = 0; i < 95; i++) codepoints[i] = 32 + i;
    for (int i = 0; i < (0x9FFF - 0x4E00 + 1); i++) codepoints[95 + i] = 0x4E00 + i;
    
    Font font = LoadFontEx("C:/Windows/Fonts/simhei.ttf", 20, codepoints, codepointCount);
    if (font.baseSize == 0) font = LoadFontEx("C:/Windows/Fonts/msyh.ttc", 20, codepoints, codepointCount);
    if (font.baseSize == 0) font = GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    free(codepoints);

    // 辅助绘制函数
    auto DrawUI = [&](const char* text, int x, int y, int size, Color color) {
        DrawTextEx(font, text, Vector2{(float)x+1, (float)y+1}, (float)size, 1, ColorAlpha(BLACK, 0.3f));
        DrawTextEx(font, text, Vector2{(float)x, (float)y}, (float)size, 1, color);
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
    // 3. 连接驱动
    auto& driver = NeteaseDriver::Instance();
    
    // 注册回调 (必须在 Connect 之前或之后均可，只要 Driver 存在)
    driver.SetTrackChangedCallback(OnTrackChanged);
    
    bool connected = driver.Connect(9222);
    
    std::string installPath = NeteaseDriver::GetInstallPath();
    bool hookInstalled = false;
    if (!installPath.empty()) {
        hookInstalled = NeteaseDriver::IsHookInstalled();
    }
    
    // 状态计时器（用于非阻塞操作）
    double restartStartTime = 0;
    bool isRestarting = false;

    // 颜色定义 (Cyan/Green Theme)
    Color THEME_PRIMARY = { 0, 255, 200, 255 };    // 青色高亮
    Color THEME_SECONDARY = { 0, 200, 180, 200 };  // 次要文字
    Color THEME_BG = { 10, 20, 25, 180 };          // 深青空背景 (0.7 alpha) (from 220 -> 180 ~0.7)
    Color THEME_BAR_BG = { 255, 255, 255, 40 };

    while (!WindowShouldClose()) {
        double currentTime = GetTime();

        // --- 拖拽逻辑 (无滞后平滑版) ---
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            g_DragState.isDragging = true;
            g_DragState.offset = GetMousePosition();
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            g_DragState.isDragging = false;
        }
        if (g_DragState.isDragging) {
            Vector2 mousePos = GetMousePosition();
            Vector2 winPos = GetWindowPosition();
            SetWindowPosition((int)(winPos.x + mousePos.x - g_DragState.offset.x), 
                              (int)(winPos.y + mousePos.y - g_DragState.offset.y));
        }

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
            std::string dllPath = "build/bin/version.dll";
            if (!FileExists(dllPath.c_str())) dllPath = "version.dll";
            
            if (FileExists(dllPath.c_str())) {
                if (NeteaseDriver::InstallHook(dllPath)) {
                    hookInstalled = true;
                    // 安装后自动重启
                    NeteaseDriver::RestartApplication(installPath);
                    isRestarting = true;
                    restartStartTime = currentTime;
                    connected = false;
                    driver.Disconnect();
                }
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
        if (isRestarting) {
            if (currentTime - restartStartTime > 2.0) {
                // 等待超时，尝试重新连接
                connected = driver.Connect(9222);
                if (connected) {
                    isRestarting = false;
                } else {
                    // 持续尝试重连 (每0.5秒)
                    if (fmod(currentTime, 1.0) < 0.1) {
                         connected = driver.Connect(9222);
                         if (connected) isRestarting = false;
                    }
                }
            }
        } else {
             // 正常运行时的简单心跳
             IPC::NeteaseState state = driver.GetState();
        }
        
        // 获取最新状态
        IPC::NeteaseState state = driver.GetState();

        // --- 处理 Toast 触发 ---
        if (g_HasNewSong) {
            std::string newId;
            {
                std::lock_guard<std::mutex> lock(g_ToastMutex);
                newId = g_PendingSongId;
                g_HasNewSong = false;
            }
            g_Toast.message = "Switched to: " + newId;
            g_Toast.alpha = 1.0f; // 开始显示
            g_Toast.startTime = currentTime;
        }
        
        // 更新 Toast 动画 (Fade out after 3s)
        if (g_Toast.alpha > 0) {
            if (currentTime - g_Toast.startTime > 3.0) {
                g_Toast.alpha -= 0.02f; // 淡出
            }
            if (g_Toast.alpha < 0) g_Toast.alpha = 0;
        }

        BeginDrawing();
        ClearBackground(BLANK);

        // --- 绘制 Glass 背景 (青色系) ---
        DrawRectangleRounded(Rectangle{0, 0, 420, 160}, 0.1f, 10, THEME_BG);
        DrawRectangleRoundedLines(Rectangle{0, 0, 420, 160}, 0.1f, 10, ColorAlpha(THEME_PRIMARY, 0.3f));

        if (isRestarting) {
             DrawUI(u8"⏳ 正在重启网易云...", 110, 50, 20, THEME_PRIMARY);
             DrawUI(u8"请稍候...", 160, 80, 16, LIGHTGRAY);
        }
        else if (!connected) {
            DrawUI(u8"⚠ 未连接网易云音乐", 20, 20, 24, ORANGE);
            
            if (installPath.empty()) {
                DrawUI(u8"未找到安装路径 ! (按 R 重试)", 20, 60, 18, LIGHTGRAY);
            } else {
                if (hookInstalled) {
                    DrawUI(u8"Hook 已就绪", 20, 60, 18, THEME_PRIMARY);
                    DrawUI(u8"请重启网易云 (按 K 重启)", 20, 90, 18, SKYBLUE);
                } else {
                    DrawUI(u8"Hook 未安装 (按 I 安装)", 20, 60, 18, RED);
                }
            }
        } else {
            // 已连接 - 显示播放信息
            if (state.isPlaying) {
                DrawUI(u8"▶ NOW PLAYING", 20, 18, 16, THEME_PRIMARY);
            } else {
                DrawUI(u8"⏸ PAUSED", 20, 18, 16, GOLD);
            }


            // 歌曲 ID
            DrawUI(TextFormat("Song: %s", state.songId), 20, 45, 20, WHITE);
            
            // 时间显示: 01:23 / 04:56
            std::string timeStr = FormatTime(state.currentProgress);
            if (state.totalDuration > 0) {
                timeStr += " / " + FormatTime(state.totalDuration);
            }
            DrawUI(timeStr.c_str(), 20, 125, 18, THEME_SECONDARY);

            // 进度条背景
            DrawRectangleRounded(Rectangle{20, 85, 380, 8}, 0.5f, 10, THEME_BAR_BG);
            
            // 进度条前景
            double duration = state.totalDuration > 0 ? state.totalDuration : 300.0; // 默认5分钟
            float progressRatio = (float)(state.currentProgress / duration);
            if (progressRatio > 1.0f) progressRatio = 1.0f;
            
            // 动态呼吸灯效果
            int alpha = 180 + (int)(sinf((float)currentTime * 3) * 50);
            Color barColor = THEME_PRIMARY;
            barColor.a = (unsigned char)alpha;
            
            DrawRectangleRounded(Rectangle{20, 85, 380 * progressRatio, 8}, 0.5f, 10, barColor);
            
            // 频谱条 (Spectrum) - 装饰性
            for (int i=0; i<8; i++) {
                float h = 12 + sinf((float)currentTime * 10 + i * 0.5f) * 10;
                if (!state.isPlaying) h = 3;
                DrawRectangle(320 + i*10, 125 - (int)h/2, 6, (int)h, ColorAlpha(THEME_PRIMARY, 0.6f));
            }
        }
        
        // --- 绘制 Toast ---
        if (g_Toast.alpha > 0.01f) {
            Color toastBg = { 50, 50, 50, (unsigned char)(200 * g_Toast.alpha) };
            Color toastText = { 255, 255, 255, (unsigned char)(255 * g_Toast.alpha) };
            
            int textWidth = MeasureTextEx(font, g_Toast.message.c_str(), 18, 1).x;
            int toastW = textWidth + 40;
            int toastX = (420 - toastW) / 2;
            int toastY = 130;  // 底部显示
            
            DrawRectangleRounded(Rectangle{(float)toastX, (float)toastY, (float)toastW, 28}, 0.5f, 10, toastBg);
            DrawTextEx(font, g_Toast.message.c_str(), Vector2{(float)(toastX + 20), (float)(toastY + 5)}, 18, 1, toastText);
        }

        EndDrawing();
    }
    
    CloseWindow();
    return 0;
}
