add_library(version SHARED
    version_proxy.cpp
)

# 链接 MinHook
target_link_libraries(version PRIVATE minhook)

# 系统库依赖 (version.lib 用于链接转发? 不需要，因为我们直接用 linker comment)
# 但可能需要 user32 等
target_link_libraries(version PRIVATE user32)

# 设置输出名称为 version.dll
set_target_properties(version PROPERTIES 
    OUTPUT_NAME "version"
    PREFIX ""   # 去掉 "lib" 前缀
    SUFFIX ".dll"
)

# C++17
target_compile_features(version PRIVATE cxx_std_17)

# 安装规则已统一移至根 CMakeLists.txt (Line 108-112)
