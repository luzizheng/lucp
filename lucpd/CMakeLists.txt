
# 确保目录存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 源文件
set(LUCPD_SRC
    src/lucpd_cfg.c
    src/lucpd_utils.c
    src/lucpd.c
)

# 生成可执行文件
add_executable(lucpd ${LUCPD_SRC})
target_compile_options(lucpd PRIVATE 
  -Werror 
  -Wno-error=sign-compare
)
target_link_libraries(lucpd PRIVATE lucfg lucp)

# 安装配置
install(TARGETS lucpd DESTINATION /usr/local/bin)
install(CODE "execute_process(COMMAND systemctl daemon-reload)")