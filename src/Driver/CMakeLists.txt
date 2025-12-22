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
    LogRedirect.cpp
    ${CMAKE_SOURCE_DIR}/src/Utils/NeteaseAPI.cpp  # 网易云 API 工具
    ${CMAKE_SOURCE_DIR}/extern/easywsclient.cpp  # WebSocket 独立编译
)

# 自动导出所有符号 (避免修改大量头文件添加 __declspec(dllexport))
set_target_properties(NeteaseDriver PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# 头文件路径
target_include_directories(NeteaseDriver PUBLIC 
    include
    ${CMAKE_SOURCE_DIR}/extern      # httplib, easywsclient
    ${CMAKE_SOURCE_DIR}/src/Shared  # SharedData.hpp
    ${CMAKE_SOURCE_DIR}/src/Utils   # NeteaseAPI.h
)

# Windows 网络库
target_link_libraries(NeteaseDriver PRIVATE 
    kernel32 
    user32 
    ws2_32      # Winsock
    wininet     # WinINet (HTTP 请求)
    shlwapi     # 路径工具
)

# C++17（用于 std::regex）
target_compile_features(NeteaseDriver PRIVATE cxx_std_17)

# MSVC 特定编译选项
if(MSVC)
    target_compile_options(NeteaseDriver PRIVATE /FS)  # 支持并行编译
endif()

# 启用导出标志
target_compile_definitions(NeteaseDriver PRIVATE 
    NETEASE_DRIVER_EXPORTS=1
    CPPHTTPLIB_NO_EXCEPTIONS=1
)


# ============================================================
# 安装规则
# ============================================================
# 注意：安装规则已统一移至根 CMakeLists.txt (Line 108-112)
# 这里保留头文件的安装规则以保持模块独立性

install(DIRECTORY include/
    DESTINATION include
)

install(FILES ${CMAKE_SOURCE_DIR}/src/Utils/NeteaseAPI.h
    DESTINATION include
)
