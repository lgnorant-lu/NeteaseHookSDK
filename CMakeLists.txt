# 网易云音乐 Hook SDK v0.0.2
# ============================================================
# 
# 项目说明：
# - 通过 CDP (Chrome DevTools Protocol) 获取播放进度
# - 无需 DLL 注入，无需 Hook
# - 需要网易云以 --remote-debugging-port=9222 启动
#
# 构建命令：
#   cmake -B build -S .
#   cmake --build build
#
# ============================================================

cmake_minimum_required(VERSION 3.20)
project(NeteaseHookSDK VERSION 0.0.2 LANGUAGES CXX)

# 支持多架构构建 (x86/x64)
# 使用 cmake -A x64 或 -A Win32 来指定目标架构

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成编译数据库（供 IDE 使用）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 输出目录 (架构分离)
# 检测当前编译架构
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_SUFFIX "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ARCH_SUFFIX "x86")
else()
    set(ARCH_SUFFIX "unknown")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/${ARCH_SUFFIX})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${ARCH_SUFFIX})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${ARCH_SUFFIX})

# ============================================================
# 依赖库
# ============================================================

include(FetchContent)

# MinHook (Hook 库)
message(STATUS "获取 MinHook...")
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

# Raylib (UI 库，用于测试程序)
message(STATUS "获取 Raylib...")
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        master
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# GoogleTest (单元测试框架)
message(STATUS "获取 GoogleTest...")
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ============================================================
# 子目录
# ============================================================

add_subdirectory(src/Agent)   # 启动参数注入 DLL (version.dll)
add_subdirectory(src/Driver)  # NeteaseDriver 静态库
add_subdirectory(src/App)     # 测试程序
add_subdirectory(src/Tests)   # 单元测试
add_subdirectory(examples)    # 示例程序 (C Demo)

# ============================================================
# 安装规则 (Install Rules for Distribution)
# ============================================================

# 安装二进制文件 (按架构分类)
install(TARGETS NeteaseDriver version
    RUNTIME DESTINATION bin/${ARCH_SUFFIX}
    LIBRARY DESTINATION bin/${ARCH_SUFFIX}
    ARCHIVE DESTINATION lib/${ARCH_SUFFIX}
)

# 安装头文件
install(FILES 
    src/Driver/include/NeteaseDriver.h
    src/Shared/SharedData.hpp
    DESTINATION include
)

# 安装文档
install(FILES
    README.md
    LICENSE
    DESTINATION .
)

install(DIRECTORY docs/
    DESTINATION docs
    FILES_MATCHING PATTERN "*.md"
)

# 安装示例
install(FILES
    examples/c_demo.c
    DESTINATION examples
)

