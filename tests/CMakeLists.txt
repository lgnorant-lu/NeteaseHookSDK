# ============================================================
# NeteaseAPI 测试程序
# ============================================================

add_executable(NeteaseAPITest
    test_api.cpp
)

# 链接库
target_link_libraries(NeteaseAPITest PRIVATE
    NeteaseDriver
    gtest
    gtest_main
)

# 头文件路径
target_include_directories(NeteaseAPITest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Utils
    ${CMAKE_SOURCE_DIR}/src/Shared
)

# 添加到测试
enable_testing()
add_test(NAME NeteaseAPITest COMMAND NeteaseAPITest)
