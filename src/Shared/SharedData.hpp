#pragma once

/**
 * SharedData.hpp - 共享数据结构定义
 * 
 * 网易云音乐 Hook SDK v0.0.2
 */

namespace IPC {
    #pragma pack(push, 8)
    /**
     * 网易云音乐播放状态
     */
    struct NeteaseState {
        double currentProgress;   // 当前播放时间（秒）
        double totalDuration;     // 总时长（秒）- 保留字段，暂未实现
        char songId[64];          // 歌曲ID（如 "501220770_KRHXXN"）
        bool isPlaying;           // 是否正在播放
        wchar_t songName[64];     // 歌曲名 - 保留字段，暂未实现
        wchar_t artistName[64];   // 艺术家 - 保留字段，暂未实现
    };
    #pragma pack(pop)
}
