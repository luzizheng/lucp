#include "luftpd_utils.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/socket.h>


int luftpd_send_raw(int sock, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len){
        ssize_t n = send(sock, buf + sent, len - sent, 0);
        if (n < 0) return -1;
        sent += n;
    }
    return 0;
}


int luftpd_send_response(int sock, const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    if (len < 0) return -1;
    
    // 确保以\r\n结尾
    if (len >= (int)sizeof(buffer) - 3) {
        len = sizeof(buffer) - 3;
    }
    if (len < 2 || buffer[len-2] != '\r' || buffer[len-1] != '\n') {
        strcpy(buffer + len, "\r\n");
        len += 2;
    }
    
    return send(sock, buffer, len, 0);
}

// 解析FTP命令
int luftpd_parse_command(const char *cmd, char *out_cmd, char *out_arg) {
    char buffer[512];
    strncpy(buffer, cmd, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    // 移除\r\n
    char *newline = strchr(buffer, '\r');
    if (newline) *newline = '\0';
    newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';
    
    // 分割命令和参数
    char *space = strchr(buffer, ' ');
    if (space) {
        *space = '\0';
        strncpy(out_cmd, buffer, 16);
        out_cmd[15] = '\0';
        strncpy(out_arg, space + 1, 256);
        out_arg[255] = '\0';
    } else {
        strncpy(out_cmd, buffer, 16);
        out_cmd[15] = '\0';
        out_arg[0] = '\0';
    }
    
    // 转换为大写
    for (char *p = out_cmd; *p; p++) {
        *p = toupper(*p);
    }
    
    return 0;
}





int luftpd_resolve_path(const char* ftp_root,
                        const char* current_path,
                        const char* user_control_path,
                        char* output,
                        size_t output_sz)
{

    // 参数检查
    if (ftp_root == NULL || current_path == NULL || user_control_path == NULL || output == NULL ||
        output_sz == 0)
    {
        errno = EINVAL;
        return -1;
    }

    // 确保FTP根目录是绝对路径
    if (ftp_root[0] != '/')
    {
        errno = EINVAL;
        return -1;
    }

    // 规范化FTP根目录路径（移除末尾的/）
    char normalized_ftp_root[PATH_MAX];
    size_t ftp_root_len = strlen(ftp_root);

    if (ftp_root_len >= PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    strncpy(normalized_ftp_root, ftp_root, PATH_MAX);
    normalized_ftp_root[PATH_MAX - 1] = '\0';

    if (ftp_root_len > 1 && normalized_ftp_root[ftp_root_len - 1] == '/')
    {
        normalized_ftp_root[ftp_root_len - 1] = '\0';
        ftp_root_len--;
    }

    // 检查current_path是否在ftp_root内
    char real_ftp_root[PATH_MAX];
    char real_current_path[PATH_MAX];

    // 获取ftp_root的真实路径
    if (realpath(normalized_ftp_root, real_ftp_root) == NULL)
    {
        return -1; // FTP根目录不存在或无法访问
    }

    // 获取current_path的真实路径
    if (realpath(current_path, real_current_path) == NULL)
    {
        return -1; // 当前路径不存在或无法访问
    }

    // 确保current_path是ftp_root的子目录
    size_t real_ftp_root_len = strlen(real_ftp_root);
    if (strncmp(real_current_path, real_ftp_root, real_ftp_root_len) != 0)
    {
        errno = EACCES;
        return -1; // current_path不在ftp_root内
    }

    // 如果current_path正好是ftp_root，那么real_current_path应该等于real_ftp_root
    // 如果current_path是子目录，那么real_current_path[real_ftp_root_len]应该是'/'
    if (strlen(real_current_path) > real_ftp_root_len &&
        real_current_path[real_ftp_root_len] != '/')
    {
        errno = EACCES;
        return -1; // 路径格式异常
    }

    // 构建基础路径
    char base_path[PATH_MAX];
    if (user_control_path[0] == '/')
    {
        // 用户提供的是绝对路径（相对于FTP根目录）
        if (strlen(real_ftp_root) >= sizeof(base_path))
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(base_path, real_ftp_root);
    }
    else
    {
        // 用户提供的是相对路径（相对于current_path）
        if (strlen(real_current_path) >= sizeof(base_path))
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(base_path, real_current_path);
    }

    // 移除base_path末尾的/
    size_t base_len = strlen(base_path);
    if (base_len > 1 && base_path[base_len - 1] == '/')
    {
        base_path[base_len - 1] = '\0';
    }

    // 处理用户控制路径
    char temp_path[PATH_MAX * 2];
    char* components[PATH_MAX];
    int comp_count = 0;

    // 复制用户路径到临时缓冲区
    size_t user_path_len = strlen(user_control_path);
    if (user_path_len >= sizeof(temp_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(temp_path, user_control_path);

    // 分割路径组件
    char* token = strtok(temp_path, "/");
    while (token != NULL && comp_count < PATH_MAX - 1)
    {
        components[comp_count++] = token;
        token                    = strtok(NULL, "/");
    }
    components[comp_count] = NULL; // 结束标记

    // 构建最终路径
    char result_path[PATH_MAX * 2];
    if (strlen(base_path) >= sizeof(result_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(result_path, base_path);

    // 处理每个路径组件
    for (int i = 0; i < comp_count; i++)
    {
        char* comp        = components[i];
        size_t result_len = strlen(result_path);

        // 安全检查：防止路径穿透攻击
        if (strcmp(comp, ".") == 0)
        {
            // 当前目录，跳过
            continue;
        }
        else if (strcmp(comp, "..") == 0)
        {
            // 上级目录，需要特殊处理

            // 检查是否尝试跳出FTP根目录
            if (strncmp(result_path, real_ftp_root, strlen(real_ftp_root)) != 0)
            {
                errno = EACCES;
                return -1;
            }

            // 找到最后一个/
            char* last_slash = strrchr(result_path, '/');
            if (last_slash != NULL)
            {
                // 确保不会删除FTP根目录
                size_t root_len = strlen(real_ftp_root);
                if (last_slash > result_path + root_len)
                {
                    *last_slash = '\0';
                }
                else if (strlen(result_path) > root_len)
                {
                    // 如果已经在FTP根目录，保持原样
                    result_path[root_len] = '\0';
                }
            }
        }
        else
        {
            // 正常路径组件
            size_t current_len = strlen(result_path);
            size_t comp_len    = strlen(comp);

            // 检查路径长度限制
            if (current_len + comp_len + 2 >= sizeof(result_path))
            {
                errno = ENAMETOOLONG;
                return -1;
            }

            // 添加路径分隔符（如果不是根目录）
            if (current_len > 0 && result_path[current_len - 1] != '/')
            {
                strcat(result_path, "/");
            }

            // 添加组件
            strcat(result_path, comp);
        }
    }

    // 最终安全检查：确保结果路径在FTP根目录内
    if (strncmp(result_path, real_ftp_root, strlen(real_ftp_root)) != 0)
    {
        errno = EACCES;
        return -1;
    }

    // 尝试使用realpath规范化路径
    char final_path[PATH_MAX];
    char* real_res = realpath(result_path, final_path);
    if (real_res == NULL)
    {
        // realpath失败，路径可能不存在，但我们仍然检查安全性
        // 使用我们构建的路径但进行基本检查
        if (strlen(result_path) >= sizeof(final_path))
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(final_path, result_path);
    }

    // 再次验证最终路径在FTP根目录内
    if (strncmp(final_path, real_ftp_root, strlen(real_ftp_root)) != 0)
    {
        errno = EACCES;
        return -1;
    }

    // 检查输出缓冲区大小
    size_t final_len = strlen(final_path);
    if (final_len >= output_sz)
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    // 复制结果到输出缓冲区
    strcpy(output, final_path);
    return 0;
}


