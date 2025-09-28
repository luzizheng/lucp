#include "lucpd_utils.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>


#define RATE_LIMIT_SECONDS 3
#define IP_HASH_SIZE       1024

static pthread_mutex_t rate_limit_mutex    = PTHREAD_MUTEX_INITIALIZER;
static char ip_hash[1024][INET_ADDRSTRLEN] = {0};

// 获取当前时间戳
time_t get_current_timestamp() { return time(NULL); }

// 日志输出
void log_debug(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    time_t now         = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stdout, "[%s] DEBUG: ", time_str);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    fflush(stdout);

    va_end(args);
}

void log_warn(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    time_t now         = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stdout, "[%s] WARN: ", time_str);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    fflush(stdout);

    va_end(args);
}

void log_error(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    time_t now         = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stderr, "[%s] ERROR: ", time_str);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    fflush(stderr);

    va_end(args);
}

// 模拟延时操作
void simulate_delay(int seconds)
{
    if (seconds > 0)
    {
        sleep(seconds);
    }
}

// IP 哈希函数
static unsigned int ip_hash_function(const char* ip)
{
    unsigned int hash = 0;
    while (*ip)
    {
        hash = (hash << 5) + hash + *ip++;
    }
    return hash % IP_HASH_SIZE;
}

// 检查频率限制
bool check_rate_limit(const char* client_ip, time_t* last_access)
{
    if (!client_ip || !last_access)
    {
        return false;
    }

    pthread_mutex_lock(&rate_limit_mutex);

    unsigned int hash = ip_hash_function(client_ip);
    time_t now        = get_current_timestamp();
    bool allowed      = true;

    // 检查IP是否存在于哈希表中
    if (strcmp(ip_hash[hash], client_ip) == 0)
    {
        // 检查是否在限制时间内
        if (now - last_access[hash] < RATE_LIMIT_SECONDS)
        {
            allowed = false;
        }
        else
        {
            // 更新访问时间
            last_access[hash] = now;
        }
    }
    else
    {
        // 新IP，添加到哈希表
        strncpy(ip_hash[hash], client_ip, INET_ADDRSTRLEN - 1);
        ip_hash[hash][INET_ADDRSTRLEN - 1] = '\0';
        last_access[hash]                  = now;
    }

    pthread_mutex_unlock(&rate_limit_mutex);
    return allowed;
}

// Utility: get current ms since epoch
uint64_t get_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t) tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

void handle_lucp_log(LucpLogLevel level, const char *file, int line, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    time_t now         = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    const char* level_str = "UNKNOWN";
    switch (level) {
        case LUCP_LOG_DEBUG: level_str = "DEBUG"; break;
        case LUCP_LOG_INFO:  level_str = "INFO";  break;
        case LUCP_LOG_WARN:  level_str = "WARN";  break;
        case LUCP_LOG_ERROR: level_str = "ERROR"; break;
    }

    fprintf(stdout, "[%s] %s (%s:%d): ", time_str, level_str, file, line);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    fflush(stdout);

    va_end(args);
}