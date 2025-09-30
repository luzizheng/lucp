#include "../include/log_protocol.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

// 初始化日志协议配置
LogProtocolConfig* log_protocol_config_init(const char *server_ip, uint16_t server_port, 
                                           const char *app_id, DltLevel threshold_level) {
    if (!server_ip || !app_id) {
        return NULL;
    }
    
    LogProtocolConfig *config = (LogProtocolConfig*)malloc(sizeof(LogProtocolConfig));
    if (!config) {
        return NULL;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&config->lock, NULL) != 0) {
        free(config);
        return NULL;
    }
    
    config->server_ip = strdup(server_ip);
    config->server_port = server_port;
    config->app_id = strdup(app_id);
    config->threshold_level = threshold_level;
    config->timeout_ms = 5000;  // 默认5秒超时
    config->max_frame_size = LOG_PROTOCOL_DEFAULT_MAX_FRAME_SIZE;
    config->auto_reconnect = 1;  // 默认开启自动重连
    config->shutdown = 0;
    config->queue = NULL;
    
    // 检查字符串复制是否成功
    if (!config->server_ip || !config->app_id) {
        log_protocol_config_free(config);
        return NULL;
    }
    
    return config;
}

// 释放日志协议配置
void log_protocol_config_free(LogProtocolConfig *config) {
    if (!config) return;
    
    // 停止异步发送线程
    log_protocol_stop_async_sender(config);
    
    free((void*)config->server_ip);
    free((void*)config->app_id);
    
    // 销毁队列
    if (config->queue) {
        log_queue_destroy(config->queue);
    }
    
    pthread_mutex_destroy(&config->lock);
    free(config);
}

// 线程安全的配置修改接口
LogProtocolError log_protocol_update_config(LogProtocolConfig *config, 
                                           DltLevel new_threshold,
                                           size_t new_max_frame_size) {
    if (!config) return LOG_PROTOCOL_INVALID_PARAM;
    
    pthread_mutex_lock(&config->lock);
    
    // 更新配置
    config->threshold_level = new_threshold;
    if (new_max_frame_size > 0 && new_max_frame_size >= sizeof(FrameHeader)) {
        config->max_frame_size = new_max_frame_size;
    }
    
    pthread_mutex_unlock(&config->lock);
    return LOG_PROTOCOL_SUCCESS;
}

// 创建并初始化日志帧
LogFrame* log_frame_create(LogProtocolConfig *config, MessageType mtyp, 
                          uint16_t sequence, uint8_t status_code) {
    if (!config) {
        return NULL;
    }
    
    LogFrame *frame = (LogFrame*)malloc(sizeof(LogFrame));
    if (!frame) {
        return NULL;
    }
    
    // 初始化所有字段为0
    memset(frame, 0, sizeof(LogFrame));
    
    // 设置最大帧大小
    frame->max_frame_size = config->max_frame_size;
    
    // 初始化头部
    frame->header.magic = htonl(LOG_PROTOCOL_MAGIC);
    frame->header.major_version = LOG_PROTOCOL_MAJOR_VERSION;
    frame->header.minor_version = LOG_PROTOCOL_MINOR_VERSION;
    frame->header.sequence = htons(sequence);
    
    // 设置当前时间
    LogTimestamp now;
    log_protocol_get_current_timestamp(&now);
    frame->header.timestamp_sec = htonll(now.seconds);
    frame->header.timestamp_ms = htons(now.milliseconds);
    
    frame->header.mtyp = mtyp;
    frame->header.status_code = status_code;
    frame->header.payload_len = 0;
    
    frame->tlvs = NULL;
    frame->tlv_count = 0;
    frame->tlv_capacity = 0;
    frame->raw_data = NULL;
    frame->raw_size = 0;
    
    return frame;
}

// 释放日志帧资源
void log_frame_free(LogFrame *frame) {
    if (!frame) return;
    
    // 释放所有TLV的值
    if (frame->tlvs) {
        for (uint32_t i = 0; i < frame->tlv_count; i++) {
            free(frame->tlvs[i].value);
            frame->tlvs[i].value = NULL;  // 防止野指针
        }
        free(frame->tlvs);
        frame->tlvs = NULL;
    }
    
    free(frame->raw_data);
    frame->raw_data = NULL;
    free(frame);
}

// 向日志帧添加TLV
LogProtocolError log_frame_add_tlv(LogFrame *frame, TlvType type, const uint8_t *value, uint16_t length) {
    if (!frame || (length > 0 && !value)) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    
    // 检查单个TLV是否超过最大大小
    if (length > LOG_PROTOCOL_MAX_TLV_SIZE) {
        return LOG_PROTOCOL_FRAME_TOO_LARGE;
    }
    
    // 计算添加此TLV后的总payload长度
    uint32_t new_payload_len = frame->header.payload_len + (1 + 2 + length);
    size_t header_size = sizeof(FrameHeader);
    
    // 检查是否超过最大帧大小限制
    if (header_size + new_payload_len > frame->max_frame_size) {
        return LOG_PROTOCOL_FRAME_TOO_LARGE;
    }
    
    // 检查payload_len是否溢出
    if (new_payload_len < frame->header.payload_len) {
        return LOG_PROTOCOL_FRAME_TOO_LARGE;
    }
    
    // 预分配优化：当TLV数量达到当前容量时，翻倍扩容（减少realloc次数）
    if (frame->tlv_count >= frame->tlv_capacity) {
        uint32_t new_capacity = (frame->tlv_capacity == 0) ? TLV_PREALLOC_COUNT : frame->tlv_capacity * 2;
        Tlv *new_tlvs = (Tlv*)realloc(frame->tlvs, new_capacity * sizeof(Tlv));
        if (!new_tlvs) {
            return LOG_PROTOCOL_MEMORY_ERROR;
        }
        frame->tlvs = new_tlvs;
        frame->tlv_capacity = new_capacity;
    }
    
    // 初始化新的TLV（使用预分配的空间）
    Tlv *new_tlv = &frame->tlvs[frame->tlv_count];
    memset(new_tlv, 0, sizeof(Tlv));
    new_tlv->type = type;
    new_tlv->length = length;
    
    // 分配并复制值
    if (length > 0) {
        new_tlv->value = (uint8_t*)malloc(length);
        if (!new_tlv->value) {
            return LOG_PROTOCOL_MEMORY_ERROR;
        }
        memcpy(new_tlv->value, value, length);
    } else {
        new_tlv->value = NULL;
    }
    
    frame->tlv_count++;
    frame->header.payload_len = new_payload_len;
    
    return LOG_PROTOCOL_SUCCESS;
}

// 添加字符串类型的TLV
LogProtocolError log_frame_add_string_tlv(LogFrame *frame, TlvType type, const char *str) {
    if (!frame || !str) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    
    // 检查字符串长度，确保不会超过TLV的最大长度
    size_t str_len = strlen(str);
    if (str_len >= LOG_PROTOCOL_MAX_TLV_SIZE) {
        return LOG_PROTOCOL_FRAME_TOO_LARGE;
    }
    
    return log_frame_add_tlv(frame, type, (const uint8_t*)str, (uint16_t)(str_len + 1));
}

// 添加整数类型的TLV (1字节)
LogProtocolError log_frame_add_uint8_tlv(LogFrame *frame, TlvType type, uint8_t value) {
    if (!frame) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    return log_frame_add_tlv(frame, type, &value, 1);
}

// 添加日志级别TLV
LogProtocolError log_frame_add_level_tlv(LogFrame *frame, TlvType type, DltLevel level) {
    if (level < DLT_LEVEL_FATAL || level > DLT_LEVEL_VERBOSE) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    return log_frame_add_uint8_tlv(frame, type, (uint8_t)level);
}

// 添加时间戳类型的TLV
LogProtocolError log_frame_add_timestamp_tlv(LogFrame *frame, LogTimestamp *timestamp) {
    if (!frame || !timestamp) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    
    uint8_t buffer[10];  // 8字节秒 + 2字节毫秒
    uint64_t sec_net = htonll(timestamp->seconds);
    uint16_t ms_net = htons(timestamp->milliseconds);
    
    memcpy(buffer, &sec_net, 8);
    memcpy(buffer + 8, &ms_net, 2);
    
    return log_frame_add_tlv(frame, TLV_TIMESTAMP, buffer, 10);
}

// 序列化日志帧为网络传输格式
LogProtocolError log_frame_serialize(LogFrame *frame) {
    if (!frame) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    
    // 先验证TLV是否符合消息类型要求
    LogProtocolError err = log_frame_validate_tlvs(frame);
    if (err != LOG_PROTOCOL_SUCCESS) {
        return err;
    }
    
    // 计算总大小：头部大小 + 负载大小
    size_t header_size = sizeof(FrameHeader);
    size_t total_size = header_size + frame->header.payload_len;
    
    // 检查是否超过最大帧大小
    if (total_size > frame->max_frame_size) {
        return LOG_PROTOCOL_FRAME_TOO_LARGE;
    }
    
    // 检查是否有必要重新分配
    if (frame->raw_size != total_size) {
        uint8_t *new_raw_data = (uint8_t*)realloc(frame->raw_data, total_size);
        if (!new_raw_data) {
            return LOG_PROTOCOL_MEMORY_ERROR;
        }
        frame->raw_data = new_raw_data;
        frame->raw_size = total_size;
    }
    
    // 复制头部数据
    memcpy(frame->raw_data, &frame->header, header_size);
    
    // 复制TLV数据
    uint8_t *payload_ptr = frame->raw_data + header_size;
    const uint8_t *payload_end = frame->raw_data + total_size;
    
    for (uint32_t i = 0; i < frame->tlv_count; i++) {
        // 检查是否有足够空间
        if (payload_ptr + 1 + 2 + frame->tlvs[i].length > payload_end) {
            return LOG_PROTOCOL_SERIALIZE_ERROR;
        }
        
        // 写入Type
        *payload_ptr++ = frame->tlvs[i].type;
        
        // 写入Length (网络字节序)
        uint16_t length_net = htons(frame->tlvs[i].length);
        memcpy(payload_ptr, &length_net, 2);
        payload_ptr += 2;
        
        // 写入Value
        if (frame->tlvs[i].length > 0 && frame->tlvs[i].value) {
            memcpy(payload_ptr, frame->tlvs[i].value, frame->tlvs[i].length);
            payload_ptr += frame->tlvs[i].length;
        }
    }
    
    return LOG_PROTOCOL_SUCCESS;
}

// 辅助函数：判断是否为已知的TLV类型
static int is_known_tlv_type(TlvType type) {
    switch (type) {
        case TLV_STATUS_MESSAGE:
        case TLV_APP_ID:
        case TLV_LOG_MESSAGE:
        case TLV_THRESHOLD_LEVEL:
        case TLV_ENTRY_LEVEL:
        case TLV_TIMESTAMP:
        case TLV_FATAL_MODE:
        case TLV_ERROR_MODE:
        case TLV_WARNING_MODE:
        case TLV_INFO_MODE:
        case TLV_DEBUG_MODE:
        case TLV_VERBOSE_MODE:
            return 1;  // 已知类型
        default:
            return 0;  // 未知类型
    }
}

// 从原始数据解析日志帧
LogFrame* log_frame_parse(LogProtocolConfig *config, const uint8_t *data, size_t length) {
    if (!config || !data) {
        return NULL;
    }
    
    // 检查输入数据是否超过最大帧长
    if (length > config->max_frame_size) {
        return NULL;
    }
    
    // 检查头部是否完整
    size_t header_size = sizeof(FrameHeader);
    if (length < header_size) {
        return NULL;
    }
    
    // 分配日志帧
    LogFrame *frame = (LogFrame*)malloc(sizeof(LogFrame));
    if (!frame) {
        return NULL;
    }
    
    // 初始化成员
    memset(frame, 0, sizeof(LogFrame));
    frame->max_frame_size = config->max_frame_size;
    frame->tlvs = NULL;
    frame->tlv_count = 0;
    frame->tlv_capacity = 0;
    frame->raw_data = NULL;
    frame->raw_size = 0;
    
    // 复制并转换头部数据
    memcpy(&frame->header, data, header_size);
    
    // 转换网络字节序到主机字节序
    frame->header.magic = ntohl(frame->header.magic);
    frame->header.sequence = ntohs(frame->header.sequence);
    frame->header.timestamp_sec = ntohll(frame->header.timestamp_sec);
    frame->header.timestamp_ms = ntohs(frame->header.timestamp_ms);
    frame->header.payload_len = ntohs(frame->header.payload_len);
    
    // 验证magic值
    if (frame->header.magic != LOG_PROTOCOL_MAGIC) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 验证版本号
    if (log_protocol_check_version(&frame->header) != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 验证总长度
    if (length != header_size + frame->header.payload_len) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 解析TLV数据
    const uint8_t *payload_ptr = data + header_size;
    const uint8_t *payload_end = payload_ptr + frame->header.payload_len;
    
    while (payload_ptr < payload_end) {
        // 读取Type
        if (payload_ptr + 1 > payload_end) {
            log_frame_free(frame);
            return NULL;
        }
        TlvType type = (TlvType)*payload_ptr++;
        
        // 读取Length
        if (payload_ptr + 2 > payload_end) {
            log_frame_free(frame);
            return NULL;
        }
        uint16_t tlv_length;
        memcpy(&tlv_length, payload_ptr, 2);
        tlv_length = ntohs(tlv_length);
        payload_ptr += 2;
        
        // 验证长度是否合理
        if (tlv_length > (payload_end - payload_ptr) || tlv_length > LOG_PROTOCOL_MAX_TLV_SIZE) {
            log_frame_free(frame);
            return NULL;
        }
        
        // 对未知类型的TLV，仅跳过不解析
        if (!is_known_tlv_type(type)) {
            payload_ptr += tlv_length;
            continue;
        }
        
        // 读取Value
        uint8_t *value = NULL;
        if (tlv_length > 0) {
            value = (uint8_t*)malloc(tlv_length);
            if (!value) {
                log_frame_free(frame);
                return NULL;
            }
            memcpy(value, payload_ptr, tlv_length);
        }
        
        payload_ptr += tlv_length;
        
        // 添加到TLV列表
        LogProtocolError err = log_frame_add_tlv(frame, type, value, tlv_length);
        free(value);  // log_frame_add_tlv已经复制了数据
        
        if (err != LOG_PROTOCOL_SUCCESS) {
            log_frame_free(frame);
            return NULL;
        }
    }
    
    // 验证TLV是否符合消息类型要求
    if (log_frame_validate_tlvs(frame) != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 保存原始数据副本
    frame->raw_data = (uint8_t*)malloc(length);
    if (frame->raw_data) {
        memcpy(frame->raw_data, data, length);
        frame->raw_size = length;
    } else {
        // 即使原始数据保存失败，帧本身仍然有效
        frame->raw_size = 0;
    }
    
    return frame;
}

// 查找指定类型的TLV
Tlv* log_frame_find_tlv(LogFrame *frame, TlvType type) {
    if (!frame) return NULL;
    
    for (uint32_t i = 0; i < frame->tlv_count; i++) {
        if (frame->tlvs[i].type == type) {
            return &frame->tlvs[i];
        }
    }
    
    return NULL;
}

// 验证帧中的TLV是否符合当前消息类型的要求
LogProtocolError log_frame_validate_tlvs(LogFrame *frame) {
    if (!frame) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    
    // 记录必须的TLV类型
    TlvType required_tlvs[10];
    uint32_t required_count = 0;
    
    // 根据消息类型确定必须的TLV
    switch (frame->header.mtyp) {
        case MTYP_REQUEST_CONFIG:
            required_tlvs[required_count++] = TLV_APP_ID;
            break;
            
        case MTYP_CONFIG_STATUS:
            required_tlvs[required_count++] = TLV_STATUS_MESSAGE;
            required_tlvs[required_count++] = TLV_APP_ID;
            required_tlvs[required_count++] = TLV_THRESHOLD_LEVEL;
            required_tlvs[required_count++] = TLV_FATAL_MODE;
            required_tlvs[required_count++] = TLV_ERROR_MODE;
            required_tlvs[required_count++] = TLV_WARNING_MODE;
            required_tlvs[required_count++] = TLV_INFO_MODE;
            required_tlvs[required_count++] = TLV_DEBUG_MODE;
            required_tlvs[required_count++] = TLV_VERBOSE_MODE;
            break;
            
        case MTYP_PURE_STATUS:
            required_tlvs[required_count++] = TLV_APP_ID;
            required_tlvs[required_count++] = TLV_STATUS_MESSAGE;
            break;
            
        case MTYP_UPDATE_CONFIG:
            required_tlvs[required_count++] = TLV_APP_ID;
            required_tlvs[required_count++] = TLV_THRESHOLD_LEVEL;
            required_tlvs[required_count++] = TLV_FATAL_MODE;
            required_tlvs[required_count++] = TLV_ERROR_MODE;
            required_tlvs[required_count++] = TLV_WARNING_MODE;
            required_tlvs[required_count++] = TLV_INFO_MODE;
            required_tlvs[required_count++] = TLV_DEBUG_MODE;
            required_tlvs[required_count++] = TLV_VERBOSE_MODE;
            break;
            
        case MTYP_SINGLE_LOG:
            required_tlvs[required_count++] = TLV_APP_ID;
            required_tlvs[required_count++] = TLV_ENTRY_LEVEL;
            required_tlvs[required_count++] = TLV_TIMESTAMP;
            required_tlvs[required_count++] = TLV_LOG_MESSAGE;
            break;
            
        case MTYP_MULTIPLE_LOGS:
            // 第一个必须是AppId
            if (frame->tlv_count == 0 || frame->tlvs[0].type != TLV_APP_ID) {
                return LOG_PROTOCOL_TLV_INVALID;
            }
            
            // 检查剩余的TLV是否按3个一组(EntryLevel+TimeStamp+LogMessage)出现
            if ((frame->tlv_count - 1) % 3 != 0) {
                return LOG_PROTOCOL_TLV_INVALID;
            }
            
            for (uint32_t i = 1; i < frame->tlv_count; i += 3) {
                if (i + 2 >= frame->tlv_count ||
                    frame->tlvs[i].type != TLV_ENTRY_LEVEL ||
                    frame->tlvs[i+1].type != TLV_TIMESTAMP ||
                    frame->tlvs[i+2].type != TLV_LOG_MESSAGE) {
                    return LOG_PROTOCOL_TLV_INVALID;
                }
            }
            break;
            
        case MTYP_UNDEFINED:
        default:
            return LOG_PROTOCOL_INVALID_FRAME;
    }
    
    // 检查必须的TLV是否存在
    for (uint32_t i = 0; i < required_count; i++) {
        if (!log_frame_find_tlv(frame, required_tlvs[i])) {
            return LOG_PROTOCOL_TLV_NOT_FOUND;
        }
    }
    
    // 验证特定TLV的格式（仅针对已知类型）
    for (uint32_t i = 0; i < frame->tlv_count; i++) {
        Tlv *tlv = &frame->tlvs[i];
        
        // 对已知类型进行格式验证
        switch (tlv->type) {
            case TLV_TIMESTAMP:
                if (tlv->length != 10) {
                    return LOG_PROTOCOL_TLV_INVALID;
                }
                break;
                
            case TLV_THRESHOLD_LEVEL:
            case TLV_ENTRY_LEVEL:
            case TLV_FATAL_MODE:
            case TLV_ERROR_MODE:
            case TLV_WARNING_MODE:
            case TLV_INFO_MODE:
            case TLV_DEBUG_MODE:
            case TLV_VERBOSE_MODE:
                if (tlv->length != 1) {
                    return LOG_PROTOCOL_TLV_INVALID;
                }
                break;
                
            // 其他类型不验证长度
            default:
                break;
        }
    }
    
    return LOG_PROTOCOL_SUCCESS;
}

// 从TLV中获取字符串值
const char* tlv_get_string(Tlv *tlv) {
    if (!tlv || tlv->length == 0 || !tlv->value) {
        return NULL;
    }
    
    // 确保字符串以NULL结尾
    if (tlv->value[tlv->length - 1] != '\0') {
        return NULL;
    }
    
    return (const char*)tlv->value;
}

// 从TLV中获取uint8_t值
uint8_t tlv_get_uint8(Tlv *tlv, LogProtocolError *err) {
    if (err) *err = LOG_PROTOCOL_SUCCESS;
    
    if (!tlv || tlv->length != 1 || !tlv->value) {
        if (err) *err = LOG_PROTOCOL_TLV_INVALID;
        return 0;
    }
    
    return tlv->value[0];
}

// 从TLV中获取日志级别
DltLevel tlv_get_level(Tlv *tlv, LogProtocolError *err) {
    uint8_t value = tlv_get_uint8(tlv, err);
    if (*err != LOG_PROTOCOL_SUCCESS) {
        return DLT_LEVEL_FATAL;
    }
    
    if (value > DLT_LEVEL_VERBOSE) {
        if (err) *err = LOG_PROTOCOL_TLV_INVALID;
        return DLT_LEVEL_FATAL;
    }
    
    return (DltLevel)value;
}

// 从TLV中获取时间戳值
LogProtocolError tlv_get_timestamp(Tlv *tlv, LogTimestamp *timestamp) {
    if (!tlv || !timestamp || tlv->length != 10 || !tlv->value) {
        return LOG_PROTOCOL_TLV_INVALID;
    }
    
    uint64_t sec_net;
    uint16_t ms_net;
    
    memcpy(&sec_net, tlv->value, 8);
    memcpy(&ms_net, tlv->value + 8, 2);
    
    timestamp->seconds = ntohll(sec_net);
    timestamp->milliseconds = ntohs(ms_net);
    
    return LOG_PROTOCOL_SUCCESS;
}

// 创建客户端连接
int log_protocol_connect(LogProtocolConfig *config) {
    if (!config) {
        return -1;
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }
    
    // 设置超时
    struct timeval timeout;
    timeout.tv_sec = config->timeout_ms / 1000;
    timeout.tv_usec = (config->timeout_ms % 1000) * 1000;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        close(sockfd);
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config->server_port);
    
    // 转换IP地址
    if (inet_pton(AF_INET, config->server_ip, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }
    
    // 连接服务器
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

// 关闭连接
void log_protocol_disconnect(int sockfd) {
    if (sockfd >= 0) {
        close(sockfd);
    }
}

// 发送日志帧
LogProtocolError log_protocol_send_frame(int sockfd, LogFrame *frame) {
    if (sockfd < 0 || !frame || !frame->raw_data || frame->raw_size == 0) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    
    // 先发送帧大小（4字节，网络字节序）
    uint32_t frame_size_net = htonl(frame->raw_size);
    ssize_t sent = send(sockfd, &frame_size_net, sizeof(frame_size_net), 0);
    if (sent != sizeof(frame_size_net)) {
        return LOG_PROTOCOL_SEND_ERROR;
    }
    
    // 发送帧数据
    sent = send(sockfd, frame->raw_data, frame->raw_size, 0);
    if (sent != (ssize_t)frame->raw_size) {
        return LOG_PROTOCOL_SEND_ERROR;
    }
    
    return LOG_PROTOCOL_SUCCESS;
}

// 接收日志帧
LogFrame* log_protocol_recv_frame(int sockfd, LogProtocolConfig *config) {
    if (sockfd < 0 || !config) {
        return NULL;
    }
    
    // 先接收帧大小（4字节，网络字节序）
    uint32_t frame_size_net;
    ssize_t recv_len = recv(sockfd, &frame_size_net, sizeof(frame_size_net), 0);
    if (recv_len != sizeof(frame_size_net)) {
        return NULL;
    }
    
    size_t frame_size = ntohl(frame_size_net);
    
    // 检查帧大小是否超过最大限制
    if (frame_size == 0 || frame_size > config->max_frame_size) {
        return NULL;
    }
    
    // 分配缓冲区接收帧数据
    uint8_t *buffer = (uint8_t*)malloc(frame_size);
    if (!buffer) {
        return NULL;
    }
    
    // 接收帧数据
    recv_len = recv(sockfd, buffer, frame_size, 0);
    if (recv_len != (ssize_t)frame_size) {
        free(buffer);
        return NULL;
    }
    
    // 解析帧数据
    LogFrame *frame = log_frame_parse(config, buffer, frame_size);
    free(buffer);
    
    return frame;
}

// 初始化连接池
ConnectionPool* connection_pool_init(uint32_t capacity, LogProtocolConfig *config) {
    if (capacity == 0 || !config) {
        return NULL;
    }
    
    ConnectionPool *pool = (ConnectionPool*)malloc(sizeof(ConnectionPool));
    if (!pool) {
        return NULL;
    }
    
    pool->sockets = (int*)malloc(sizeof(int) * capacity);
    if (!pool->sockets) {
        free(pool);
        return NULL;
    }
    
    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        free(pool->sockets);
        free(pool);
        return NULL;
    }
    
    pool->count = 0;
    pool->capacity = capacity;
    pool->config = config;
    
    return pool;
}

// 销毁连接池
void connection_pool_destroy(ConnectionPool *pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&pool->lock);
    
    // 关闭所有连接
    for (uint32_t i = 0; i < pool->count; i++) {
        log_protocol_disconnect(pool->sockets[i]);
    }
    
    pthread_mutex_unlock(&pool->lock);
    
    free(pool->sockets);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

// 从连接池获取连接
int connection_pool_acquire(ConnectionPool *pool) {
    if (!pool) return -1;
    
    pthread_mutex_lock(&pool->lock);
    
    int sockfd = -1;
    // 从池中获取可用连接
    if (pool->count > 0) {
        sockfd = pool->sockets[--pool->count];
    }
    
    pthread_mutex_unlock(&pool->lock);
    
    // 池中无可用连接，创建新连接
    if (sockfd < 0) {
        sockfd = log_protocol_connect(pool->config);
    }
    
    return sockfd;
}

// 释放连接到池
void connection_pool_release(ConnectionPool *pool, int sockfd) {
    if (!pool || sockfd < 0) return;
    
    pthread_mutex_lock(&pool->lock);
    
    // 连接池未满则放回，否则关闭
    if (pool->count < pool->capacity) {
        pool->sockets[pool->count++] = sockfd;
    } else {
        log_protocol_disconnect(sockfd);
    }
    
    pthread_mutex_unlock(&pool->lock);
}

// 初始化日志队列
LogQueue* log_queue_init() {
    LogQueue *queue = (LogQueue*)malloc(sizeof(LogQueue));
    if (!queue) return NULL;
    
    memset(queue, 0, sizeof(LogQueue));
    if (pthread_mutex_init(&queue->lock, NULL) != 0 ||
        pthread_cond_init(&queue->cond, NULL) != 0) {
        if (pthread_mutex_init(&queue->lock, NULL) == 0) {
            pthread_mutex_destroy(&queue->lock);
        }
        free(queue);
        return NULL;
    }
    queue->shutdown = 0;
    return queue;
}

// 销毁日志队列
void log_queue_destroy(LogQueue *queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->lock);
    queue->shutdown = 1;
    pthread_cond_broadcast(&queue->cond);  // 唤醒所有等待的线程
    pthread_mutex_unlock(&queue->lock);
    
    // 释放剩余的帧
    LogFrame *frame;
    while ((frame = log_queue_dequeue(queue)) != NULL) {
        log_frame_free(frame);
    }
    
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->cond);
    free(queue);
}

// 入队（非阻塞，队列满则丢弃并返回错误）
LogProtocolError log_queue_enqueue(LogQueue *queue, LogFrame *frame) {
    if (!queue || !frame) return LOG_PROTOCOL_INVALID_PARAM;
    
    pthread_mutex_lock(&queue->lock);
    
    // 队列满则返回错误
    if (queue->count >= 1024) {
        pthread_mutex_unlock(&queue->lock);
        return LOG_PROTOCOL_QUEUE_FULL;
    }
    
    queue->frames[queue->tail] = frame;
    queue->tail = (queue->tail + 1) % 1024;
    queue->count++;
    
    // 通知消费者
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->lock);
    
    return LOG_PROTOCOL_SUCCESS;
}

// 出队（阻塞直到有数据或关闭）
LogFrame* log_queue_dequeue(LogQueue *queue) {
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->lock);
    
    // 等待数据或关闭信号
    while (queue->count == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->cond, &queue->lock);
    }
    
    // 已关闭且队列空
    if (queue->shutdown && queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }
    
    LogFrame *frame = queue->frames[queue->head];
    queue->head = (queue->head + 1) % 1024;
    queue->count--;
    
    pthread_mutex_unlock(&queue->lock);
    return frame;
}

// 后台发送线程函数
void* log_sender_thread(void *arg) {
    LogProtocolConfig *config = (LogProtocolConfig*)arg;
    if (!config || !config->queue) return NULL;
    
    int sockfd = -1;
    // 尝试连接
    if (config->auto_reconnect) {
        sockfd = log_protocol_connect(config);
    }
    
    while (!config->shutdown) {
        LogFrame *frame = log_queue_dequeue(config->queue);
        if (!frame) break;  // 队列已关闭
        
        // 连接断开且需要自动重连
        if (sockfd < 0 && config->auto_reconnect) {
            sockfd = log_protocol_connect(config);
            if (sockfd < 0) {
                // 重连失败，等待后重试
                log_frame_free(frame);
                sleep(1);
                continue;
            }
        }
        
        // 发送帧
        if (sockfd >= 0) {
            LogProtocolError err = log_protocol_send_frame(sockfd, frame);
            if (err != LOG_PROTOCOL_SUCCESS) {
                // 发送失败，关闭连接下次重连
                log_protocol_disconnect(sockfd);
                sockfd = -1;
            }
        }
        
        log_frame_free(frame);
    }
    
    // 清理
    if (sockfd >= 0) {
        log_protocol_disconnect(sockfd);
    }
    return NULL;
}

// 启动异步发送线程
LogProtocolError log_protocol_start_async_sender(LogProtocolConfig *config) {
    if (!config) return LOG_PROTOCOL_INVALID_PARAM;
    
    pthread_mutex_lock(&config->lock);
    
    // 如果已经有队列或线程在运行，直接返回
    if (config->queue) {
        pthread_mutex_unlock(&config->lock);
        return LOG_PROTOCOL_SUCCESS;
    }
    
    // 创建队列
    config->queue = log_queue_init();
    if (!config->queue) {
        pthread_mutex_unlock(&config->lock);
        return LOG_PROTOCOL_MEMORY_ERROR;
    }
    
    // 启动发送线程
    config->shutdown = 0;
    if (pthread_create(&config->sender_thread, NULL, log_sender_thread, config) != 0) {
        log_queue_destroy(config->queue);
        config->queue = NULL;
        pthread_mutex_unlock(&config->lock);
        return LOG_PROTOCOL_MEMORY_ERROR;
    }
    
    pthread_mutex_unlock(&config->lock);
    return LOG_PROTOCOL_SUCCESS;
}

// 停止异步发送线程
void log_protocol_stop_async_sender(LogProtocolConfig *config) {
    if (!config) return;
    
    pthread_mutex_lock(&config->lock);
    
    if (config->queue) {
        config->shutdown = 1;
        // 等待线程结束
        if (pthread_join(config->sender_thread, NULL) != 0) {
            // 线程join失败，可能已经退出
        }
    }
    
    pthread_mutex_unlock(&config->lock);
}

// 初始化帧对象池
LogFramePool* log_frame_pool_init() {
    LogFramePool *pool = (LogFramePool*)malloc(sizeof(LogFramePool));
    if (!pool) return NULL;
    
    memset(pool, 0, sizeof(LogFramePool));
    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        free(pool);
        return NULL;
    }
    return pool;
}

// 销毁帧对象池
void log_frame_pool_destroy(LogFramePool *pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&pool->lock);
    
    // 释放所有帧
    for (uint32_t i = 0; i < pool->count; i++) {
        // 释放预分配的TLV数组
        free(pool->frames[i]->tlvs);
        free(pool->frames[i]);
    }
    
    pthread_mutex_unlock(&pool->lock);
    
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

// 从池获取帧（无可用帧则创建新的）
LogFrame* log_frame_pool_acquire(LogFramePool *pool, LogProtocolConfig *config, 
                                MessageType mtyp, uint16_t sequence, uint8_t status_code) {
    if (!pool || !config) return NULL;
    
    LogFrame *frame = NULL;
    pthread_mutex_lock(&pool->lock);
    
    // 从池中获取
    if (pool->count > 0) {
        frame = pool->frames[--pool->count];
        // 重置帧状态（比重新创建更高效）
        memset(&frame->header, 0, sizeof(FrameHeader));
        frame->header.magic = htonl(LOG_PROTOCOL_MAGIC);
        frame->header.major_version = LOG_PROTOCOL_MAJOR_VERSION;
        frame->header.minor_version = LOG_PROTOCOL_MINOR_VERSION;
        frame->header.sequence = htons(sequence);
        
        // 更新时间戳
        LogTimestamp now;
        log_protocol_get_current_timestamp(&now);
        frame->header.timestamp_sec = htonll(now.seconds);
        frame->header.timestamp_ms = htons(now.milliseconds);
        
        frame->header.mtyp = mtyp;
        frame->header.status_code = status_code;
        frame->header.payload_len = 0;
        frame->tlv_count = 0;
        frame->raw_size = 0;
        free(frame->raw_data);
        frame->raw_data = NULL;
    }
    pthread_mutex_unlock(&pool->lock);
    
    // 池中无可用帧，创建新的
    if (!frame) {
        frame = log_frame_create(config, mtyp, sequence, status_code);
        if (frame) {
            frame->tlv_capacity = TLV_PREALLOC_COUNT;  // 预分配TLV空间
            frame->tlvs = (Tlv*)malloc(frame->tlv_capacity * sizeof(Tlv));
            if (!frame->tlvs) {
                log_frame_free(frame);
                return NULL;
            }
        }
    }
    
    return frame;
}

// 回收帧到池（超过池大小则销毁）
void log_frame_pool_release(LogFramePool *pool, LogFrame *frame) {
    if (!pool || !frame) return;
    
    // 清空动态数据，但保留预分配的缓冲区
    for (uint32_t i = 0; i < frame->tlv_count; i++) {
        free(frame->tlvs[i].value);
        frame->tlvs[i].value = NULL;
    }
    frame->tlv_count = 0;
    free(frame->raw_data);
    frame->raw_data = NULL;
    frame->raw_size = 0;
    
    pthread_mutex_lock(&pool->lock);
    if (pool->count < FRAME_POOL_SIZE) {
        pool->frames[pool->count++] = frame;
    } else {
        // 池已满，直接销毁
        free(frame->tlvs);
        free(frame);
    }
    pthread_mutex_unlock(&pool->lock);
}

// 创建请求配置帧
LogFrame* log_protocol_create_config_request(LogProtocolConfig *config, uint16_t sequence) {
    if (!config) {
        return NULL;
    }
    
    // 创建帧
    LogFrame *frame = log_frame_create(config, MTYP_REQUEST_CONFIG, sequence, 0);
    if (!frame) {
        return NULL;
    }
    
    // 添加AppId TLV
    LogProtocolError err = log_frame_add_string_tlv(frame, TLV_APP_ID, config->app_id);
    if (err != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 序列化帧
    if (log_frame_serialize(frame) != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    return frame;
}

// 创建单条日志帧
LogFrame* log_protocol_create_single_log(LogProtocolConfig *config, uint16_t sequence,
                                        DltLevel level, const char *message) {
    if (!config || !message) {
        return NULL;
    }
    
    // 如果日志级别低于阈值，则不创建日志帧
    if (level > config->threshold_level) {
        return NULL;
    }
    
    // 创建帧
    LogFrame *frame = log_frame_create(config, MTYP_SINGLE_LOG, sequence, 0);
    if (!frame) {
        return NULL;
    }
    
    // 添加必要的TLV
    LogProtocolError err;
    
    // AppId
    err = log_frame_add_string_tlv(frame, TLV_APP_ID, config->app_id);
    if (err != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 日志级别
    err = log_frame_add_level_tlv(frame, TLV_ENTRY_LEVEL, level);
    if (err != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 时间戳
    LogTimestamp timestamp;
    log_protocol_get_current_timestamp(&timestamp);
    err = log_frame_add_timestamp_tlv(frame, &timestamp);
    if (err != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 日志消息
    err = log_frame_add_string_tlv(frame, TLV_LOG_MESSAGE, message);
    if (err != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    // 序列化帧
    if (log_frame_serialize(frame) != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    return frame;
}

// 创建批量日志帧
LogFrame* log_protocol_create_batch_log_frame(LogProtocolConfig *config, uint16_t sequence) {
    if (!config) {
        return NULL;
    }
    
    // 创建帧
    LogFrame *frame = log_frame_create(config, MTYP_MULTIPLE_LOGS, sequence, 0);
    if (!frame) {
        return NULL;
    }
    
    // 添加AppId TLV（批量日志帧必须第一个是AppId）
    LogProtocolError err = log_frame_add_string_tlv(frame, TLV_APP_ID, config->app_id);
    if (err != LOG_PROTOCOL_SUCCESS) {
        log_frame_free(frame);
        return NULL;
    }
    
    return frame;
}

// 向批量日志帧添加一条日志
LogProtocolError log_protocol_add_batch_log(LogFrame *batch_frame, DltLevel level, 
                                          const char *message, LogTimestamp *timestamp) {
    if (!batch_frame || !message || !timestamp || 
        batch_frame->header.mtyp != MTYP_MULTIPLE_LOGS) {
        return LOG_PROTOCOL_INVALID_PARAM;
    }
    
    LogProtocolError err;
    
    // 添加日志级别
    err = log_frame_add_level_tlv(batch_frame, TLV_ENTRY_LEVEL, level);
    if (err != LOG_PROTOCOL_SUCCESS) return err;
    
    // 添加时间戳
    err = log_frame_add_timestamp_tlv(batch_frame, timestamp);
    if (err != LOG_PROTOCOL_SUCCESS) return err;
    
    // 添加日志消息
    err = log_frame_add_string_tlv(batch_frame, TLV_LOG_MESSAGE, message);
    if (err != LOG_PROTOCOL_SUCCESS) return err;
    
    return LOG_PROTOCOL_SUCCESS;
}

// 获取当前时间戳
void log_protocol_get_current_timestamp(LogTimestamp *timestamp) {
    if (!timestamp) return;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    timestamp->seconds = tv.tv_sec;
    timestamp->milliseconds = (uint16_t)(tv.tv_usec / 1000);
}

// 日志级别转换为字符串
const char* log_level_to_string(DltLevel level) {
    // 使用局部数组而非静态变量，确保重入安全
    const char *level_strs[] = {
        "FATAL", "ERROR", "WARNING", "INFO", "DEBUG", "VERBOSE"
    };
    
    if (level < DLT_LEVEL_FATAL || level > DLT_LEVEL_VERBOSE) {
        return "UNKNOWN";
    }
    
    return level_strs[level];
}

// 版本兼容性检查
LogProtocolError log_protocol_check_version(FrameHeader *header) {
    if (!header) return LOG_PROTOCOL_INVALID_PARAM;
    
    // 主版本号必须完全匹配（不兼容的修改）
    if (header->major_version != LOG_PROTOCOL_MAJOR_VERSION) {
        return LOG_PROTOCOL_VERSION_MISMATCH;
    }
    
    // 次版本号可以不同（向前兼容的扩展）
    // 记录版本差异，便于后续处理
    if (header->minor_version != LOG_PROTOCOL_MINOR_VERSION) {
        // 可以在这里添加版本差异处理逻辑
        fprintf(stderr, "Warning: Protocol minor version mismatch (local: %d, remote: %d)\n",
                LOG_PROTOCOL_MINOR_VERSION, header->minor_version);
    }
    
    return LOG_PROTOCOL_SUCCESS;
}
