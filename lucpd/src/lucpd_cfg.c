#include "lucpd_cfg.h"
#include "lucfg.h"
#include "lucpd_utils.h"
#include <stdlib.h>
#include <string.h>

// 设置默认配置
static void set_default_config(LucpdConfig_t* config) {
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
    strncpy(config->file.tmp_dir, "/tmp/lucp", sizeof(config->file.tmp_dir)-1);
    config->file.file_retention_min = 30;
}

// 加载配置文件
int lucpd_cfg_load_with_file(LucpdConfig_t * cfg, const char* config_file) {

    if (cfg == NULL) 
    {
        log_error("%s: LucpdConfig_t params is NULL.",__func__);
        return -1;
    }
    set_default_config(cfg);  // 先设置默认值

    // 打开配置文件
    lucfg_handle_t* lucfg = lucfg_open(config_file);
    if (!lucfg) {
        log_warn("Failed to open config file: %s, using default config", config_file);
        return -1;  // 返回错误
    }

    // 读取[network]部分配置
    const char* ip_str;
    if (lucfg_get_string(lucfg, "network", "ip", &ip_str) == LUCFG_OK) {
        strncpy(cfg->network.ip, ip_str, sizeof(cfg->network.ip)-1);
        log_debug("using config file value: network->ip = %s",ip_str);
    }

    uint16_t port;
    if (lucfg_get_uint16(lucfg, "network", "port", &port) == LUCFG_OK) {
        cfg->network.port = port;
        log_debug("using config file value: network->port = %d",port);
    }

    int32_t max_clients;
    if (lucfg_get_int32(lucfg, "network", "max_clients", &max_clients) == LUCFG_OK) {
        if (max_clients > 0 && max_clients <= 100) {  // 限制合理范围
            cfg->network.max_clients = max_clients;
        } else {
            log_warn("Invalid network->max_clients %d", max_clients);
        }
    }

    int32_t recv_timeout;
    if (lucfg_get_int32(lucfg, "network", "recv_timeout_ms", &recv_timeout) == LUCFG_OK) {
        if (recv_timeout >= 100 && recv_timeout <= 10000) {  // 100ms~10s
            cfg->network.recv_timeout_ms = recv_timeout;
        }else{
            log_warn("Invalid network->recv_timeout_ms: %d",recv_timeout);
        }
    }

    int32_t send_timeout;
    if (lucfg_get_int32(lucfg, "network", "send_timeout_ms", &send_timeout) == LUCFG_OK) {
        if (send_timeout >= 100 && send_timeout <= 10000) {
            cfg->network.send_timeout_ms = send_timeout;
        }else{
            log_warn("Invalid network->send_timeout_ms: %d",send_timeout);
        }
    }

    int32_t rate_limit;
    if (lucfg_get_int32(lucfg, "protocol", "rate_limit_ms", &rate_limit) == LUCFG_OK) {
        if (rate_limit >= 1000 && rate_limit <= 60000) {  // 1~60秒
            cfg->protocol.rate_limit_ms = rate_limit;
        }else{
            log_warn("Invalid protocol->rate_limit_ms: %d",rate_limit);
        }
    }

    int32_t session_timeout;
    if (lucfg_get_int32(lucfg, "protocol", "session_timeout_ms", &session_timeout) == LUCFG_OK) {
        if (session_timeout >= 1000 && session_timeout <= 30000) {  // 1~30秒
            cfg->protocol.session_timeout_ms = session_timeout;
        }else{
            log_warn("Invalid protocol->session_timeout_ms: %d",session_timeout);
        }
    }

    int need_validate_version;
    if (lucfg_get_bool(lucfg,"protocol","validate_version",&need_validate_version) == LUCFG_OK)
    {
        if (need_validate_version == 0 || need_validate_version == 1)
        {
            cfg->protocol.validate_version = need_validate_version;
            log_debug("setting validate_version: %s",need_validate_version?"true":"false");
        }else{
            log_warn("Invalid protocol->validate_version: %d",need_validate_version);
        }
    }

    int need_validate_crc16;
    if (lucfg_get_bool(lucfg,"protocol","validate_crc16",&need_validate_crc16) == LUCFG_OK)
    {
        if (need_validate_crc16 == 0 || need_validate_crc16 == 1)
        {
            cfg->protocol.validate_crc16 = need_validate_crc16;
            log_debug("setting validate_crc16: %s",need_validate_crc16?"true":"false");
        }else{
            log_warn("Invalid protocol->validate_crc16: %d",need_validate_crc16);
        }
    }

    // 读取[logging]部分配置
    const char* log_level;
    if (lucfg_get_string(lucfg, "logging", "log_level", &log_level) == LUCFG_OK) {
        strncpy(cfg->logging.log_level, log_level, sizeof(cfg->logging.log_level)-1);
    }

    const char* log_file;
    if (lucfg_get_string(lucfg, "logging", "log_file", &log_file) == LUCFG_OK) {
        strncpy(cfg->logging.log_file, log_file, sizeof(cfg->logging.log_file)-1);
    }

    // 读取[file]部分配置
    const char* tmp_dir;
    if (lucfg_get_string(lucfg, "file", "tmp_dir", &tmp_dir) == LUCFG_OK) {
        strncpy(cfg->file.tmp_dir, tmp_dir, sizeof(cfg->file.tmp_dir)-1);
    }

    int32_t retention;
    if (lucfg_get_int32(lucfg, "file", "file_retention_min", &retention) == LUCFG_OK) {
        if (retention >= 5 && retention <= 1440) {  // 5分钟~24小时
            cfg->file.file_retention_min = retention;
        }else{
            log_warn("Invalid file->file_retention_min: %d",retention);
        }
    }

    lucfg_close(lucfg);
    log_debug("Loaded config from %s", config_file);
    return 0;
}

static void set_file_and_port(LucpdConfig_t * cfg, const char *file, int port, bool hassetc_)
{
    lucpd_cfg_load_with_file(cfg, file);
    if (hassetc_)
    {
        cfg->network.port = port;
    }
}

int lucpd_cfg_load_with_entryArgs(LucpdConfig_t * cfg, int argc, char *argv[])
{
    const char *cfgpath = LUCPD_DEFAULT_CFG_FILE;
    int port = LUCPD_DEFAULT_PORT;
    bool hassetc = false;
    // 遍历所有参数
    for (int i = 1; i < argc; i++) {
        // 处理-c选项
        if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                cfgpath = argv[i + 1];
                i++;  // 跳过已处理的值
            } else {
                log_warn("args parse error: -c need cfg file path");
                set_file_and_port(cfg, cfgpath, port, hassetc);
                return -1;
            }
        }
        // 处理-p选项
        else if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 < argc) {
                // 转换端口号为整数
                int input_port = atoi(argv[i + 1]);
                // 验证端口号范围
                if (input_port<= 0 || input_port > 65535) {
                    log_warn("args parse error: port muse be 1-65535.");
                    set_file_and_port(cfg, cfgpath, port, hassetc);
                    return -1;
                }else{
                    port = input_port;
                    hassetc = true;
                }
                i++;  // 跳过已处理的值
            } else {
                log_warn("args parse error: -p option need port value");
                set_file_and_port(cfg, cfgpath, port, hassetc);
                return -1;
            }
        }
        // 处理未知选项
        else {
            log_warn("args parse error: unknown option %s", argv[i]);
            set_file_and_port(cfg, cfgpath, port, hassetc);
            return -1;
        }
    }

    set_file_and_port(cfg, cfgpath, port, hassetc);
    return 0;
}