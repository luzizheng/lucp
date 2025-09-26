
cmake_minimum_required(VERSION 3.10)
project(lucfg
        VERSION 1.0.0
        DESCRIPTION "Extensible, thread-safe INI-style config parser"
        LANGUAGES C)
        
# 选项：是否编译示例
option(lucfg_BUILD_EXAMPLE "Build example" ON)

# 生成共享库
add_library(lucfg SHARED
    src/lucfg.c)

target_include_directories(lucfg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

# 需要线程库
find_package(Threads REQUIRED)
target_link_libraries(lucfg PRIVATE Threads::Threads)



# 安装规则
include(GNUInstallDirs)
install(TARGETS lucfg
        EXPORT lucfgTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/lucfg.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# 生成并安装 CMake 配置文件
install(EXPORT lucfgTargets
        FILE lucfgTargets.cmake
        NAMESPACE lucfg::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lucfg)

# 版本文件
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/lucfgConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_file(cmake/lucfgConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/lucfgConfig.cmake"
    @ONLY)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/lucfgConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/lucfgConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lucfg)

# 子目录
if(lucfg_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()

# 最后补一句别名，方便外部统一用 lucfg::lucfg
add_library(lucfg::lucfg ALIAS lucfg)