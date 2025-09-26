#ifndef LUCPD_CONFIG_H
#define LUCPD_CONFIG_H

#include <stdint.h>
#include <stdbool.h>



// 服务器配置缺省值
#define LUCPD_DEFAULT_PORT 32100
#define LUCPD_DEFAULT_IP "127.0.0.1"
#define LUCPD_DEFAULT_VERSION 0x10      // 1.0 版本 (高4位主版本，低4位次版本)
#define LUCPD_DEFAULT_MAX_CLIENTS 10
#define LUCPD_DEFAULT_NW_RECV_TIMEOUT_MS 1000
#define LUCPD_DEFAULT_NW_SEND_TIMEOUT_MS 1000
#define LUCPD_DEFAULT_RATE_LIMIT_MS 3000
#define LUCPD_DEFAULT_SESSION_TIMEOUT_MS 2000
#define LUCPD_DEFAULT_VALIDATE_VERSION 1
#define LUCPD_DEFAULT_VALIDATE_CRC16 1

#define LUCPD_DEFAULT_CFG_FILE "/etc/lucpd.conf"


// 配置结构体，与lucpd.conf对应
typedef struct {
    // 网络相关配置
    struct {
        char ip[64];           // 绑定的IP地址，默认"0.0.0.0"
        uint16_t port;         // 监听端口，默认32100
        int max_clients;       // 最大客户端连接数，默认10
        int recv_timeout_ms;   // 接收超时(毫秒)，默认1000
        int send_timeout_ms;   // 发送超时(毫秒)，默认1000
    } network;

    // 协议相关配置
    struct {
        int rate_limit_ms;    // 频率限制(秒)，默认3
        int session_timeout_ms; // 会话超时(秒)，默认2
        bool validate_version; // 是否校验版本号
        bool validate_crc16;     // 是否校验crc16
    } protocol;

    // 日志相关配置
    struct {
        char log_level[16];    // 日志级别(DEBUG/INFO/WARN/ERROR)，默认"DEBUG"
        char log_file[256];    // 日志文件路径，默认stdout
    } logging;

    // 文件相关配置
    struct {
        char tmp_dir[256];     // 临时文件目录，默认"/var/lucp/tmp"
        int file_retention_min;// 文件保留时间(分钟)，默认30
    } file;
} LucpdConfig;


// 加载配置文件，返回默认配置(失败时)或解析后的配置
LucpdConfig* load_config(const char* config_file);

// 释放配置结构体
void free_config(LucpdConfig* config);

#endif // CONFIG_H