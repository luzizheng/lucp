#ifndef LOG_DEF_H
#define LOG_DEF_H
#include <stdint.h>
#include <stddef.h>
typedef enum
{
    DLT_LEVEL_DISABLED = 0,
    DLT_LEVEL_FATAL    = 1,
    DLT_LEVEL_ERROR    = 2,
    DLT_LEVEL_WARN     = 3,
    DLT_LEVEL_INFO     = 4,
    DLT_LEVEL_DEBUG    = 5,
    DLT_LEVEL_VERBOSE  = 6
} DltLevel;


typedef enum
{
    DLT_LOG_MODE_NONE = 0,
    DLT_LOG_MODE_CONSOLE = 1 << 0,      // 终端输出
    DLT_LOG_MODE_PERSISTENT = 1 << 1,   // 持久化存储（文件）
    DLT_LOG_MODE_VOLATILE = 1 << 2,     // 易失性存储（内存）
} DltLogMode;

typedef struct
{
    DltLevel entry_level;
    DltLogMode mode;
} DltLevelInfo;

typedef struct
{
    char server_ip[64];
    int server_port;
    DltLevel treshold_level;
    char log_persistent_storage_dir[256];
    char log_volatile_storage_dir[256];
    size_t max_file_size;
    int max_backup_files;
    char log_format[256];
    char date_format[64];
    DltLevelInfo level_info[7]; // index by DltLevel
} DltGeneralCfg;

typedef struct
{
    char appId[32];
    DltLevel treshold_level;
    DltLevelInfo level_info[7]; // index by DltLevel
} DltAppCfg;

// dlt logd and log_cli communication frame
typedef struct 
{
    uint32_t magic;
    uint16_t version;
    DltLevel threshold_Level;
} DltFrame;



#endif