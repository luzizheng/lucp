#ifndef DLT_SERVER_H
#define DLT_SERVER_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "../include/log_protocol.h"
#include "../include/logdef.h"
#include "logd_cfg.h"
#include <netinet/in.h>

// 客户端连接信息
typedef struct
{
    int sockfd;                  // 客户端socket
    char app_id[32];             // 应用ID
    int connected;               // 连接状态
    pthread_t thread;            // 处理线程
    pthread_mutex_t send_lock;   // 发送锁
    struct sockaddr_in client_addr; // 客户端地址
} ClientConnection;

// 服务端上下文
typedef struct
{
    int server_fd;               // 服务端socket
    int port;                    // 监听端口
    int running;                 // 运行状态
    ClientConnection clients[20]; // 客户端连接数组
    pthread_mutex_t clients_lock; // 客户端数组锁
} DltServer;


// 服务端函数声明
DltServer* dlt_server_init(DltGeneralCfg *cfg);
int dlt_server_start(DltServer *server);
void dlt_server_stop(DltServer *server);
void dlt_server_destroy(DltServer *server);

#endif // DLT_SERVER_H
