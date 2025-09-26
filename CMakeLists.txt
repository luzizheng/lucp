cmake_minimum_required(VERSION 3.10)
project(lucp)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2")

# 配置解析库
add_subdirectory(third_party/lucfg)

# lucp协议解析库
add_subdirectory(lucplib)

# lucp服务器守护进程
add_subdirectory(lucpd)

# 测试程序
add_subdirectory(demo)