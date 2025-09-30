#ifndef LOGD_CFG_H
#define LOGD_CFG_H

#include <stddef.h>
#include <pthread.h>
#include "../include/logdef.h"


#define DLT_CFG_FILE_PATH "/etc/logMgr.cfg"
#define DLT_DEFAULT_SERVER_IP "0.0.0.0"
#define DLT_DEFAULT_SERVER_PORT 32123
#define DLT_DEFAULT_TRESHOLD_LEVEL DLT_LEVEL_DEBUG
#define DLT_DEFAULT_FATAL_INFO {DLT_LEVEL_FATAL, DLT_LOG_MODE_CONSOLE | DLT_LOG_MODE_PERSISTENT}
#define DLT_DEFAULT_ERROR_INFO {DLT_LEVEL_ERROR, DLT_LOG_MODE_CONSOLE | DLT_LOG_MODE_PERSISTENT}
#define DLT_DEFAULT_WARN_INFO {DLT_LEVEL_WARN, DLT_LOG_MODE_VOLATILE}
#define DLT_DEFAULT_INFO_INFO {DLT_LEVEL_INFO, DLT_LOG_MODE_VOLATILE}
#define DLT_DEFAULT_DEBUG_INFO {DLT_LEVEL_DEBUG, DLT_LOG_MODE_VOLATILE}
#define DLT_DEFAULT_VERBOSE_INFO {DLT_LEVEL_VERBOSE, DLT_LOG_MODE_VOLATILE} 
#define DLT_DEFAULT_PERSISTENT_DIR "/var/log/logMgr"
#define DLT_DEFAULT_VOLATILE_DIR "/tmp/log/logMgr"
#define DLT_DEFAULT_MAX_FILE_SIZE (10 * 1024 * 1024) // 10 MB
#define DLT_DEFAULT_MAX_BACKUP_FILES 10
#define DLT_DEFAULT_LOG_FORMAT "%Y-%m-%d %H:%M:%S - %(name)s - %(levelname)s - %(message)s"
#define DLT_DEFAULT_DATE_FORMAT "%Y-%m-%d %H:%M:%S"

extern DltGeneralCfg g_dlt_general_cfg; // global general config
extern DltAppCfg g_dlt_app_cfgs[20]; // max 10 apps

/// @brief Load DLT config from file
/// @param cfg_file_path Config file path
/// @param reset If reset is non-zero, reset g_dlt_app_cfgs to zero before loading config file
/// @return 0 on success, -1 on failure
int dlt_load_cfg(const char *cfg_file_path, int reset);
DltAppCfg *dlt_get_appcfg(const char *appId);

#endif