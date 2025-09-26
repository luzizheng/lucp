#include "lucpd_cfg.h"
#include "lucfg.h"
#include "lucpd_utils.h"
#include <stdlib.h>
#include <string.h>

// 设置默认配置
static void set_default_config(LucpdConfig* config) {
    // 网络默认配置
    strncpy(config->network.ip, LUCPD_DEFAULT_IP, sizeof(config->network.ip)-1);
    config->network.port = LUCPD_DEFAULT_PORT;
    config->network.max_clients = LUCPD_DEFAULT_MAX_CLIENTS;
    config->network.recv_timeout_ms = LUCPD_DEFAULT_NW_RECV_TIMEOUT_MS;
    config->network.send_timeout_ms = LUCPD_DEFAULT_NW_SEND_TIMEOUT_MS;

    // 协议默认配置
    config->protocol.rate_limit_ms = LUCPD_DEFAULT_RATE_LIMIT_MS;
    config->protocol.session_timeout_ms = LUCPD_DEFAULT_SESSION_TIMEOUT_MS;
    config->protocol.validate_version = LUCPD_DEFAULT_VALIDATE_VERSION;
    config->protocol.validate_crc16 = LUCPD_DEFAULT_VALIDATE_CRC16;

    // 日志默认配置
    strncpy(config->logging.log_level, "DEBUG", sizeof(config->logging.log_level)-1);
    config->logging.log_file[0] = '\0';  // 默认输出到stdout

    // 文件默认配置
    strncpy(config->file.tmp_dir, "/var/lucp/tmp", sizeof(config->file.tmp_dir)-1);
    config->file.file_retention_min = 30;
}

// 加载配置文件
LucpdConfig* load_config(const char* config_file) {
    LucpdConfig* config = (LucpdConfig*)malloc(sizeof(LucpdConfig));
    if (!config) {
        log_error("Failed to allocate memory for config");
        return NULL;
    }
    set_default_config(config);  // 先设置默认值

    // 打开配置文件
    lucfg_handle_t* xcfg = xcfg_open(config_file);
    if (!xcfg) {
        log_warn("Failed to open config file: %s, using default config", config_file);
        return config;  // 返回默认配置
    }

    // 读取[network]部分配置
    const char* ip_str;
    if (xcfg_get_string(xcfg, "network", "ip", &ip_str) == LUCFG_OK) {
        strncpy(config->network.ip, ip_str, sizeof(config->network.ip)-1);
        log_debug("using config file value: network->ip = %s",ip_str);
    }

    uint16_t port;
    if (xcfg_get_uint16(xcfg, "network", "port", &port) == LUCFG_OK) {
        config->network.port = port;
        log_debug("using config file value: network->port = %d",port);
    }

    int32_t max_clients;
    if (xcfg_get_int32(xcfg, "network", "max_clients", &max_clients) == LUCFG_OK) {
        if (max_clients > 0 && max_clients <= 100) {  // 限制合理范围
            config->network.max_clients = max_clients;
        } else {
            log_warn("Invalid network->max_clients %d", max_clients);
        }
    }

    int32_t recv_timeout;
    if (xcfg_get_int32(xcfg, "network", "recv_timeout_ms", &recv_timeout) == LUCFG_OK) {
        if (recv_timeout >= 100 && recv_timeout <= 10000) {  // 100ms~10s
            config->network.recv_timeout_ms = recv_timeout;
        }else{
            log_warn("Invalid network->recv_timeout_ms: %d",recv_timeout);
        }
    }

    int32_t send_timeout;
    if (xcfg_get_int32(xcfg, "network", "send_timeout_ms", &send_timeout) == LUCFG_OK) {
        if (send_timeout >= 100 && send_timeout <= 10000) {
            config->network.send_timeout_ms = send_timeout;
        }else{
            log_warn("Invalid network->send_timeout_ms: %d",send_timeout);
        }
    }

    int32_t rate_limit;
    if (xcfg_get_int32(xcfg, "protocol", "rate_limit_ms", &rate_limit) == LUCFG_OK) {
        if (rate_limit >= 1000 && rate_limit <= 60000) {  // 1~60秒
            config->protocol.rate_limit_ms = rate_limit;
        }else{
            log_warn("Invalid protocol->rate_limit_ms: %d",rate_limit);
        }
    }

    int32_t session_timeout;
    if (xcfg_get_int32(xcfg, "protocol", "session_timeout_ms", &session_timeout) == LUCFG_OK) {
        if (session_timeout >= 1000 && session_timeout <= 30000) {  // 1~30秒
            config->protocol.session_timeout_ms = session_timeout;
        }else{
            log_warn("Invalid protocol->session_timeout_ms: %d",session_timeout);
        }
    }

    int need_validate_version;
    if (xcfg_get_bool(xcfg,"protocol","validate_version",&need_validate_version) == LUCFG_OK)
    {
        if (need_validate_version == 0 || need_validate_version == 1)
        {
            config->protocol.validate_version = need_validate_version;
            log_debug("setting validate_version: %s",need_validate_version?"true":"false");
        }else{
            log_warn("Invalid protocol->validate_version: %d",need_validate_version);
        }
    }

    int need_validate_crc16;
    if (xcfg_get_bool(xcfg,"protocol","validate_crc16",&need_validate_crc16) == LUCFG_OK)
    {
        if (need_validate_crc16 == 0 || need_validate_crc16 == 1)
        {
            config->protocol.validate_crc16 = need_validate_crc16;
            log_debug("setting validate_crc16: %s",need_validate_crc16?"true":"false");
        }else{
            log_warn("Invalid protocol->validate_crc16: %d",need_validate_crc16);
        }
    }

    // 读取[logging]部分配置
    const char* log_level;
    if (xcfg_get_string(xcfg, "logging", "log_level", &log_level) == LUCFG_OK) {
        strncpy(config->logging.log_level, log_level, sizeof(config->logging.log_level)-1);
    }

    const char* log_file;
    if (xcfg_get_string(xcfg, "logging", "log_file", &log_file) == LUCFG_OK) {
        strncpy(config->logging.log_file, log_file, sizeof(config->logging.log_file)-1);
    }

    // 读取[file]部分配置
    const char* tmp_dir;
    if (xcfg_get_string(xcfg, "file", "tmp_dir", &tmp_dir) == LUCFG_OK) {
        strncpy(config->file.tmp_dir, tmp_dir, sizeof(config->file.tmp_dir)-1);
    }

    int32_t retention;
    if (xcfg_get_int32(xcfg, "file", "file_retention_min", &retention) == LUCFG_OK) {
        if (retention >= 5 && retention <= 1440) {  // 5分钟~24小时
            config->file.file_retention_min = retention;
        }else{
            log_warn("Invalid file->file_retention_min: %d",retention);
        }
    }

    xcfg_close(xcfg);
    log_debug("Loaded config from %s", config_file);
    return config;
}

// 释放配置
void free_config(LucpdConfig* config) {
    if (config) {
        free(config);
    }
}