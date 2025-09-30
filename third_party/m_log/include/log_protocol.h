#ifndef LOG_PROTOCOL_H
#define LOG_PROTOCOL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "logdef.h"

// 协议常量定义
#define LOG_PROTOCOL_MAGIC 0xDEADBEEF
#define LOG_PROTOCOL_MAJOR_VERSION 1
#define LOG_PROTOCOL_MINOR_VERSION 0

// 帧大小限制
#define LOG_PROTOCOL_DEFAULT_MAX_FRAME_SIZE 65536  // 默认64KB
#define LOG_PROTOCOL_MAX_TLV_SIZE 32768            // 最大TLV值大小32KB

// 内存池配置
#define TLV_PREALLOC_COUNT 8  // 预分配TLV数量
#define FRAME_POOL_SIZE 32    // 帧对象池大小

// 错误码定义
typedef enum {
    LOG_PROTOCOL_SUCCESS = 0,
    LOG_PROTOCOL_INVALID_PARAM = -1,
    LOG_PROTOCOL_MEMORY_ERROR = -2,
    LOG_PROTOCOL_INVALID_FRAME = -3,
    LOG_PROTOCOL_TLV_NOT_FOUND = -4,
    LOG_PROTOCOL_TLV_INVALID = -5,
    LOG_PROTOCOL_SERIALIZE_ERROR = -6,
    LOG_PROTOCOL_FRAME_TOO_LARGE = -7,
    LOG_PROTOCOL_CONNECT_ERROR = -8,
    LOG_PROTOCOL_SEND_ERROR = -9,
    LOG_PROTOCOL_RECV_ERROR = -10,
    LOG_PROTOCOL_VERSION_MISMATCH = -11,
    LOG_PROTOCOL_QUEUE_FULL = -12,
    LOG_PROTOCOL_TIMEOUT = -13
} LogProtocolError;



// TLV类型定义
typedef enum {
    TLV_STATUS_MESSAGE   = 0x00,
    TLV_APP_ID           = 0x01,
    TLV_LOG_MESSAGE      = 0x02,
    TLV_THRESHOLD_LEVEL  = 0x03,
    TLV_ENTRY_LEVEL      = 0x04,
    TLV_TIMESTAMP        = 0x05,
    TLV_FATAL_MODE       = 0x21,
    TLV_ERROR_MODE       = 0x22,
    TLV_WARNING_MODE     = 0x23,
    TLV_INFO_MODE        = 0x24,
    TLV_DEBUG_MODE       = 0x25,
    TLV_VERBOSE_MODE     = 0x26
} TlvType;

// 报文类型定义
typedef enum {
    MTYP_UNDEFINED           = 0x00,
    MTYP_REQUEST_CONFIG      = 0x01,  // 客户端请求配置
    MTYP_CONFIG_STATUS       = 0x02,  // 服务端返回配置
    MTYP_PURE_STATUS         = 0x03,  // 服务端纯状态报文
    MTYP_UPDATE_CONFIG       = 0x04,  // 客户端更新配置
    MTYP_SINGLE_LOG          = 0x05,  // 客户端单条日志
    MTYP_MULTIPLE_LOGS       = 0x06   // 客户端多条日志
} MessageType;

// 时间戳结构体
typedef struct {
    time_t seconds;         // 秒级时间戳
    uint16_t milliseconds;  // 毫秒数
} LogTimestamp;

// TLV结构体
typedef struct {
    TlvType type;
    uint16_t length;
    uint8_t *value;
} Tlv;

// 数据帧头部结构体 - 确保按1字节对齐
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t major_version;
    uint8_t minor_version;
    uint16_t sequence;
    int64_t timestamp_sec;  // 64位时间戳
    uint16_t timestamp_ms;
    MessageType mtyp;
    uint8_t status_code;
    uint16_t payload_len;
} FrameHeader;
#pragma pack(pop)

// 完整数据帧结构体
typedef struct {
    FrameHeader header;
    Tlv *tlvs;
    uint32_t tlv_count;
    uint32_t tlv_capacity;  // 预分配的TLV容量
    uint8_t *raw_data;      // 用于存储序列化后的原始数据
    size_t raw_size;        // 原始数据大小
    size_t max_frame_size;  // 该帧的最大大小限制
} LogFrame;

// 日志队列（用于异步发送）
typedef struct LogQueue LogQueue;

// 连接池结构体
typedef struct {
    int *sockets;
    uint32_t count;
    uint32_t capacity;
    pthread_mutex_t lock;
    struct LogProtocolConfig *config;
} ConnectionPool;

// 帧对象池
typedef struct {
    LogFrame *frames[FRAME_POOL_SIZE];
    uint32_t count;
    pthread_mutex_t lock;
} LogFramePool;

// 日志协议配置
typedef struct LogProtocolConfig {
    char *server_ip;
    uint16_t server_port;
    char *app_id;
    DltLevel threshold_level;
    uint32_t timeout_ms;
    size_t max_frame_size;
    int auto_reconnect;       // 自动重连开关
    int shutdown;             // 关闭标志
    LogQueue *queue;          // 异步日志队列
    pthread_t sender_thread;  // 发送线程ID
    pthread_mutex_t lock;     // 配置互斥锁
} LogProtocolConfig;

// 日志队列结构体（内部实现）
struct LogQueue {
    LogFrame *frames[1024];  // 固定大小的循环队列
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int shutdown;
};

// 64位字节序转换函数
static inline uint64_t htonll(uint64_t host64) {
    // 检测系统字节序
    static const int endian = 1;
    if (*(char *)&endian == 1) {  // 小端系统
        return ((uint64_t)htonl(host64 & 0xFFFFFFFF) << 32) | htonl(host64 >> 32);
    } else {  // 大端系统
        return host64;
    }
}

static inline uint64_t ntohll(uint64_t net64) {
    return htonll(net64);  // 字节序转换是对称操作
}

// 配置管理接口
LogProtocolConfig* log_protocol_config_init(const char *server_ip, uint16_t server_port, 
                                           const char *app_id, DltLevel threshold_level);
void log_protocol_config_free(LogProtocolConfig *config);
LogProtocolError log_protocol_update_config(LogProtocolConfig *config, 
                                           DltLevel new_threshold,
                                           size_t new_max_frame_size);

// 帧对象池操作
LogFramePool* log_frame_pool_init();
void log_frame_pool_destroy(LogFramePool *pool);
LogFrame* log_frame_pool_acquire(LogFramePool *pool, LogProtocolConfig *config, 
                                MessageType mtyp, uint16_t sequence, uint8_t status_code);
void log_frame_pool_release(LogFramePool *pool, LogFrame *frame);

// 日志帧操作接口
LogFrame* log_frame_create(LogProtocolConfig *config, MessageType mtyp, 
                          uint16_t sequence, uint8_t status_code);
void log_frame_free(LogFrame *frame);
LogProtocolError log_frame_add_tlv(LogFrame *frame, TlvType type, const uint8_t *value, uint16_t length);
LogProtocolError log_frame_add_string_tlv(LogFrame *frame, TlvType type, const char *str);
LogProtocolError log_frame_add_uint8_tlv(LogFrame *frame, TlvType type, uint8_t value);
LogProtocolError log_frame_add_level_tlv(LogFrame *frame, TlvType type, DltLevel level);
LogProtocolError log_frame_add_timestamp_tlv(LogFrame *frame, LogTimestamp *timestamp);
LogProtocolError log_frame_serialize(LogFrame *frame);
LogFrame* log_frame_parse(LogProtocolConfig *config, const uint8_t *data, size_t length);
Tlv* log_frame_find_tlv(LogFrame *frame, TlvType type);
LogProtocolError log_frame_validate_tlvs(LogFrame *frame);

// TLV解析接口
const char* tlv_get_string(Tlv *tlv);
uint8_t tlv_get_uint8(Tlv *tlv, LogProtocolError *err);
DltLevel tlv_get_level(Tlv *tlv, LogProtocolError *err);
LogProtocolError tlv_get_timestamp(Tlv *tlv, LogTimestamp *timestamp);

// 网络通信接口
int log_protocol_connect(LogProtocolConfig *config);
void log_protocol_disconnect(int sockfd);
LogProtocolError log_protocol_send_frame(int sockfd, LogFrame *frame);
LogFrame* log_protocol_recv_frame(int sockfd, LogProtocolConfig *config);

// 连接池操作
ConnectionPool* connection_pool_init(uint32_t capacity, LogProtocolConfig *config);
void connection_pool_destroy(ConnectionPool *pool);
int connection_pool_acquire(ConnectionPool *pool);
void connection_pool_release(ConnectionPool *pool, int sockfd);

// 日志队列操作
LogQueue* log_queue_init();
void log_queue_destroy(LogQueue *queue);
LogProtocolError log_queue_enqueue(LogQueue *queue, LogFrame *frame);
LogFrame* log_queue_dequeue(LogQueue *queue);

// 异步发送相关
void* log_sender_thread(void *arg);
LogProtocolError log_protocol_start_async_sender(LogProtocolConfig *config);
void log_protocol_stop_async_sender(LogProtocolConfig *config);

// 便捷日志帧创建接口
LogFrame* log_protocol_create_config_request(LogProtocolConfig *config, uint16_t sequence);
LogFrame* log_protocol_create_single_log(LogProtocolConfig *config, uint16_t sequence,
                                        DltLevel level, const char *message);
LogFrame* log_protocol_create_batch_log_frame(LogProtocolConfig *config, uint16_t sequence);
LogProtocolError log_protocol_add_batch_log(LogFrame *batch_frame, DltLevel level, 
                                          const char *message, LogTimestamp *timestamp);

// 辅助工具函数
void log_protocol_get_current_timestamp(LogTimestamp *timestamp);
const char* log_level_to_string(DltLevel level);
LogProtocolError log_protocol_check_version(FrameHeader *header);

#endif // LOG_PROTOCOL_H
