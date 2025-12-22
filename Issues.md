# Known Issues

本文件记录 NeteaseHookSDK 已知的待解决问题。

## 1. 歌曲总时长显示异常 [v0.1.3+]
- **描述**: 歌曲总时长，偶发可能因波 exe/CDP 卡顿动变为 `00:00`。
- **原因**: 底层 Driver 对 `totalDuration` 字段支持不稳定，依赖 CDP 实时返回。
- **建议**: 当前暂缓修复，待后续重构驱动层数据同步机制时统一解决。
- **状态**: Open (Planned for future refactor)
