project(Examples)

# C Demo
add_executable(c_demo c_demo.c)

# 链接 NeteaseDriver (它现在是 SHARED 库，会自动链接 import lib)
target_link_libraries(c_demo PRIVATE NeteaseDriver)

# 复制 DLL 到可执行文件目录
add_custom_command(TARGET c_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:NeteaseDriver>
    $<TARGET_FILE_DIR:c_demo>
)

