cmake_minimum_required(VERSION 3.10)
project(
  logMgr
  VERSION 1.0.0
  DESCRIPTION "A simple and efficient logging library"
  LANGUAGES C)



# 生成协议共享库
add_library(log_protocol SHARED src/log_protocol.c)
target_include_directories(
  log_protocol PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                      $<INSTALL_INTERFACE:include>)
set_target_properties(log_protocol PROPERTIES OUTPUT_NAME "log_protocol")
# 需要线程库
find_package(Threads REQUIRED)
target_link_libraries(log_protocol PRIVATE Threads::Threads)

# 生成日志客户端动态库
add_library(log_cli SHARED src/log_cli_impl.c)
target_include_directories(
  log_cli PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                      $<INSTALL_INTERFACE:include>)
target_link_libraries(log_cli PRIVATE log_protocol)
set_target_properties(log_cli PROPERTIES OUTPUT_NAME "log_cli")


# 生成日志服务端程序
add_executable(logd src/logd.c src/log_server.c src/logd_cfg.c)
target_link_libraries(logd PRIVATE lucfg log_protocol)


# 安装规则
include(GNUInstallDirs)
install(
  TARGETS log_protocol
  EXPORT log_protocolTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/log_protocol.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  TARGETS log_cli
  EXPORT log_cliTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/log_cli_api.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


# 生成logd安装
install(TARGETS logd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# 安装配置文件到/etc
install(FILES cfg/logMgr.conf DESTINATION /etc)


