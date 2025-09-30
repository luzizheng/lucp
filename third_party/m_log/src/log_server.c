#include "log_server.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>




// 创建配置响应帧（0x02类型）
static LogFrame* create_config_response_frame(DltAppCfg *app_cfg, uint16_t sequence) {
    if (!app_cfg) return NULL;
    
    // 创建日志协议配置
    LogProtocolConfig *config = log_protocol_config_init(
        "127.0.0.1", 0, app_cfg->appId, 
        app_cfg->treshold_level
    );
    
    if (!config) return NULL;
    
    // 创建响应帧（0x02类型）
    LogFrame *frame = log_frame_create(config, 0x02, sequence, 0);
    if (!frame) {
        log_protocol_config_free(config);
        return NULL;
    }
    
    // 添加App ID
    log_frame_add_string_tlv(frame, TLV_APP_ID, app_cfg->appId);
    
    // 添加阈值级别
    log_frame_add_level_tlv(frame, TLV_THRESHOLD_LEVEL, 
                           app_cfg->treshold_level);
    
    // 添加各级别模式配置
    log_frame_add_uint8_tlv(frame, TLV_FATAL_MODE, 
                           (app_cfg->level_info[DLT_LEVEL_FATAL].mode != DLT_LOG_MODE_NONE) ? 1 : 0);
    log_frame_add_uint8_tlv(frame, TLV_ERROR_MODE, 
                           (app_cfg->level_info[DLT_LEVEL_ERROR].mode != DLT_LOG_MODE_NONE) ? 1 : 0);
    log_frame_add_uint8_tlv(frame, TLV_WARNING_MODE, 
                           (app_cfg->level_info[DLT_LEVEL_WARN].mode != DLT_LOG_MODE_NONE) ? 1 : 0);
    log_frame_add_uint8_tlv(frame, TLV_INFO_MODE, 
                           (app_cfg->level_info[DLT_LEVEL_INFO].mode != DLT_LOG_MODE_NONE) ? 1 : 0);
    log_frame_add_uint8_tlv(frame, TLV_DEBUG_MODE, 
                           (app_cfg->level_info[DLT_LEVEL_DEBUG].mode != DLT_LOG_MODE_NONE) ? 1 : 0);
    log_frame_add_uint8_tlv(frame, TLV_VERBOSE_MODE, 
                           (app_cfg->level_info[DLT_LEVEL_VERBOSE].mode != DLT_LOG_MODE_NONE) ? 1 : 0);
    
    // 序列化帧
    log_frame_serialize(frame);
    
    // 释放配置，帧已经包含所需信息
    log_protocol_config_free(config);
    
    return frame;
}

// 创建未注册响应帧（0x03类型）
static LogFrame* create_unregistered_response_frame(const char *app_id, uint16_t sequence) {
    // 创建日志协议配置
    LogProtocolConfig *config = log_protocol_config_init(
        "127.0.0.1", 0, app_id, DLT_LEVEL_FATAL
    );
    
    if (!config) return NULL;
    
    // 创建响应帧（0x03类型）
    LogFrame *frame = log_frame_create(config, 0x03, sequence, 0);
    if (!frame) {
        log_protocol_config_free(config);
        return NULL;
    }
    
    // 添加App ID和状态消息
    log_frame_add_string_tlv(frame, TLV_APP_ID, app_id);
    log_frame_add_string_tlv(frame, TLV_STATUS_MESSAGE, "Application not registered");
    
    // 序列化帧
    log_frame_serialize(frame);
    
    // 释放配置
    log_protocol_config_free(config);
    
    return frame;
}

// 向客户端发送帧
static int send_frame_to_client(ClientConnection *client, LogFrame *frame) {
    if (!client || !frame || !client->connected) return -1;
    
    pthread_mutex_lock(&client->send_lock);
    
    // 发送帧
    int result = (log_protocol_send_frame(client->sockfd, frame) == LOG_PROTOCOL_SUCCESS) ? 0 : -1;
    
    pthread_mutex_unlock(&client->send_lock);
    return result;
}

// 处理客户端初始化请求（0x01类型报文）
static int handle_init_request(ClientConnection *client, LogFrame *frame) {
    if (!client || !frame) return -1;
    
    // 从帧中获取App ID
    Tlv *app_id_tlv = log_frame_find_tlv(frame, TLV_APP_ID);
    if (!app_id_tlv) return -1;
    
    const char *app_id = tlv_get_string(app_id_tlv);
    if (!app_id) return -1;
    
    // 保存App ID
    strncpy(client->app_id, app_id, sizeof(client->app_id) - 1);
    
    // 检查App是否注册
    DltAppCfg *app_cfg = dlt_get_appcfg(app_id);
    LogFrame *response = NULL;
    
    if (app_cfg) {
        // App已注册，发送0x02配置响应
        response = create_config_response_frame(app_cfg, frame->header.sequence);
    } else {
        // App未注册，发送0x03响应
        response = create_unregistered_response_frame(app_id, frame->header.sequence);
    }
    
    if (!response) return -1;
    
    // 发送响应
    int result = send_frame_to_client(client, response);
    log_frame_free(response);
    
    // 如果App未注册，关闭连接
    if (!app_cfg) {
        client->connected = 0;
    }
    
    return result;
}

// 处理客户端发送的日志消息
static int handle_log_message(ClientConnection *client, LogFrame *frame) {
    if (!client || !frame) return -1;
    
    // 根据配置决定如何处理日志（此处不实现具体日志写入）
    // 实际应用中应根据g_dlt_general_cfg和app配置处理日志
    
    // 检查消息类型
    switch (frame->header.mtyp) {
        case MTYP_SINGLE_LOG:
            // 处理单条日志
            // ...
            break;
            
        case MTYP_MULTIPLE_LOGS:
            // 处理批量日志
            // ...
            break;
            
        default:
            // 未知消息类型
            return -1;
    }
    
    return 0;
}

// 客户端处理线程
static void* client_handler(void *arg) {
    ClientConnection *client = (ClientConnection*)arg;
    if (!client || client->sockfd < 0) return NULL;
    
    // 创建日志协议配置
    LogProtocolConfig *config = log_protocol_config_init(
        inet_ntoa(client->client_addr.sin_addr), 
        ntohs(client->client_addr.sin_port),
        "", DLT_LEVEL_VERBOSE
    );
    
    if (!config) {
        close(client->sockfd);
        client->connected = 0;
        return NULL;
    }
    
    // 客户端消息处理循环
    while (client->connected) {
        // 接收客户端帧
        LogFrame *frame = log_protocol_recv_frame(client->sockfd, config);
        if (!frame) {
            // 接收失败，可能连接已关闭
            break;
        }
        
        // 根据消息类型处理
        switch (frame->header.mtyp) {
            case 0x01:
                // 处理初始化请求
                handle_init_request(client, frame);
                break;
                
            case MTYP_SINGLE_LOG:
            case MTYP_MULTIPLE_LOGS:
                // 处理日志消息
                handle_log_message(client, frame);
                break;
                
            default:
                // 未知消息类型，忽略
                break;
        }
        
        log_frame_free(frame);
    }
    
    // 清理
    log_protocol_config_free(config);
    close(client->sockfd);
    client->connected = 0;
    printf("Client %s disconnected\n", client->app_id);
    
    return NULL;
}

// 查找空闲的客户端连接槽位
static int find_free_client_slot(DltServer *server) {
    if (!server) return -1;
    
    pthread_mutex_lock(&server->clients_lock);
    
    for (int i = 0; i < 20; i++) {
        if (!server->clients[i].connected) {
            pthread_mutex_unlock(&server->clients_lock);
            return i;
        }
    }
    
    pthread_mutex_unlock(&server->clients_lock);
    return -1; // 没有空闲槽位
}

// 接受客户端连接
static void* connection_acceptor(void *arg) {
    DltServer *server = (DltServer*)arg;
    if (!server || server->server_fd < 0) return NULL;
    
    printf("Server started, listening on port %d\n", server->port);
    
    while (server->running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // 接受客户端连接
        int client_fd = accept(server->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (server->running) {
                perror("accept failed");
            }
            continue;
        }
        
        // 查找空闲槽位
        int slot = find_free_client_slot(server);
        if (slot < 0) {
            printf("Max clients reached, rejecting new connection\n");
            close(client_fd);
            continue;
        }
        
        // 初始化客户端连接
        pthread_mutex_lock(&server->clients_lock);
        
        ClientConnection *client = &server->clients[slot];
        client->sockfd = client_fd;
        client->connected = 1;
        client->client_addr = client_addr;
        memset(client->app_id, 0, sizeof(client->app_id));
        pthread_mutex_init(&client->send_lock, NULL);
        
        // 启动客户端处理线程
        if (pthread_create(&client->thread, NULL, client_handler, client) != 0) {
            perror("Failed to create client thread");
            close(client_fd);
            client->connected = 0;
            pthread_mutex_destroy(&client->send_lock);
        } else {
            printf("New client connected from %s:%d (slot %d)\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port),
                   slot);
        }
        
        pthread_mutex_unlock(&server->clients_lock);
    }
    
    return NULL;
}

// 初始化服务端
DltServer* dlt_server_init(DltGeneralCfg *cfg){
    // 初始化信号处理（忽略PIPE信号，避免写入已关闭连接导致崩溃）
    signal(SIGPIPE, SIG_IGN);
    
    // 分配服务端结构
    DltServer *server = (DltServer*)malloc(sizeof(DltServer));
    if (!server) return NULL;
    
    memset(server, 0, sizeof(DltServer));
    int port = cfg ? cfg->server_port : DLT_DEFAULT_SERVER_PORT;
    server->port = port;
    server->running = 0;
    
    // 初始化客户端数组和锁
    pthread_mutex_init(&server->clients_lock, NULL);
    for (int i = 0; i < 20; i++) {
        server->clients[i].sockfd = -1;
        server->clients[i].connected = 0;
        memset(server->clients[i].app_id, 0, sizeof(server->clients[i].app_id));
    }
    
    // 创建服务器socket
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        perror("socket creation failed");
        dlt_server_destroy(server);
        return NULL;
    }
    
    // 设置socket选项，允许地址重用
    int opt = 1;
    if (setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        dlt_server_destroy(server);
        return NULL;
    }
    
    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server->server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        dlt_server_destroy(server);
        return NULL;
    }
    
    // 开始监听
    if (listen(server->server_fd, 5) < 0) {
        perror("listen failed");
        dlt_server_destroy(server);
        return NULL;
    }
    
    return server;
}

// 启动服务端
int dlt_server_start(DltServer *server) {
    if (!server || server->running) return -1;
    
    // 加载配置
    if (dlt_load_cfg("dlt_server.cfg", 0) != 0) {
        fprintf(stderr, "Warning: Failed to load configuration file\n");
    }
    
    server->running = 1;
    
    // 启动连接接受线程
    pthread_t acceptor_thread;
    if (pthread_create(&acceptor_thread, NULL, connection_acceptor, server) != 0) {
        perror("Failed to create acceptor thread");
        server->running = 0;
        return -1;
    }
    
    // 等待接受线程结束（通常不会返回，除非服务器停止）
    pthread_join(acceptor_thread, NULL);
    
    return 0;
}

// 停止服务端
void dlt_server_stop(DltServer *server) {
    if (!server || !server->running) return;
    
    server->running = 0;
    
    // 关闭服务器socket，唤醒accept
    if (server->server_fd >= 0) {
        close(server->server_fd);
        server->server_fd = -1;
    }
    
    // 关闭所有客户端连接
    pthread_mutex_lock(&server->clients_lock);
    for (int i = 0; i < 20; i++) {
        if (server->clients[i].connected) {
            server->clients[i].connected = 0;
            close(server->clients[i].sockfd);
            pthread_join(server->clients[i].thread, NULL);
            pthread_mutex_destroy(&server->clients[i].send_lock);
        }
    }
    pthread_mutex_unlock(&server->clients_lock);
}

// 销毁服务端
void dlt_server_destroy(DltServer *server) {
    if (!server) return;
    
    dlt_server_stop(server);
    pthread_mutex_destroy(&server->clients_lock);
    free(server);
}
