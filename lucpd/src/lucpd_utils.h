#ifndef LUCPD_UTILS_H
#define LUCPD_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// 获取当前时间戳
time_t get_current_timestamp();

// 日志输出
void log_debug(const char* format, ...);
void log_warn(const char* format, ...);
void log_error(const char* format, ...);

// 模拟延时操作
void simulate_delay(int seconds);

// 检查频率限制
bool check_rate_limit(const char* client_ip, time_t* last_access);

uint64_t get_now_ms(void);
#endif // UTILS_H