cmake_minimum_required(VERSION 3.10)
project(liblucp VERSION 1.0.0 LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 启用位置无关代码（对于共享库必需）
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 定义源文件和头文件
set(LUCP_SOURCES
    lucp.c
)

set(LUCP_HEADERS
    lucp.h
)

# 构建动态库
add_library(lucp SHARED
    ${LUCP_SOURCES}
    ${LUCP_HEADERS}
)

# 设置库版本信息
set_target_properties(lucp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "lucp"
)

# 添加编译选项，增强代码健壮性
target_compile_options(lucp PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wpedantic
    -fstack-protector-strong
)

# 包含头文件目录
target_include_directories(lucp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# 安装配置
install(TARGETS lucp
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# 安装头文件
install(FILES ${LUCP_HEADERS}
    DESTINATION include
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)

# 可选: 生成pkg-config文件, 方便其他项目使用
configure_file(lucp.pc.in lucp.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lucp.pc
    DESTINATION lib/pkgconfig
)