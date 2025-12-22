
# 测试可执行文件
add_executable(NeteaseSDKTest
    main_test.cpp
    # 这里可以添加其他测试文件
)

# 包含头文件路径
target_include_directories(NeteaseSDKTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Driver/include
    ${CMAKE_SOURCE_DIR}/src/Shared
    ${CMAKE_SOURCE_DIR}/src/Utils
)

# 链接依赖
target_link_libraries(NeteaseSDKTest PRIVATE
    GTest::gtest_main
    NeteaseDriver  # 链接核心库
)

# 启用测试发现
include(GoogleTest)
gtest_discover_tests(NeteaseSDKTest)
