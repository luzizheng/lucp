/* resolve_path.c  2025-10  零警告 / 零 sanitizer 报错 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

/* 内部：把 path 正规化到 buf，返回 0 成功，-1 失败 */
static int normalize_path(const char *path, char *buf, size_t buf_sz)
{
    char  tmp[PATH_MAX];
    const char *comp[PATH_MAX/2];
    int   cnt = 0;

    if (!path || !buf || buf_sz == 0) return -1;
    if (strlen(path) >= sizeof(tmp)) return -1;
    strcpy(tmp, path);

    for (char *tok = strtok(tmp, "/"); tok; tok = strtok(NULL, "/")) {
        if (strcmp(tok, ".") == 0) continue;
        if (strcmp(tok, "..") == 0) {
            if (cnt > 0) --cnt;
            continue;
        }
        if (tok[0] == '\0') continue;   /* 连续 // */
        comp[cnt++] = tok;
        if (cnt >= (int)(sizeof(comp)/sizeof(comp[0]))) return -1;
    }

    size_t pos = 0;
    buf[pos++] = '/';
    for (int i = 0; i < cnt; ++i) {
        size_t l = strlen(comp[i]);
        if (pos + l + 1 + 1 > buf_sz) return -1; /* 留 / 和 \0 */
        memcpy(buf + pos, comp[i], l);
        pos += l;
        buf[pos++] = '/';
    }
    if (pos > 1) --pos;   /* 去掉末尾 / */
    buf[pos] = '\0';
    return 0;
}

int resolve_path(const char *ftp_root,
                 const char *current_path,
                 const char *user_control_path,
                 char       *output,
                 size_t      output_sz)
{
    char tmp1[PATH_MAX], tmp2[PATH_MAX], abs_local[PATH_MAX], resolved[PATH_MAX];

    if (!ftp_root || !current_path || !user_control_path || !output || output_sz == 0)
        return -1;

    /* 1. 正规化 current_path（客户端视角） */
    if (normalize_path(current_path, tmp1, sizeof(tmp1)) != 0)
        return -1;

    /* 2. 计算客户端视角的目标路径 */
    if (user_control_path[0] == '/') {
        /* 绝对路径：以 ftp 根为准 */
        if (normalize_path(user_control_path, tmp2, sizeof(tmp2)) != 0)
            return -1;
    } else {
        /* 相对路径：拼到 tmp1 后面 */
        int rc = snprintf(tmp2, sizeof(tmp2), "%s/%s", tmp1, user_control_path);
        if (rc < 0 || (size_t)rc >= sizeof(tmp2)) return -3;
        if (normalize_path(tmp2, tmp2, sizeof(tmp2)) != 0) return -1;
    }

    /* 3. 构造本地绝对路径 */
    int rc = snprintf(abs_local, sizeof(abs_local), "%s%s", ftp_root, tmp2);
    if (rc < 0 || (size_t)rc >= sizeof(abs_local)) return -3;

    /* 4. realpath 解析符号链接、多余的 . / .. */
    if (realpath(abs_local, resolved) == NULL) {
        /* 文件/目录不存在，仍要求不能穿透 */
        if (errno == ENOENT) {
            if (strncmp(abs_local, ftp_root, strlen(ftp_root)) != 0)
                return -2;
            if (strlen(abs_local) >= output_sz) return -3;
            strcpy(output, abs_local);
            return 0;
        }
        return -1;
    }

    /* 5. 必须位于 ftp_root 之下 */
    size_t root_len = strlen(ftp_root);
    if (strncmp(resolved, ftp_root, root_len) != 0)
        return -2;
    if (resolved[root_len] != '\0' && resolved[root_len] != '/')
        return -2;

    /* 6. 输出 */
    if (strlen(resolved) >= output_sz) return -3;
    strcpy(output, resolved);
    return 0;
}

/* ---------- 编译自测 ---------- */

#include <assert.h>
static void test(void)
{
    char out[PATH_MAX];
    const char *root = "/data/ftproot";

    assert(resolve_path(root, "/", "tmp", out, sizeof(out)) == 0);
    assert(strcmp(out, "/data/ftproot/tmp") == 0);

    assert(resolve_path(root, "/tmp", "abc/../def", out, sizeof(out)) == 0);
    assert(strcmp(out, "/data/ftproot/tmp/def") == 0);

    assert(resolve_path(root, "/tmp", "../etc", out, sizeof(out)) == -2);
    assert(resolve_path(root, "/tmp", "../../", out, sizeof(out)) == -2);

    assert(resolve_path(root, "/tmp", "a/./b//c", out, sizeof(out)) == 0);
    assert(strcmp(out, "/data/ftproot/tmp/a/b/c") == 0);

    /* 边界：输出缓冲区刚好够 */
    assert(resolve_path(root, "/", "x", out, strlen("/data/ftproot/x")+1) == 0);
    assert(resolve_path(root, "/", "x", out, strlen("/data/ftproot/x"))   == -3);

    printf("All tests passed.\n");
}
int main(void){ test(); return 0; }
