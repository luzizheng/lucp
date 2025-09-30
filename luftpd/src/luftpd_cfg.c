#include "luftpd_cfg.h"
#include <lucfg.h>
#include <string.h>

int luftpd_cfg_load_with_file(LuftpdConfig_t* cfg, const char* config_file)
{
    if (cfg == NULL || config_file == NULL) return -1;

    // 设置默认值
    *cfg = (LuftpdConfig_t)LUFTPD_CONFIG_INITIALIZER;

    // 读取配置文件
    lucfg_handle_t* handle = lucfg_open(config_file);
    if (handle == NULL) {
        return -1;
    }

    // 解析配置项
    if (lucfg_get_string(handle, "server", "ip", cfg->ip) != LUCFG_OK) {
        strcpy(cfg->ip, LUFTPD_DEFAULT_IP);
    }
    if (lucfg_get_uint16(handle, "server", "port", &cfg->port) != LUCFG_OK) {
        cfg->port = LUFTPD_DEFAULT_PORT;
    }

    if (lucfg_get_string(handle, "server", "root_dir", cfg->root_dir) != LUCFG_OK) {
        strcpy(cfg->root_dir, LUFTPD_DEFAULT_ROOT_DIR);
    }

    if (lucfg_get_int32(handle, "server", "max_connections", &cfg->max_connections) != LUCFG_OK) {
        cfg->max_connections = LUFTPD_DEFAULT_MAX_CONNECTIONS;
    }
    
    if (lucfg_get_int32(handle, "server", "data_port_min",&cfg->data_port_min) != LUCFG_OK) {
        cfg->data_port_min = LUFTPD_DEFAULT_DATA_PORT_MIN;
    }
    
    if (lucfg_get_int32(handle, "server", "data_port_max",&cfg->data_port_max) != LUCFG_OK) {
        cfg->data_port_max = LUFTPD_DEFAULT_DATA_PORT_MAX;
    }

    lucfg_close(handle);
    return 0;
}
