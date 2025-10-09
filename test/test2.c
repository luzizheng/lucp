#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

int resolve_path(const char *ftp_root, const char *current_path, 
                 const char *user_control_path, char *output, size_t output_sz) {
    
    // 参数检查
    if (ftp_root == NULL || current_path == NULL || 
        user_control_path == NULL || output == NULL || output_sz == 0) {
        errno = EINVAL;
        return -1;
    }
    
    // 确保FTP根目录是绝对路径
    if (ftp_root[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    
    // 规范化FTP根目录路径（移除末尾的/）
    char normalized_ftp_root[PATH_MAX];
    size_t ftp_root_len = strlen(ftp_root);
    
    if (ftp_root_len >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    
    strncpy(normalized_ftp_root, ftp_root, PATH_MAX);
    normalized_ftp_root[PATH_MAX - 1] = '\0';
    
    if (ftp_root_len > 1 && normalized_ftp_root[ftp_root_len - 1] == '/') {
        normalized_ftp_root[ftp_root_len - 1] = '\0';
        ftp_root_len--;
    }
    
    // 检查current_path是否在ftp_root内
    char real_ftp_root[PATH_MAX];
    char real_current_path[PATH_MAX];
    
    // 获取ftp_root的真实路径
    if (realpath(normalized_ftp_root, real_ftp_root) == NULL) {
        return -1; // FTP根目录不存在或无法访问
    }
    
    // 获取current_path的真实路径
    if (realpath(current_path, real_current_path) == NULL) {
        return -1; // 当前路径不存在或无法访问
    }
    
    // 确保current_path是ftp_root的子目录
    size_t real_ftp_root_len = strlen(real_ftp_root);
    if (strncmp(real_current_path, real_ftp_root, real_ftp_root_len) != 0) {
        errno = EACCES;
        return -1; // current_path不在ftp_root内
    }
    
    // 如果current_path正好是ftp_root，那么real_current_path应该等于real_ftp_root
    // 如果current_path是子目录，那么real_current_path[real_ftp_root_len]应该是'/'
    if (strlen(real_current_path) > real_ftp_root_len && 
        real_current_path[real_ftp_root_len] != '/') {
        errno = EACCES;
        return -1; // 路径格式异常
    }
    
    // 构建基础路径
    char base_path[PATH_MAX];
    if (user_control_path[0] == '/') {
        // 用户提供的是绝对路径（相对于FTP根目录）
        if (strlen(real_ftp_root) >= sizeof(base_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(base_path, real_ftp_root);
    } else {
        // 用户提供的是相对路径（相对于current_path）
        if (strlen(real_current_path) >= sizeof(base_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(base_path, real_current_path);
    }
    
    // 移除base_path末尾的/
    size_t base_len = strlen(base_path);
    if (base_len > 1 && base_path[base_len - 1] == '/') {
        base_path[base_len - 1] = '\0';
    }
    
    // 处理用户控制路径
    char temp_path[PATH_MAX * 2];
    char *components[PATH_MAX];
    int comp_count = 0;
    
    // 复制用户路径到临时缓冲区
    size_t user_path_len = strlen(user_control_path);
    if (user_path_len >= sizeof(temp_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(temp_path, user_control_path);
    
    // 分割路径组件
    char *token = strtok(temp_path, "/");
    while (token != NULL && comp_count < PATH_MAX - 1) {
        components[comp_count++] = token;
        token = strtok(NULL, "/");
    }
    components[comp_count] = NULL; // 结束标记
    
    // 构建最终路径
    char result_path[PATH_MAX * 2];
    if (strlen(base_path) >= sizeof(result_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(result_path, base_path);
    
    // 处理每个路径组件
    for (int i = 0; i < comp_count; i++) {
        char *comp = components[i];
        size_t result_len = strlen(result_path);
        
        // 安全检查：防止路径穿透攻击
        if (strcmp(comp, ".") == 0) {
            // 当前目录，跳过
            continue;
        } else if (strcmp(comp, "..") == 0) {
            // 上级目录，需要特殊处理
            
            // 检查是否尝试跳出FTP根目录
            if (strncmp(result_path, real_ftp_root, strlen(real_ftp_root)) != 0) {
                errno = EACCES;
                return -1;
            }
            
            // 找到最后一个/
            char *last_slash = strrchr(result_path, '/');
            if (last_slash != NULL) {
                // 确保不会删除FTP根目录
                size_t root_len = strlen(real_ftp_root);
                if (last_slash > result_path + root_len) {
                    *last_slash = '\0';
                } else if (strlen(result_path) > root_len) {
                    // 如果已经在FTP根目录，保持原样
                    result_path[root_len] = '\0';
                }
            }
        } else {
            // 正常路径组件
            size_t current_len = strlen(result_path);
            size_t comp_len = strlen(comp);
            
            // 检查路径长度限制
            if (current_len + comp_len + 2 >= sizeof(result_path)) {
                errno = ENAMETOOLONG;
                return -1;
            }
            
            // 添加路径分隔符（如果不是根目录）
            if (current_len > 0 && result_path[current_len - 1] != '/') {
                strcat(result_path, "/");
            }
            
            // 添加组件
            strcat(result_path, comp);
        }
    }
    
    // 最终安全检查：确保结果路径在FTP根目录内
    if (strncmp(result_path, real_ftp_root, strlen(real_ftp_root)) != 0) {
        errno = EACCES;
        return -1;
    }
    
    // 尝试使用realpath规范化路径
    char final_path[PATH_MAX];
    char *real_res = realpath(result_path, final_path);
    if (real_res == NULL) {
        // realpath失败，路径可能不存在，但我们仍然检查安全性
        // 使用我们构建的路径但进行基本检查
        if (strlen(result_path) >= sizeof(final_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(final_path, result_path);
    }
    
    // 再次验证最终路径在FTP根目录内
    if (strncmp(final_path, real_ftp_root, strlen(real_ftp_root)) != 0) {
        errno = EACCES;
        return -1;
    }
    
    // 检查输出缓冲区大小
    size_t final_len = strlen(final_path);
    if (final_len >= output_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    
    // 复制结果到输出缓冲区
    strcpy(output, final_path);
    return 0;
}

// 测试用例
#include <assert.h>

void test_resolve_path() {
    char output[PATH_MAX];
    int ret;
    
    printf("Starting tests...\n");
    
    // 创建测试目录结构
    system("mkdir -p /tmp/test_ftp_root/data /tmp/test_ftp_root/tmp /tmp/test_ftp_root/uploads");
    system("mkdir -p /tmp/outside_dir"); // 创建一个外部目录用于测试
    
    // 测试用例1：基本相对路径
    ret = resolve_path("/tmp/test_ftp_root", "/tmp/test_ftp_root/tmp", "abc", output, sizeof(output));
    assert(ret == 0);
    assert(strcmp(output, "/tmp/test_ftp_root/tmp/abc") == 0);
    printf("Test 1 passed: %s\n", output);
    
    // 测试用例2：current_path就是ftp_root
    ret = resolve_path("/tmp/test_ftp_root", "/tmp/test_ftp_root", "uploads", output, sizeof(output));
    assert(ret == 0);
    assert(strcmp(output, "/tmp/test_ftp_root/uploads") == 0);
    printf("Test 2 passed: %s\n", output);
    
    // 测试用例3：路径穿透攻击防护
    ret = resolve_path("/tmp/test_ftp_root", "/tmp/test_ftp_root/tmp", "../../etc/passwd", output, sizeof(output));
    assert(ret == -1); // 应该失败
    printf("Test 3 passed: Path traversal correctly blocked\n");
    
    // 测试用例4：非法的current_path（不在ftp_root内）
    ret = resolve_path("/tmp/test_ftp_root", "/tmp/outside_dir", "any", output, sizeof(output));
    assert(ret == -1); // 应该失败
    printf("Test 4 passed: Invalid current_path correctly rejected\n");
    
    // 测试用例5：符号链接攻击防护（如果存在符号链接）
    system("ln -sf /etc /tmp/test_ftp_root/link_to_etc 2>/dev/null || true");
    ret = resolve_path("/tmp/test_ftp_root", "/tmp/test_ftp_root", "link_to_etc/passwd", output, sizeof(output));
    // 这个测试结果取决于系统配置，但至少不会返回/etc/passwd
    printf("Test 5 completed (result depends on system): %d\n", ret);
    
    // 测试用例6：边界情况 - 空路径
    ret = resolve_path("/tmp/test_ftp_root", "/tmp/test_ftp_root/tmp", "", output, sizeof(output));
    assert(ret == 0);
    assert(strcmp(output, "/tmp/test_ftp_root/tmp") == 0);
    printf("Test 6 passed: %s\n", output);
    
    // 测试用例7：复杂的相对路径
    ret = resolve_path("/tmp/test_ftp_root", "/tmp/test_ftp_root/tmp", "a/b/../c/./d", output, sizeof(output));
    assert(ret == 0);
    assert(strcmp(output, "/tmp/test_ftp_root/tmp/a/c/d") == 0);
    printf("Test 7 passed: %s\n", output);
    
    // 清理测试目录
    system("rm -rf /tmp/test_ftp_root /tmp/outside_dir");
    
    printf("All tests passed!\n");
}

int main() {
    test_resolve_path();
    return 0;
}
