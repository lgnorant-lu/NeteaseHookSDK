# ============================================================
# NeteaseMonitor 测试程序
# ============================================================
# 
# 使用 Raylib 显示播放进度的可视化程序
#
# ============================================================

add_executable(NeteaseMonitor
    main.cpp
)

# 链接 Driver 库和 Raylib
target_link_libraries(NeteaseMonitor PRIVATE 
    NeteaseDriver 
    raylib
)
