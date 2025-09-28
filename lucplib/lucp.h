#ifndef LUCP_H
#define LUCP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * LUCP 协议常量
 */
#define LUCP_MAGIC            0x4C554350 // ASCII 'LUCP'
#define LUCP_VER_MAJOR        1
#define LUCP_VER_MINOR        0
#define LUCP_MAX_TEXTINFO_LEN 1010 // 业务范围 : payload 0~1010

/**
 * LUCP 报文类型定义
 */
#define LUCP_MTYP_DEFAULT             0x00
#define LUCP_MTYP_UPLOAD_REQUEST      0x01
#define LUCP_MTYP_ACK_START           0x02
#define LUCP_MTYP_NOTIFY_DONE         0x03
#define LUCP_MTYP_FTP_LOGIN_RESULT    0x04
#define LUCP_MTYP_FTP_DOWNLOAD_RESULT 0x05
#define LUCP_MTYP_CLOUD_UPLOAD_RESULT 0x06

/**
 * LUCP 状态码定义
 */
#define LUCP_STAT_FAILED               0x00
#define LUCP_STAT_SUCCESS              0x01
#define LUCP_STAT_ARCHIVE_FAILED       0x10
#define LUCP_STAT_FTP_UPLOAD_FAILED    0x11
#define LUCP_STAT_FTP_LOGIN_FAILED     0x20
#define LUCP_STAT_FTP_DOWNLOAD_FAILED  0x21
#define LUCP_STAT_CLOUD_UPLOAD_FAILED  0x30
#define LUCP_STAT_INVALID_REQUEST      0xF0
#define LUCP_STAT_TOO_MANY_CONNECTIONS 0xF1
#define LUCP_STAT_RATE_LIMITED         0xF2
#define LUCP_STAT_INTERNAL_ERROR       0xFF

/**
 * LUCP 数据帧结构
 */
typedef struct
{
    uint32_t magic;                          // 固定值 LUCP_MAGIC ('LUCP')
    uint8_t version_major;                   // Protocol 主版本
    uint8_t version_minor;                   // Protocol 次版本
    uint32_t seq_num;                        // 序列号
    uint8_t msgType;                         // 报文类型(指令类型)
    uint8_t status;                          // 状态码(返回码)
    uint16_t textInfo_len;                   // textInfo 的长度 (0~1010)
    uint8_t textInfo[LUCP_MAX_TEXTINFO_LEN]; // 文本信息（不带尾符）
} lucp_frame_t;

/**
 * 将LUCP帧封装到字节缓冲区中。
 * 成功时返回总写入字节数，出错时返回-1。
 */
int lucp_frame_pack(const lucp_frame_t* frame, uint8_t* buf, size_t buflen);

/**
 * 从字节缓冲区中解包LUCP帧。
 * 成功时返回消耗的字节数，不完整时返回0，出错时返回-1。
 */
int lucp_frame_unpack(lucp_frame_t* frame, const uint8_t* buf, size_t buflen);

/**
 * 初始化一个LUCP帧，包含指定的字段和文本信息。
 */
void lucp_frame_make(lucp_frame_t* frame,
                     uint32_t seq,
                     uint8_t cmd,
                     uint8_t status,
                     const char* textInfo,
                     uint16_t textStrlen);

/* 以下接口涉及网络层辅助方法，仅支持Linux平台 */
#ifndef _WIN32
/**
 * LUCP 网络上下文（用于重组和套接字状态）
 */
typedef struct
{
    int fd;             // Socket fd
    uint8_t rbuf[2048]; // 部分读取的接收缓冲区
    size_t rbuf_len;    // 当前在缓冲区中的字节数
} lucp_net_ctx_t;

/**
 * 使用已连接的Socket fd初始化LUCP网络上下文。
 */
void lucp_net_ctx_init(lucp_net_ctx_t* ctx, int fd);

/**
 * 通过网络发送一个LUCP帧。
 * 成功时返回0，出错时返回-1。
 */
int lucp_net_send(lucp_net_ctx_t* ctx, const lucp_frame_t* frame);

/**
 * 接收完整的LUCP帧（处理TCP粘包/分片）。
 * 成功返回0，错误返回-1。
 */
int lucp_net_recv(lucp_net_ctx_t* ctx, lucp_frame_t* frame);

/**
 * 发送一个LUCP帧，并等待预期回复，期间会进行重试。
 * 成功时返回0，出错时返回-1。
 */
int lucp_net_send_with_retries(lucp_net_ctx_t* ctx,
                               lucp_frame_t* frame,
                               lucp_frame_t* reply,
                               uint8_t expect_cmd,
                               int n_retries,
                               int timeout_ms);


#endif // !_WIN32

// ======================================= LOGGING ====================================
// 日志级别枚举
typedef enum {
    LUCP_LOG_DEBUG,   // 调试信息（详细过程）
    LUCP_LOG_INFO,    // 普通信息（正常流程）
    LUCP_LOG_WARN,    // 警告（非致命错误）
    LUCP_LOG_ERROR    // 错误（致命问题）
} LucpLogLevel;

// 日志回调函数类型定义
// 参数说明：
//   level: 日志级别
//   file: 日志产生的源文件（如"lucp.c"）
//   line: 日志产生的行号
//   format: 日志格式化字符串（同printf）
//   ...: 可变参数（与format对应）
typedef void (*LucpLogCallback)(LucpLogLevel level, const char *file, int line, const char *format, ...);

// 注册日志回调函数
// 调用方通过此函数设置自定义日志处理逻辑
// 参数：callback - 调用方实现的日志回调函数（NULL表示禁用日志）
void lucp_set_log_callback(LucpLogCallback callback);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LUCP_H