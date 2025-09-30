#include "luftpd_utils.h"
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


int luftpd_utils_resolve_path(const char *root, const char *cwd, const char *path)
{
    char full_path[PATH_MAX];
    if (path[0] == '/') {
        // 绝对路径
        snprintf(full_path, sizeof(full_path), "%s%s", root, path);
    } else {
        // 相对路径
        snprintf(full_path, sizeof(full_path), "%s/%s/%s", root, cwd, path);
    }

    // 规范化路径，去除多余的斜杠和点
    char resolved_path[PATH_MAX];
    char *p = full_path;
    char *q = resolved_path;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            // 跳过多余的斜杠
            p++;
        } else if (p[0] == '/' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
            // 跳过当前目录标识
            p += 2;
        } else if (p[0] == '/' && p[1] == '.' && p[2] == '.' && (p[3] == '/' || p[3] == '\0')) {
            // 处理上级目录标识
            p += 3;
            // 回退到上一级目录
            if (q > resolved_path) {
                q--;
                while (q > resolved_path && *q != '/') {
                    q--;
                }
            }
        } else {
            // 普通字符，复制过去
            *q++ = *p++;
        }
    }
    // 去掉结尾的斜杠（如果有）
    if (q > resolved_path + 1 && *(q - 1) == '/') {
        q--;
    }
    *q = '\0';

    // 检查路径是否在 root 目录下
    if (strncmp(resolved_path, root, strlen(root)) != 0) {
        return -1; // 越界，尝试访问 root 目录外的路径
    }

    return 0; // 成功解析且在 root 目录内
}
