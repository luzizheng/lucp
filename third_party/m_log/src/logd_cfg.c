#include "logd_cfg.h"
#include <lucfg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
// include for stricmp
#ifdef _WIN32
#include <string.h>
#else
#include <strings.h>
#define stricmp strcasecmp
#endif

DltGeneralCfg  g_dlt_general_cfg = {
    .log_persistent_storage_dir = DLT_DEFAULT_PERSISTENT_DIR,
    .log_volatile_storage_dir = DLT_DEFAULT_VOLATILE_DIR,
    .max_file_size = DLT_DEFAULT_MAX_FILE_SIZE,
    .max_backup_files = DLT_DEFAULT_MAX_BACKUP_FILES,
    .log_format = DLT_DEFAULT_LOG_FORMAT,
    .date_format = DLT_DEFAULT_DATE_FORMAT,
    .level_info = {
        DLT_DEFAULT_FATAL_INFO,
        DLT_DEFAULT_ERROR_INFO,
        DLT_DEFAULT_WARN_INFO,
        DLT_DEFAULT_INFO_INFO,
        DLT_DEFAULT_DEBUG_INFO,
        DLT_DEFAULT_VERBOSE_INFO
    }
};

DltAppCfg g_dlt_app_cfgs[20]; // max 20 apps

pthread_mutex_t g_dlt_cfg_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_dlt_app_cfgs_lock = PTHREAD_MUTEX_INITIALIZER;


int dlt_load_cfg(const char *cfg_file_path, int reset)
{
    // lock
    pthread_mutex_lock(&g_dlt_cfg_lock);
    pthread_mutex_lock(&g_dlt_app_cfgs_lock);
    if (reset)
    { 
        // reset general config to default
        g_dlt_general_cfg = (DltGeneralCfg) {
            .server_ip = DLT_DEFAULT_SERVER_IP,
            .server_port = DLT_DEFAULT_SERVER_PORT,
            .treshold_level = DLT_DEFAULT_TRESHOLD_LEVEL,
            .log_persistent_storage_dir = DLT_DEFAULT_PERSISTENT_DIR,
            .log_volatile_storage_dir = DLT_DEFAULT_VOLATILE_DIR,
            .max_file_size = DLT_DEFAULT_MAX_FILE_SIZE,
            .max_backup_files = DLT_DEFAULT_MAX_BACKUP_FILES,
            .log_format = DLT_DEFAULT_LOG_FORMAT,
            .date_format = DLT_DEFAULT_DATE_FORMAT,
            .level_info = {
                DLT_DEFAULT_FATAL_INFO,
                DLT_DEFAULT_ERROR_INFO,
                DLT_DEFAULT_WARN_INFO,
                DLT_DEFAULT_INFO_INFO,
                DLT_DEFAULT_DEBUG_INFO,
                DLT_DEFAULT_VERBOSE_INFO
            }
        };
        // reset app configs
        memset(g_dlt_app_cfgs, 0, sizeof(g_dlt_app_cfgs));
    }


    lucfg_handle_t *cfg = lucfg_open(cfg_file_path);
    if (cfg == NULL)
    {
        pthread_mutex_unlock(&g_dlt_app_cfgs_lock);
        pthread_mutex_unlock(&g_dlt_cfg_lock);
        fprintf(stderr, "Failed to open config file: %s\n", cfg_file_path);
        return -1;
    }

    const char *str_val = NULL;
    int int_val = 0;
    size_t size_val = 0;

    // Load general config
    if (lucfg_get_string(cfg, "general", "ip", &str_val) == LUCFG_OK)
    {
        snprintf(g_dlt_general_cfg.server_ip, sizeof(g_dlt_general_cfg.server_ip), "%s", str_val);
    }
    if (lucfg_get_int32(cfg, "general", "port", (int32_t*)&int_val) == LUCFG_OK)
    {
        g_dlt_general_cfg.server_port = int_val;
    }
    if (lucfg_get_string(cfg, "general", "treshold_level", &str_val) == LUCFG_OK)
    {
        if (stricmp(str_val, "fatal") == 0)
            g_dlt_general_cfg.treshold_level = DLT_LEVEL_FATAL;
        else if (stricmp(str_val, "error") == 0)
            g_dlt_general_cfg.treshold_level = DLT_LEVEL_ERROR;
        else if (stricmp(str_val, "warning") == 0)
            g_dlt_general_cfg.treshold_level = DLT_LEVEL_WARN;
        else if (stricmp(str_val, "info") == 0)
            g_dlt_general_cfg.treshold_level = DLT_LEVEL_INFO;
        else if (stricmp(str_val, "debug") == 0)
            g_dlt_general_cfg.treshold_level = DLT_LEVEL_DEBUG;
        else if (stricmp(str_val, "verbose") == 0)
            g_dlt_general_cfg.treshold_level = DLT_LEVEL_VERBOSE;
    }
    if (lucfg_get_string(cfg, "general", "log_persistent_storage_dir", &str_val) == LUCFG_OK)
    {
        snprintf(g_dlt_general_cfg.log_persistent_storage_dir, sizeof(g_dlt_general_cfg.log_persistent_storage_dir), "%s", str_val);
    }
    if (lucfg_get_string(cfg, "general", "log_volatile_storage_dir", &str_val) == LUCFG_OK)
    {
        snprintf(g_dlt_general_cfg.log_volatile_storage_dir, sizeof(g_dlt_general_cfg.log_volatile_storage_dir), "%s", str_val);
    }
    if (lucfg_get_int64(cfg, "general", "max_file_size", (int64_t*)&size_val) == LUCFG_OK)
    {
        g_dlt_general_cfg.max_file_size = size_val;
    }
    if (lucfg_get_int32(cfg, "general", "max_backup_files", (int32_t*)&int_val) == LUCFG_OK)
    {
        g_dlt_general_cfg.max_backup_files = int_val;
    }
    if (lucfg_get_string(cfg, "general", "log_format", &str_val) == LUCFG_OK)
    {
        snprintf(g_dlt_general_cfg.log_format, sizeof(g_dlt_general_cfg.log_format), "%s", str_val);
    }
    if (lucfg_get_string(cfg, "general", "date_format", &str_val) == LUCFG_OK)
    {
        snprintf(g_dlt_general_cfg.date_format, sizeof(g_dlt_general_cfg.date_format), "%s", str_val);
    }

    // Load level modes
    const char *level_keys[] = {
        "fatal.mode",
        "error.mode",
        "warning.mode",
        "info.mode",
        "debug.mode",
        "verbose.mode"
    };

    // There are 6 levels from index 0 to 5
    // 检测每个级别的 mode 配置：只要字符串中包含对应的模式名称，就启用该模式
    for (int i = 0; i < 6; i++)
    {
        if (lucfg_get_string(cfg, "general", level_keys[i], &str_val) == LUCFG_OK)
        {
            g_dlt_general_cfg.level_info[i].mode = DLT_LOG_MODE_NONE;
            if (strstr(str_val, "console") != NULL)
            {
                g_dlt_general_cfg.level_info[i].mode |= DLT_LOG_MODE_CONSOLE;
            }
            if (strstr(str_val, "persistent") != NULL)
            {
                g_dlt_general_cfg.level_info[i].mode |= DLT_LOG_MODE_PERSISTENT;
            }
            if (strstr(str_val, "volatile") != NULL)
            {
                g_dlt_general_cfg.level_info[i].mode |= DLT_LOG_MODE_VOLATILE;
            }
        }
    }

    // app specific config
    // memset all to 0
    memset(g_dlt_app_cfgs, 0, sizeof(g_dlt_app_cfgs));
    // get all sections
    const char **sections = NULL;
    int section_count = 0;
    if (lucfg_get_sections(cfg, &sections, &section_count) != LUCFG_OK)
    {
        lucfg_close(cfg);
        pthread_mutex_unlock(&g_dlt_app_cfgs_lock);
        pthread_mutex_unlock(&g_dlt_cfg_lock);
        fprintf(stderr, "Failed to get sections from config file: %s\n", cfg_file_path);
        return -1;
    }
    int app_cfg_index = 0;
    for (int i = 0; i < section_count; i++)
    {
        if (stricmp(sections[i], "general") == 0)
            continue; // skip general section
        if (app_cfg_index >= 20)
            break; // max 20 apps
        DltAppCfg *app_cfg = &g_dlt_app_cfgs[app_cfg_index];
        memset(app_cfg, 0, sizeof(DltAppCfg));
        snprintf(app_cfg->appId, sizeof(app_cfg->appId), "%s", sections[i]);
        // load app specific treshold level
        if (lucfg_get_string(cfg, sections[i], "treshold_level", &str_val) == LUCFG_OK)
        {
            if (stricmp(str_val, "fatal") == 0)
                app_cfg->treshold_level = DLT_LEVEL_FATAL;
            else if (stricmp(str_val, "error") == 0)
                app_cfg->treshold_level = DLT_LEVEL_ERROR;
            else if (stricmp(str_val, "warning") == 0)
                app_cfg->treshold_level = DLT_LEVEL_WARN;
            else if (stricmp(str_val, "info") == 0)
                app_cfg->treshold_level = DLT_LEVEL_INFO;
            else if (stricmp(str_val, "debug") == 0)
                app_cfg->treshold_level = DLT_LEVEL_DEBUG;
            else if (stricmp(str_val, "verbose") == 0)
                app_cfg->treshold_level = DLT_LEVEL_VERBOSE;
        }
        // default to general config if not set
        if (app_cfg->treshold_level == 0)
            app_cfg->treshold_level = g_dlt_general_cfg.treshold_level;
        // load app specific level modes
        for (int j = 0; j < 6; j++)
        {
            if (lucfg_get_string(cfg, sections[i], level_keys[j], &str_val) == LUCFG_OK)
            {
                app_cfg->level_info[j].mode = DLT_LOG_MODE_NONE;
                if (strstr(str_val, "console") != NULL)
                {
                    app_cfg->level_info[j].mode |= DLT_LOG_MODE_CONSOLE;
                }
                if (strstr(str_val, "persistent") != NULL)
                {
                    app_cfg->level_info[j].mode |= DLT_LOG_MODE_PERSISTENT;
                }
                if (strstr(str_val, "volatile") != NULL)
                {
                    app_cfg->level_info[j].mode |= DLT_LOG_MODE_VOLATILE;
                }
            }
        }
        app_cfg_index++;
    }
    lucfg_free_array(sections, section_count);
    lucfg_close(cfg);
    // unlock
    pthread_mutex_unlock(&g_dlt_app_cfgs_lock);
    pthread_mutex_unlock(&g_dlt_cfg_lock);
    return 0;
}


DltAppCfg *dlt_get_appcfg(const char *appId)
{
    pthread_mutex_lock(&g_dlt_app_cfgs_lock);
    for (int i = 0; i < 20; i++)
    {
        if (g_dlt_app_cfgs[i].appId[0] == '\0')
            break; // no more configured apps   
        if (strcmp(g_dlt_app_cfgs[i].appId, appId) == 0)
        {
            pthread_mutex_unlock(&g_dlt_app_cfgs_lock);
            return &g_dlt_app_cfgs[i];
        }
    }
    pthread_mutex_unlock(&g_dlt_app_cfgs_lock);
    return NULL;
}



