#ifndef LUCPD_UTILS_H
#define LUCPD_UTILS_H

#include <stdint.h>
#include <time.h>
#include <stdbool.h>

// 计算 CRC16-IBM 校验 (多项式 0x8005，初值 0xFFFF，无反转，结果异或 0)
uint16_t crc16_ibm(const uint8_t* data, uint32_t length);

// 获取当前时间戳
time_t get_current_timestamp();

// 日志输出
void log_debug(const char* format, ...);
void log_warn(const char *format, ...);
void log_error(const char* format, ...);

// 模拟延时操作
void simulate_delay(int seconds);

// 检查频率限制
bool check_rate_limit(const char* client_ip, time_t* last_access);

uint64_t get_now_ms(void);
#endif // UTILS_H