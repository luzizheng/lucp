
# 确保目录存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 源文件
set(LUFTPD_SRC src/luftpd.c)

# 生成可执行文件
add_executable(luftpd ${LUFTPD_SRC})
target_compile_options(luftpd PRIVATE 
  -Werror 
  -Wno-error=sign-compare
  -Wno-error=format-truncation
)
target_link_libraries(luftpd PRIVATE lucfg)

# 安装配置
install(TARGETS luftpd DESTINATION /usr/local/bin)
install(CODE "execute_process(COMMAND systemctl daemon-reload)")