#include "../include/log_cli_api.h"
#include "../include/log_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>

// 客户端实例结构
typedef struct ClientNode{
    char *app_id;
    LogProtocolConfig *log_config;
    ConnectionPool *conn_pool;
    LogFramePool *frame_pool;
    LogQueue *log_buffer;       // 日志缓冲队列
    pthread_t flush_thread;     // 日志刷新线程
    pthread_t recv_thread;      // 服务端消息接收线程
    int running;                // 客户端运行状态
    pthread_mutex_t config_lock;// 配置保护锁
    
    // 日志级别配置
    DltLevel threshold_level;
    int fatal_mode;
    int error_mode;
    int warning_mode;
    int info_mode;
    int debug_mode;
    int verbose_mode;
    
    struct ClientNode *next;    // 链表节点
} ClientNode;

// 客户端链表（管理多个客户端实例）
static ClientNode *client_list = NULL;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

// 查找客户端实例
static ClientNode* find_client(const char *app_id) {
    if (!app_id) return NULL;
    
    pthread_mutex_lock(&list_lock);
    ClientNode *node = client_list;
    while (node) {
        if (strcmp(node->app_id, app_id) == 0) {
            pthread_mutex_unlock(&list_lock);
            return node;
        }
        node = node->next;
    }
    pthread_mutex_unlock(&list_lock);
    return NULL;
}

// 解析配置响应帧，更新客户端配置
static void update_client_config(ClientNode *client, LogFrame *frame) {
    if (!client || !frame) return;
    
    pthread_mutex_lock(&client->config_lock);
    
    // 解析阈值级别
    Tlv *threshold_tlv = log_frame_find_tlv(frame, TLV_THRESHOLD_LEVEL);
    if (threshold_tlv) {
        LogProtocolError err;
        client->threshold_level = tlv_get_level(threshold_tlv, &err);
    }
    
    // 解析各级别模式
    Tlv *tlv;
    if ((tlv = log_frame_find_tlv(frame, TLV_FATAL_MODE))) {
        client->fatal_mode = tlv_get_uint8(tlv, NULL) ? 1 : 0;
    }
    if ((tlv = log_frame_find_tlv(frame, TLV_ERROR_MODE))) {
        client->error_mode = tlv_get_uint8(tlv, NULL) ? 1 : 0;
    }
    if ((tlv = log_frame_find_tlv(frame, TLV_WARNING_MODE))) {
        client->warning_mode = tlv_get_uint8(tlv, NULL) ? 1 : 0;
    }
    if ((tlv = log_frame_find_tlv(frame, TLV_INFO_MODE))) {
        client->info_mode = tlv_get_uint8(tlv, NULL) ? 1 : 0;
    }
    if ((tlv = log_frame_find_tlv(frame, TLV_DEBUG_MODE))) {
        client->debug_mode = tlv_get_uint8(tlv, NULL) ? 1 : 0;
    }
    if ((tlv = log_frame_find_tlv(frame, TLV_VERBOSE_MODE))) {
        client->verbose_mode = tlv_get_uint8(tlv, NULL) ? 1 : 0;
    }
    
    pthread_mutex_unlock(&client->config_lock);
}

// 服务端消息接收线程
static void* server_message_receiver(void *arg) {
    ClientNode *client = (ClientNode*)arg;
    if (!client) return NULL;
    
    while (client->running) {
        // 从连接池获取连接
        int sockfd = connection_pool_acquire(client->conn_pool);
        if (sockfd < 0) {
            // 连接失败，等待后重试
            sleep(1);
            continue;
        }
        
        // 接收服务端消息（非阻塞方式）
        fd_set read_fds;
        struct timeval timeout = {1, 0}; // 1秒超时
        int ret;
        
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        
        ret = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (ret > 0 && FD_ISSET(sockfd, &read_fds)) {
            // 有数据可读，接收帧
            LogFrame *frame = log_protocol_recv_frame(sockfd, client->log_config);
            if (frame) {
                // 处理0x03类型的配置更新报文
                if (frame->header.mtyp == 0x03) {
                    update_client_config(client, frame);
                }
                log_frame_free(frame);
            }
        }
        
        // 将连接放回池
        connection_pool_release(client->conn_pool, sockfd);
    }
    
    return NULL;
}

// 日志刷新线程（处理缓冲的日志）
static void* log_flush_thread(void *arg) {
    ClientNode *client = (ClientNode*)arg;
    if (!client) return NULL;
    
    while (client->running) {
        // 最多批量处理100条日志
        LogFrame *batch_frame = log_protocol_create_batch_log_frame(
            client->log_config, 0); // 序列号在实际发送时由库管理
        
        if (!batch_frame) {
            usleep(100000); // 100ms后重试
            continue;
        }
        
        int log_count = 0;
        LogFrame *log_frame;
        
        // 从缓冲队列获取日志
        while (log_count < 100 && 
               (log_frame = log_queue_dequeue(client->log_buffer)) != NULL) {
            
            // 提取日志信息
            Tlv *level_tlv = log_frame_find_tlv(log_frame, TLV_ENTRY_LEVEL);
            Tlv *msg_tlv = log_frame_find_tlv(log_frame, TLV_LOG_MESSAGE);
            Tlv *ts_tlv = log_frame_find_tlv(log_frame, TLV_TIMESTAMP);
            
            if (level_tlv && msg_tlv && ts_tlv) {
                DltLevel level = tlv_get_level(level_tlv, NULL);
                const char *message = tlv_get_string(msg_tlv);
                LogTimestamp timestamp;
                tlv_get_timestamp(ts_tlv, &timestamp);
                
                // 添加到批量帧
                log_protocol_add_batch_log(batch_frame, level, message, &timestamp);
                log_count++;
            }
            
            log_frame_free(log_frame);
        }
        
        // 如果有日志需要发送
        if (log_count > 0) {
            // 序列化批量帧
            if (log_frame_serialize(batch_frame) == LOG_PROTOCOL_SUCCESS) {
                // 获取连接并发送
                int sockfd = connection_pool_acquire(client->conn_pool);
                if (sockfd >= 0) {
                    log_protocol_send_frame(sockfd, batch_frame);
                    connection_pool_release(client->conn_pool, sockfd);
                }
            }
        }
        
        log_frame_free(batch_frame);
        
        // 休眠100ms，或者如果队列中有更多日志则立即处理
        if (log_count < 100) {
            usleep(100000);
        }
    }
    
    return NULL;
}

// 检查日志级别是否允许发送
static int is_log_allowed(ClientNode *client, DltLevel level) {
    if (!client) return 0;
    
    pthread_mutex_lock(&client->config_lock);
    int allowed = 0;
    
    // 首先检查是否超过阈值级别
    if (level <= client->threshold_level) {
        // 再检查对应级别的模式是否开启
        switch (level) {
            case DLT_LEVEL_FATAL:
                allowed = client->fatal_mode;
                break;
            case DLT_LEVEL_ERROR:
                allowed = client->error_mode;
                break;
            case DLT_LEVEL_WARN:
                allowed = client->warning_mode;
                break;
            case DLT_LEVEL_INFO:
                allowed = client->info_mode;
                break;
            case DLT_LEVEL_DEBUG:
                allowed = client->debug_mode;
                break;
            case DLT_LEVEL_VERBOSE:
                allowed = client->verbose_mode;
                break;
            default:
                allowed = 0;
        }
    }
    
    pthread_mutex_unlock(&client->config_lock);
    return allowed;
}

// 通用日志发送函数
static int dlt_log_common(const char *app_id, DltLevel level, const char *format, va_list args) {
    if (!app_id || !format) return -1;
    
    // 查找客户端实例
    ClientNode *client = find_client(app_id);
    if (!client || !client->running) return -1;
    
    // 检查日志级别是否允许发送
    if (!is_log_allowed(client, level)) {
        return 0; // 不允许发送，返回成功避免应用层处理错误
    }
    
    // 格式化日志消息
    char log_msg[1024];
    vsnprintf(log_msg, sizeof(log_msg), format, args);
    log_msg[sizeof(log_msg) - 1] = '\0';
    
    // 从对象池获取日志帧
    LogFrame *frame = log_frame_pool_acquire(
        client->frame_pool, 
        client->log_config, 
        MTYP_SINGLE_LOG, 
        0,  // 序列号由库管理
        0
    );
    
    if (!frame) return -1;
    
    // 添加日志内容
    LogProtocolError err;
    err = log_frame_add_string_tlv(frame, TLV_APP_ID, app_id);
    if (err != LOG_PROTOCOL_SUCCESS) goto fail;
    
    err = log_frame_add_level_tlv(frame, TLV_ENTRY_LEVEL, level);
    if (err != LOG_PROTOCOL_SUCCESS) goto fail;
    
    LogTimestamp timestamp;
    log_protocol_get_current_timestamp(&timestamp);
    err = log_frame_add_timestamp_tlv(frame, &timestamp);
    if (err != LOG_PROTOCOL_SUCCESS) goto fail;
    
    err = log_frame_add_string_tlv(frame, TLV_LOG_MESSAGE, log_msg);
    if (err != LOG_PROTOCOL_SUCCESS) goto fail;
    
    // 序列化帧
    if (log_frame_serialize(frame) != LOG_PROTOCOL_SUCCESS) goto fail;
    
    // 将日志帧加入缓冲队列
    if (log_queue_enqueue(client->log_buffer, frame) != LOG_PROTOCOL_SUCCESS) {
        // 队列满，直接发送
        int sockfd = connection_pool_acquire(client->conn_pool);
        if (sockfd >= 0) {
            log_protocol_send_frame(sockfd, frame);
            connection_pool_release(client->conn_pool, sockfd);
        }
        log_frame_pool_release(client->frame_pool, frame);
    }
    
    return 0;
    
fail:
    log_frame_pool_release(client->frame_pool, frame);
    return -1;
}

int dlt_init_client(const char *app_id) {
    if (!app_id || find_client(app_id)) {
        return -1; // 已存在该客户端
    }
    
    // 创建客户端节点
    ClientNode *client = (ClientNode*)malloc(sizeof(ClientNode));
    if (!client) return -1;
    
    memset(client, 0, sizeof(ClientNode));
    client->app_id = strdup(app_id);
    if (!client->app_id) {
        free(client);
        return -1;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&client->config_lock, NULL) != 0) {
        free(client->app_id);
        free(client);
        return -1;
    }
    
    // 默认日志级别配置
    client->threshold_level = DLT_LEVEL_INFO;
    client->fatal_mode = 1;
    client->error_mode = 1;
    client->warning_mode = 1;
    client->info_mode = 1;
    client->debug_mode = 0;
    client->verbose_mode = 0;
    
    // 初始化日志协议配置（假设服务器地址和端口通过环境变量或配置文件获取）
    // 实际应用中应从配置获取服务器地址和端口
    const char *server_ip = getenv("DLT_SERVER_IP") ? getenv("DLT_SERVER_IP") : "127.0.0.1";
    uint16_t server_port = getenv("DLT_SERVER_PORT") ? 
        atoi(getenv("DLT_SERVER_PORT")) : 5000;
    
    client->log_config = log_protocol_config_init(
        server_ip, 
        server_port, 
        app_id, 
        client->threshold_level
    );
    
    if (!client->log_config) goto fail;
    
    // 初始化连接池（3个连接）
    client->conn_pool = connection_pool_init(3, client->log_config);
    if (!client->conn_pool) goto fail;
    
    // 初始化帧对象池
    client->frame_pool = log_frame_pool_init();
    if (!client->frame_pool) goto fail;
    
    // 初始化日志缓冲队列
    client->log_buffer = log_queue_init();
    if (!client->log_buffer) goto fail;
    
    // 发送0x01初始化报文
    int sockfd = log_protocol_connect(client->log_config);
    if (sockfd < 0) goto fail;
    
    LogFrame *init_frame = log_frame_create(
        client->log_config, 
        0x01,  // MessageType为0x01
        1,     // 序列号
        0
    );
    
    if (!init_frame) {
        log_protocol_disconnect(sockfd);
        goto fail;
    }
    
    // 添加AppId TLV
    if (log_frame_add_string_tlv(init_frame, TLV_APP_ID, app_id) != LOG_PROTOCOL_SUCCESS ||
        log_frame_serialize(init_frame) != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(init_frame);
        log_protocol_disconnect(sockfd);
        goto fail;
    }
    
    // 发送初始化帧
    LogProtocolError err = log_protocol_send_frame(sockfd, init_frame);
    log_frame_free(init_frame);
    
    if (err != LOG_PROTOCOL_SUCCESS) {
        log_protocol_disconnect(sockfd);
        goto fail;
    }
    
    // 等待服务器响应
    LogFrame *response = log_protocol_recv_frame(sockfd, client->log_config);
    log_protocol_disconnect(sockfd);
    
    if (!response) goto fail;
    
    // 检查响应类型
    int init_success = 0;
    if (response->header.mtyp == 0x02) {
        // 初始化成功，更新配置
        update_client_config(client, response);
        init_success = 1;
    } else if (response->header.mtyp == 0x03) {
        // 应用未注册，初始化失败
        init_success = 0;
    }
    
    log_frame_free(response);
    if (!init_success) goto fail;
    
    // 启动日志刷新线程
    client->running = 1;
    if (pthread_create(&client->flush_thread, NULL, log_flush_thread, client) != 0) {
        goto fail;
    }
    
    // 启动服务端消息接收线程
    if (pthread_create(&client->recv_thread, NULL, server_message_receiver, client) != 0) {
        pthread_join(client->flush_thread, NULL);
        goto fail;
    }
    
    // 将客户端添加到链表
    pthread_mutex_lock(&list_lock);
    client->next = client_list;
    client_list = client;
    pthread_mutex_unlock(&list_lock);
    
    return 0;
    
fail:
    if (client->log_config) log_protocol_config_free(client->log_config);
    if (client->conn_pool) connection_pool_destroy(client->conn_pool);
    if (client->frame_pool) log_frame_pool_destroy(client->frame_pool);
    if (client->log_buffer) log_queue_destroy(client->log_buffer);
    pthread_mutex_destroy(&client->config_lock);
    free(client->app_id);
    free(client);
    return -1;
}

int dlt_free_client(const char *app_id) {
    if (!app_id) return -1;
    
    pthread_mutex_lock(&list_lock);
    ClientNode *prev = NULL;
    ClientNode *node = client_list;
    
    // 查找客户端
    while (node) {
        if (strcmp(node->app_id, app_id) == 0) break;
        prev = node;
        node = node->next;
    }
    
    if (!node) {
        pthread_mutex_unlock(&list_lock);
        return -1;
    }
    
    // 从链表移除
    if (prev) {
        prev->next = node->next;
    } else {
        client_list = node->next;
    }
    pthread_mutex_unlock(&list_lock);
    
    // 停止线程
    node->running = 0;
    pthread_join(node->flush_thread, NULL);
    pthread_join(node->recv_thread, NULL);
    
    // 释放资源
    if (node->log_config) log_protocol_config_free(node->log_config);
    if (node->conn_pool) connection_pool_destroy(node->conn_pool);
    if (node->frame_pool) log_frame_pool_destroy(node->frame_pool);
    if (node->log_buffer) log_queue_destroy(node->log_buffer);
    pthread_mutex_destroy(&node->config_lock);
    free(node->app_id);
    free(node);
    
    return 0;
}

int dlt_log_verbose(const char *app_id, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = dlt_log_common(app_id, DLT_LEVEL_VERBOSE, format, args);
    va_end(args);
    return ret;
}

int dlt_log_debug(const char *app_id, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = dlt_log_common(app_id, DLT_LEVEL_DEBUG, format, args);
    va_end(args);
    return ret;
}

int dlt_log_info(const char *app_id, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = dlt_log_common(app_id, DLT_LEVEL_INFO, format, args);
    va_end(args);
    return ret;
}

int dlt_log_warn(const char *app_id, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = dlt_log_common(app_id, DLT_LEVEL_WARN, format, args);
    va_end(args);
    return ret;
}

int dlt_log_error(const char *app_id, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = dlt_log_common(app_id, DLT_LEVEL_ERROR, format, args);
    va_end(args);
    return ret;
}

int dlt_log_fatal(const char *app_id, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = dlt_log_common(app_id, DLT_LEVEL_FATAL, format, args);
    va_end(args);
    return ret;
}
