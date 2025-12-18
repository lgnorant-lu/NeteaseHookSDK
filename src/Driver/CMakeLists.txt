# ============================================================
# NeteaseDriver 静态库
# ============================================================
# 
# 这是 SDK 的核心库，提供获取网易云播放进度的 API
#
# 依赖：
# - cpp-httplib (HTTP 客户端，单头文件)
# - easywsclient (WebSocket 客户端)
#
# ============================================================

add_library(NeteaseDriver SHARED
    NeteaseDriver.cpp
    CDPController.cpp
    ${CMAKE_SOURCE_DIR}/extern/easywsclient.cpp  # WebSocket 独立编译
)

# 自动导出所有符号 (避免修改大量头文件添加 __declspec(dllexport))
set_target_properties(NeteaseDriver PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# 头文件路径
target_include_directories(NeteaseDriver PUBLIC 
    include
    ${CMAKE_SOURCE_DIR}/extern      # httplib, easywsclient
    ${CMAKE_SOURCE_DIR}/src/Shared  # SharedData.hpp
)

# Windows 网络库
target_link_libraries(NeteaseDriver PRIVATE 
    kernel32 
    user32 
    ws2_32  # Winsock
)

# C++17（用于 std::regex）
target_compile_features(NeteaseDriver PRIVATE cxx_std_17)

# 禁用异常（可选优化）
target_compile_definitions(NeteaseDriver PRIVATE 
    CPPHTTPLIB_NO_EXCEPTIONS=1
)


# ============================================================
# 安装规则
# ============================================================

install(TARGETS NeteaseDriver
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)
