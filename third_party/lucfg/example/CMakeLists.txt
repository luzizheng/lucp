add_executable(test_lucfg test_lucfg.c)
target_link_libraries(test_lucfg PRIVATE lucfg)
# 让示例运行时能找到刚编出的 so
set_target_properties(test_lucfg PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}")

install(TARGETS test_lucfg DESTINATION /usr/local/bin)