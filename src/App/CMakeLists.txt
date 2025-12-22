# ============================================================
# NeteaseMonitor 测试程序
# ============================================================
# 
# 使用 Raylib 显示播放进度的可视化程序
# v0.1.2: 添加封面系统 (使用 stb_image.h v2.30)
# v0.1.2: 添加日志控制 + WIN32 子系统 (无控制台窗口)
#
# ============================================================

add_executable(NeteaseMonitor
    main.cpp
    AlbumCover.h
    AlbumCover.cpp
    AudioCapture.h
    AudioCapture.cpp
    FftHelper.h
    Visualizer.h
    MemoryMonitor.h
    MemoryMonitor.cpp
)

# v0.1.2: Release 构建使用 WIN32 子系统 (隐藏控制台窗口)
# Debug 构建保留控制台以便调试
set_target_properties(NeteaseMonitor PROPERTIES
    WIN32_EXECUTABLE $<IF:$<CONFIG:Release>,TRUE,FALSE>
)

# 调试：打印路径
message(STATUS "raylib_SOURCE_DIR: ${raylib_SOURCE_DIR}")

# 添加当前目录以及 Raylib 的外部依赖路径
target_include_directories(NeteaseMonitor PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${raylib_SOURCE_DIR}/src
    ${raylib_SOURCE_DIR}/src/external
    ${CMAKE_SOURCE_DIR}/src/Shared
    ${CMAKE_SOURCE_DIR}/src/Driver/include
)

# 链接 Driver 库、Raylib 和 WinINet
target_link_libraries(NeteaseMonitor PRIVATE 
    NeteaseDriver
    # NeteaseAPI
    raylib
    wininet
    psapi  # Windows Process Status API (for memory monitoring)
)

# 复制 shader 资源到输出目录
add_custom_command(TARGET NeteaseMonitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/shaders"
        "$<TARGET_FILE_DIR:NeteaseMonitor>/resources/shaders"
    COMMENT "Copying shader resources to output directory..."
)

# ============================================================
# 安装规则 (v0.1.2 修复)
# ============================================================

# 安装可执行文件到 bin/${ARCH_SUFFIX}
install(TARGETS NeteaseMonitor
    RUNTIME DESTINATION bin/${ARCH_SUFFIX}
)

# 安装着色器资源到 bin/${ARCH_SUFFIX}/resources/shaders
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/shaders"
    DESTINATION bin/${ARCH_SUFFIX}/resources
)
