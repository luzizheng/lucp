#ifndef LUFTPD_UTILS_H
#define LUFTPD_UTILS_H
#include <stddef.h>

int luftpd_send_raw(int sock, const char *buf, size_t len);
int luftpd_send_response(int sock, const char *fmt, ...);
int luftpd_parse_command(const char *cmd, char *out_cmd, char *out_arg);


/// @brief 解析用户提供的路径，确保其在FTP根目录内，并生成绝对路径
int luftpd_resolve_path(const char* ftp_root,
                        const char* current_path,
                        const char* user_control_path,
                        char* output,
                        size_t output_sz);

#endif // LUFTPD_UTILS_H