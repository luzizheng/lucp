/*
 * 完整FTP服务器实现
 * 支持的核心命令：USER, PASS, QUIT, PWD, CWD, PASV, LIST, RETR
 * 支持的扩展命令：SYST, FEAT, SIZE, TYPE
 * 遵循FTP协议标准(RFC 959及相关扩展)
 */

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <sys/time.h>
#include <signal.h>

// 传输模式枚举
typedef enum {
    TRANSFER_ASCII,   // ASCII模式（默认）- 用于文本文件，自动转换换行符
    TRANSFER_BINARY   // 二进制模式 - 用于所有文件，原样传输
} TransferType;

// FTP服务器配置结构体
typedef struct {
    char ip[16];           // 服务器绑定IP
    uint16_t port;         // 控制连接端口（默认21）
    char root_dir[256];    // FTP根目录
    int max_connections;   // 最大并发连接数
    int data_port_min;     // 数据连接端口范围最小值
    int data_port_max;     // 数据连接端口范围最大值
} FtpServerConfig;

// 客户端连接信息结构体
typedef struct {
    int control_sock;              // 控制连接socket
    int data_sock;                 // 数据连接socket
    int pasv_sock;                 // 被动模式监听socket
    struct sockaddr_in client_addr; // 客户端地址信息
    pthread_t thread_id;           // 处理线程ID
    int is_active;                 // 连接是否活跃
    char cwd[PATH_MAX];            // 当前工作目录
    FtpServerConfig *config;       // 服务器配置指针
    TransferType transfer_type;    // 当前传输模式
} Client_t;

// 命令处理函数指针类型
typedef int (*CmdHandlerFunc)(Client_t *, const char *);

// 命令处理结构体
typedef struct {
    const char *cmd;          // 命令字符串
    CmdHandlerFunc handler;   // 对应的处理函数
} CmdHandler;

// 全局变量
Client_t *clients;               // 客户端数组
int client_count = 0;            // 当前连接数
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;  // 客户端操作互斥锁
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;      // 日志操作互斥锁
int *free_client_indices;        // 空闲客户端索引列表
int free_count;                  // 空闲客户端数量

// 函数声明
void init_signals();
int init_server(FtpServerConfig *config);
void *handle_client(void *arg);
int send_response(int sock, int code, const char *fmt, ...);
int parse_command(char *buf, char *cmd, char *arg);
int handle_cwd(Client_t *client, const char *path);
int handle_list(Client_t *client);
int handle_retr(Client_t *client, const char *filename);
int enter_pasv_mode(Client_t *client);
int create_data_connection(Client_t *client);
void close_client(Client_t *client);
char* resolve_path(Client_t *client, const char *path, char *resolved);
int handle_quit(Client_t *client, const char *arg);
int handle_pwd(Client_t *client, const char *arg);
int handle_syst(Client_t *client, const char *arg);
int handle_feat(Client_t *client, const char *arg);
int handle_size(Client_t *client, const char *filename);
int handle_type(Client_t *client, const char *arg);

// 日志函数声明
void LOG_ERROR(const char *fmt, ...);
void LOG_WARN(const char *fmt, ...);
void LOG_INFO(const char *fmt, ...);

// 命令处理表（按字母顺序排列）
CmdHandler cmd_handlers[] = {
    {"CWD", handle_cwd},
    {"FEAT", handle_feat},
    {"LIST", handle_list},
    {"PASV", enter_pasv_mode},
    {"PWD", handle_pwd},
    {"QUIT", handle_quit},
    {"RETR", handle_retr},
    {"SIZE", handle_size},
    {"SYST", handle_syst},
    {"TYPE", handle_type},
    {NULL, NULL}  // 结束标志
};

// 主函数 - 服务器初始化与主循环
int main(int argc, char *argv[]) {
    // 初始化信号处理（忽略SIGPIPE避免客户端断开时崩溃）
    init_signals();
    
    // 服务器默认配置
    FtpServerConfig config = {
        .ip = "0.0.0.0",        // 绑定所有可用接口
        .port = 21,             // 默认FTP控制端口
        .root_dir = "./ftp_root",// FTP根目录
        .max_connections = 10,  // 最大并发连接数
        .data_port_min = 2000,  // 数据端口范围起始
        .data_port_max = 2100   // 数据端口范围结束
    };
    
    // 检查并创建根目录（如果不存在）
    struct stat st;
    if (stat(config.root_dir, &st) == -1) {
        if (mkdir(config.root_dir, 0755) == -1) {
            LOG_ERROR("Failed to create root directory: %s", strerror(errno));
            return 1;
        }
        LOG_INFO("Created root directory: %s", config.root_dir);
    } else if (!S_ISDIR(st.st_mode)) {
        LOG_ERROR("%s is not a directory", config.root_dir);
        return 1;
    }
    
    // 初始化客户端数组和空闲列表
    clients = malloc(sizeof(Client_t) * config.max_connections);
    free_client_indices = malloc(config.max_connections * sizeof(int));
    if (!clients || !free_client_indices) {
        LOG_ERROR("Failed to allocate memory for clients");
        free(clients);
        free(free_client_indices);
        return 1;
    }
    
    memset(clients, 0, sizeof(Client_t) * config.max_connections);
    for (int i = 0; i < config.max_connections; i++) {
        free_client_indices[i] = i;
        clients[i].control_sock = -1;
        clients[i].data_sock = -1;
        clients[i].pasv_sock = -1;
    }
    free_count = config.max_connections;
    
    // 初始化服务器socket
    int server_sock = init_server(&config);
    if (server_sock == -1) {
        LOG_ERROR("Failed to initialize server");
        free(clients);
        free(free_client_indices);
        return 1;
    }
    
    LOG_INFO("FTP server started on %s:%d, root directory: %s", 
             config.ip, config.port, config.root_dir);
    
    // 主循环 - 接受客户端连接
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_sock == -1) {
            LOG_WARN("Failed to accept connection: %s", strerror(errno));
            continue;
        }
        
        // 检查是否达到最大连接数
        pthread_mutex_lock(&clients_mutex);
        if (client_count >= config.max_connections) {
            LOG_WARN("Max connections reached. Rejecting new connection from %s", 
                     inet_ntoa(client_addr.sin_addr));
            send_response(client_sock, 421, "Too many connections");
            close(client_sock);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        
        // 获取空闲客户端索引
        int i = free_client_indices[--free_count];
        
        // 初始化客户端信息
        clients[i].control_sock = client_sock;
        clients[i].data_sock = -1;
        clients[i].pasv_sock = -1;
        clients[i].client_addr = client_addr;
        clients[i].is_active = 1;
        strcpy(clients[i].cwd, config.root_dir);
        clients[i].config = &config;
        clients[i].transfer_type = TRANSFER_ASCII;  // 默认ASCII模式
        
        client_count++;
        pthread_mutex_unlock(&clients_mutex);
        
        LOG_INFO("New connection from %s:%d (client %d)", 
                 inet_ntoa(client_addr.sin_addr), 
                 ntohs(client_addr.sin_port), i);
        
        // 发送欢迎信息
        send_response(client_sock, 220, "Welcome to simple FTP server");
        
        // 创建线程处理客户端
        if (pthread_create(&clients[i].thread_id, NULL, handle_client, &clients[i]) != 0) {
            LOG_ERROR("Failed to create thread for client %d", i);
            pthread_mutex_lock(&clients_mutex);
            close_client(&clients[i]);
            free_client_indices[free_count++] = i;
            client_count--;
            pthread_mutex_unlock(&clients_mutex);
        }
    }
    
    // 清理资源（理论上不会执行到这里）
    close(server_sock);
    free(clients);
    free(free_client_indices);
    return 0;
}

// 初始化信号处理
void init_signals() {
    signal(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE信号
}

// 初始化服务器socket
int init_server(FtpServerConfig *config) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // 设置socket选项，允许端口重用
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        LOG_WARN("Failed to set socket options: %s", strerror(errno));
    }
    
    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config->ip);
    server_addr.sin_port = htons(config->port);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        close(server_sock);
        return -1;
    }
    
    // 开始监听
    if (listen(server_sock, 5) == -1) {
        LOG_ERROR("Failed to listen on socket: %s", strerror(errno));
        close(server_sock);
        return -1;
    }
    
    return server_sock;
}

// 处理客户端连接（线程函数）
void *handle_client(void *arg) {
    Client_t *client = (Client_t *)arg;
    char buf[1024];
    int bytes_read;
    char cmd[16], cmd_arg[1024];
    int client_index = -1;
    
    // 分离线程，不需要主线程等待
    pthread_detach(pthread_self());
    
    // 获取客户端索引
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client->config->max_connections; i++) {
        if (&clients[i] == client) {
            client_index = i;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    // 处理登录（匿名登录流程）
    while (1) {
        memset(buf, 0, sizeof(buf));
        bytes_read = recv(client->control_sock, buf, sizeof(buf) - 1, 0);
        
        if (bytes_read <= 0) {
            if (bytes_read < 0) {
                LOG_WARN("Error reading from client %s: %s", 
                         inet_ntoa(client->client_addr.sin_addr), strerror(errno));
            } else {
                LOG_INFO("Client %s disconnected during login", 
                         inet_ntoa(client->client_addr.sin_addr));
            }
            goto cleanup;
        }
        
        // 移除换行符
        buf[strcspn(buf, "\r\n")] = '\0';
        LOG_INFO("Received from %s: %s", inet_ntoa(client->client_addr.sin_addr), buf);
        
        // 解析命令
        if (parse_command(buf, cmd, cmd_arg) != 0) {
            send_response(client->control_sock, 500, "Syntax error, command unrecognized");
            continue;
        }
        
        // 处理USER和PASS命令进行登录
        if (strcasecmp(cmd, "USER") == 0) {
            send_response(client->control_sock, 331, "User name okay, need password");
        } else if (strcasecmp(cmd, "PASS") == 0) {
            send_response(client->control_sock, 230, "User logged in, proceed");
            break; // 登录成功，退出登录处理循环
        } else {
            send_response(client->control_sock, 503, "Bad sequence of commands");
        }
    }
    
    // 处理FTP命令（登录后）
    while (1) {
        memset(buf, 0, sizeof(buf));
        bytes_read = recv(client->control_sock, buf, sizeof(buf) - 1, 0);
        
        if (bytes_read <= 0) {
            if (bytes_read < 0) {
                LOG_WARN("Error reading from client %s: %s", 
                         inet_ntoa(client->client_addr.sin_addr), strerror(errno));
            } else {
                LOG_INFO("Client %s disconnected", 
                         inet_ntoa(client->client_addr.sin_addr));
            }
            break;
        }
        
        // 移除换行符
        buf[strcspn(buf, "\r\n")] = '\0';
        LOG_INFO("Received from %s: %s", inet_ntoa(client->client_addr.sin_addr), buf);
        
        // 解析命令
        if (parse_command(buf, cmd, cmd_arg) != 0) {
            send_response(client->control_sock, 500, "Syntax error, command unrecognized");
            continue;
        }
        
        // 查找并调用命令处理函数
        int cmd_handled = 0;
        for (int i = 0; cmd_handlers[i].cmd; i++) {
            if (strcasecmp(cmd, cmd_handlers[i].cmd) == 0) {
                cmd_handlers[i].handler(client, cmd_arg);
                cmd_handled = 1;
                break;
            }
        }
        
        // 未处理的命令
        if (!cmd_handled) {
            send_response(client->control_sock, 502, "Command not implemented");
        }
        
        // 如果是QUIT命令，退出循环
        if (strcasecmp(cmd, "QUIT") == 0) {
            break;
        }
    }
    
cleanup:
    // 清理客户端资源
    pthread_mutex_lock(&clients_mutex);
    close_client(client);
    if (client_index != -1) {
        free_client_indices[free_count++] = client_index;
    }
    client_count--;
    pthread_mutex_unlock(&clients_mutex);
    
    LOG_INFO("Client %s disconnected", inet_ntoa(client->client_addr.sin_addr));
    return NULL;
}

// 发送响应给客户端
int send_response(int sock, int code, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf) - 4, fmt, args); // 预留空间给代码和换行
    va_end(args);
    
    // 格式化响应: 代码 + 空格 + 消息 + CRLF
    char response[2048];
    snprintf(response, sizeof(response), "%d %s\r\n", code, buf);
    
    LOG_INFO("Sending response: %d %s", code, buf);
    
    if (send(sock, response, strlen(response), 0) == -1) {
        LOG_WARN("Failed to send response: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

// 解析命令和参数
int parse_command(char *buf, char *cmd, char *arg) {
    // 初始化输出缓冲区
    memset(cmd, 0, sizeof(cmd));
    memset(arg, 0, sizeof(arg));
    
    // 跳过前导空格
    while (*buf && isspace(*buf)) {
        buf++;
    }
    
    // 提取命令（转为大写）
    int i = 0;
    while (*buf && !isspace(*buf) && i < sizeof(cmd) - 1) {
        cmd[i++] = toupper(*buf++);
    }
    cmd[i] = '\0';
    
    // 跳过命令后的空格
    while (*buf && isspace(*buf)) {
        buf++;
    }
    
    // 提取参数并检查非法字符
    i = 0;
    while (*buf && i < sizeof(arg) - 1) {
        if (*buf < 32 || *buf > 126) {  // 只允许可见ASCII字符
            LOG_WARN("Invalid character in argument: %d", (unsigned char)*buf);
            break;
        }
        arg[i++] = *buf++;
    }
    arg[i] = '\0';
    
    // 如果没有命令，返回错误
    if (cmd[0] == '\0') {
        return -1;
    }
    
    return 0;
}

// 解析路径，处理相对路径和绝对路径，防止目录遍历
char* resolve_path(Client_t *client, const char *path, char *resolved) {
    if (!path || *path == '\0') {
        strncpy(resolved, client->cwd, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
        return resolved;
    }
    
    // 处理绝对路径和相对路径
    if (path[0] == '/') {
        // 绝对路径，相对于根目录
        strncpy(resolved, client->config->root_dir, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
        
        // 确保有足够空间添加路径
        size_t root_len = strlen(resolved);
        size_t path_len = strlen(path);
        if (root_len + path_len + 1 < PATH_MAX) {
            strncat(resolved, path, PATH_MAX - root_len - 1);
        } else {
            // 路径太长，截断
            LOG_WARN("Path too long: %s%s", client->config->root_dir, path);
            return NULL;
        }
    } else {
        // 相对路径
        strncpy(resolved, client->cwd, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
        
        // 确保有足够空间添加路径分隔符和路径
        size_t cwd_len = strlen(resolved);
        size_t path_len = strlen(path);
        if (cwd_len + 1 + path_len < PATH_MAX) {
            strncat(resolved, "/", PATH_MAX - cwd_len - 1);
            strncat(resolved, path, PATH_MAX - strlen(resolved) - 1);
        } else {
            // 路径太长，截断
            LOG_WARN("Path too long: %s/%s", client->cwd, path);
            return NULL;
        }
    }
    
    // 处理路径中的.和..
    char *p, *q, *dotdot;
    char temp[PATH_MAX];
    
    strncpy(temp, resolved, PATH_MAX - 1);
    temp[PATH_MAX - 1] = '\0';
    p = temp;
    q = resolved;
    dotdot = resolved;
    
    if (*p == '/') {
        *q++ = *p++;
        dotdot = q;
    }
    
    while (*p) {
        if (strncmp(p, "/./", 3) == 0) {
            p += 2;
        } else if (strncmp(p, "/../", 4) == 0) {
            p += 3;
            if (dotdot > resolved) {
                for (q--; q > resolved && *q != '/'; q--);
            } else {
                *q++ = '/';
            }
        } else if (*p == '/' && *(p+1) == '\0') {
            break;
        } else {
            *q++ = *p++;
            if (*(q-1) == '/') {
                dotdot = q;
            }
        }
    }
    
    *q = '\0';
    
    // 如果路径为空，设置为根目录
    if (q == resolved) {
        *q++ = '/';
        *q = '\0';
    }
    
    // 强制获取绝对路径并检查是否在根目录下
    char *abs_path = realpath(resolved, NULL);
    if (!abs_path) {
        LOG_WARN("resolve_path: Invalid path %s", resolved);
        return NULL;
    }
    
    // 检查是否在根目录下（防止目录遍历攻击）
    if (strncmp(abs_path, client->config->root_dir, strlen(client->config->root_dir)) != 0) {
        free(abs_path);
        LOG_WARN("resolve_path: Path %s is outside root directory", abs_path);
        return NULL;
    }
    
    strncpy(resolved, abs_path, PATH_MAX-1);
    free(abs_path);
    return resolved;
}

// 处理CWD命令（改变当前目录）
int handle_cwd(Client_t *client, const char *path) {
    char new_path[PATH_MAX];
    if (!resolve_path(client, path, new_path)) {
        return send_response(client->control_sock, 550, "Failed to change directory");
    }
    
    // 检查路径是否存在且是目录
    struct stat st;
    if (stat(new_path, &st) == -1) {
        LOG_WARN("CWD: Path %s does not exist: %s", new_path, strerror(errno));
        return send_response(client->control_sock, 550, "Failed to change directory");
    }
    
    if (!S_ISDIR(st.st_mode)) {
        LOG_WARN("CWD: %s is not a directory", new_path);
        return send_response(client->control_sock, 550, "Failed to change directory");
    }
    
    // 更新当前工作目录
    strncpy(client->cwd, new_path, sizeof(client->cwd) - 1);
    client->cwd[sizeof(client->cwd) - 1] = '\0';
    
    LOG_INFO("CWD: Client %s changed directory to %s", 
             inet_ntoa(client->client_addr.sin_addr), client->cwd);
    
    return send_response(client->control_sock, 250, "Directory successfully changed");
}

// 处理LIST命令（列出目录内容）
int handle_list(Client_t *client) {
    DIR *dir = opendir(client->cwd);
    if (!dir) {
        LOG_WARN("LIST: Failed to open directory %s: %s", client->cwd, strerror(errno));
        return -1;
    }
    
    struct dirent *entry;
    char list_entry[1024];
    int ret = -1;  // 默认失败
    
    while ((entry = readdir(dir)) != NULL) {
        // 跳过.和..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 获取文件信息
        char full_path[PATH_MAX];
        strncpy(full_path, client->cwd, PATH_MAX - 1);
        full_path[PATH_MAX - 1] = '\0';
        
        // 安全地拼接路径
        size_t cwd_len = strlen(full_path);
        size_t name_len = strlen(entry->d_name);
        if (cwd_len + 1 + name_len < PATH_MAX) {
            strncat(full_path, "/", PATH_MAX - cwd_len - 1);
            strncat(full_path, entry->d_name, PATH_MAX - strlen(full_path) - 1);
        } else {
            LOG_WARN("Path too long: %s/%s", client->cwd, entry->d_name);
            continue;
        }
        
        struct stat st;
        if (stat(full_path, &st) == -1) {
            LOG_WARN("LIST: Failed to get stat for %s: %s", full_path, strerror(errno));
            continue;
        }
        
        // 格式化列表项（类似ls -l的格式）
        // 文件类型和权限
        char permissions[11] = "----------";
        if (S_ISDIR(st.st_mode)) permissions[0] = 'd';
        if (st.st_mode & S_IRUSR) permissions[1] = 'r';
        if (st.st_mode & S_IWUSR) permissions[2] = 'w';
        if (st.st_mode & S_IXUSR) permissions[3] = 'x';
        if (st.st_mode & S_IRGRP) permissions[4] = 'r';
        if (st.st_mode & S_IWGRP) permissions[5] = 'w';
        if (st.st_mode & S_IXGRP) permissions[6] = 'x';
        if (st.st_mode & S_IROTH) permissions[7] = 'r';
        if (st.st_mode & S_IWOTH) permissions[8] = 'w';
        if (st.st_mode & S_IXOTH) permissions[9] = 'x';
        
        // 时间格式
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%b %d %H:%M", localtime(&st.st_mtime));
        
        // 组合列表项
        snprintf(list_entry, sizeof(list_entry), 
                 "%s %3ld %8d %8d %8lld %s %s\r\n",
                 permissions, st.st_nlink, st.st_uid, st.st_gid,
                 (long long)st.st_size, time_str, entry->d_name);
        
        // 发送列表项
        if (send(client->data_sock, list_entry, strlen(list_entry), 0) == -1) {
            LOG_WARN("LIST: Failed to send data: %s", strerror(errno));
            goto cleanup;
        }
    }
    
    ret = 0;  // 成功完成
    
cleanup:
    closedir(dir);
    return ret;
}

// 处理RETR命令（下载文件）
int handle_retr(Client_t *client, const char *filename) {
    if (!filename || *filename == '\0') {
        LOG_WARN("RETR: No filename specified");
        return send_response(client->control_sock, 501, "Syntax error in parameters or arguments");
    }
    
    // 构建完整路径
    char full_path[PATH_MAX];
    if (!resolve_path(client, filename, full_path)) {
        return send_response(client->control_sock, 550, "File not found or access denied");
    }
    
    // 打开文件（二进制模式打开，避免系统自动转换换行符）
    int fd = open(full_path, O_RDONLY);
    if (fd == -1) {
        LOG_WARN("RETR: Failed to open file %s: %s", full_path, strerror(errno));
        return send_response(client->control_sock, 550, "File not found");
    }
    
    // 获取文件信息
    struct stat st;
    if (fstat(fd, &st) == -1) {
        LOG_WARN("RETR: Failed to get file stats: %s", strerror(errno));
        close(fd);
        return send_response(client->control_sock, 550, "Failed to read file");
    }
    
    // 检查是否是常规文件
    if (!S_ISREG(st.st_mode)) {
        LOG_WARN("RETR: %s is not a regular file", full_path);
        close(fd);
        return send_response(client->control_sock, 550, "Not a regular file");
    }
    
    // 根据传输模式发送响应
    if (client->transfer_type == TRANSFER_BINARY) {
        send_response(client->control_sock, 150, "Opening BINARY mode data connection for file transfer");
        LOG_INFO("RETR: Starting binary transfer of %s (%lld bytes)", full_path, (long long)st.st_size);
    } else {
        send_response(client->control_sock, 150, "Opening ASCII mode data connection for file transfer");
        LOG_INFO("RETR: Starting ASCII transfer of %s (with newline conversion)", full_path);
    }
    
    int ret = -1;  // 默认失败
    if (client->transfer_type == TRANSFER_BINARY) {
        // 二进制模式：直接传输原始数据（高效）
        off_t offset = 0;
        ssize_t sent;
        #define BINARY_BUFFER_SIZE (64 * 1024)  // 64KB块传输
        
        while (offset < st.st_size) {
            size_t chunk = (st.st_size - offset) > BINARY_BUFFER_SIZE ? 
                          BINARY_BUFFER_SIZE : (st.st_size - offset);
            
            sent = sendfile(client->data_sock, fd, &offset, chunk);
            if (sent == -1) {
                LOG_WARN("RETR: Binary transfer failed: %s", strerror(errno));
                send_response(client->control_sock, 451, "Transfer failed");
                goto cleanup;
            } else if (sent == 0) {
                break;  // 传输完成
            }
        }
        
        if (offset == st.st_size) {
            ret = 0;  // 传输成功
        }
    } else {
        // ASCII模式：需转换换行符（\n -> \r\n）
        #define ASCII_BUFFER_SIZE 1024
        char in_buf[ASCII_BUFFER_SIZE];
        char out_buf[ASCII_BUFFER_SIZE * 2];  // 预留转换空间
        ssize_t bytes_read;
        size_t out_len;
        
        while ((bytes_read = read(fd, in_buf, ASCII_BUFFER_SIZE)) > 0) {
            out_len = 0;
            // 逐字节处理，替换\n为\r\n
            for (ssize_t i = 0; i < bytes_read; i++) {
                if (in_buf[i] == '\n') {
                    // 先写\r，再写\n
                    if (out_len + 2 <= sizeof(out_buf)) {
                        out_buf[out_len++] = '\r';
                        out_buf[out_len++] = '\n';
                    } else {
                        LOG_WARN("RETR: ASCII buffer overflow");
                        send_response(client->control_sock, 451, "Buffer overflow");
                        goto cleanup;
                    }
                } else {
                    // 其他字符直接复制
                    if (out_len + 1 <= sizeof(out_buf)) {
                        out_buf[out_len++] = in_buf[i];
                    } else {
                        LOG_WARN("RETR: ASCII buffer overflow");
                        send_response(client->control_sock, 451, "Buffer overflow");
                        goto cleanup;
                    }
                }
            }
            
            // 发送转换后的内容
            if (send(client->data_sock, out_buf, out_len, 0) != (ssize_t)out_len) {
                LOG_WARN("RETR: ASCII transfer failed: %s", strerror(errno));
                send_response(client->control_sock, 451, "Transfer failed");
                goto cleanup;
            }
        }
        
        if (bytes_read == -1) {
            LOG_WARN("RETR: Error reading file: %s", strerror(errno));
            send_response(client->control_sock, 451, "Read error");
            goto cleanup;
        }
        
        ret = 0;  // ASCII传输成功
    }
    
    // 传输成功的响应
    if (ret == 0) {
        send_response(client->control_sock, 226, "Transfer complete");
        LOG_INFO("RETR: Completed %s transfer of %s", 
                 (client->transfer_type == TRANSFER_BINARY ? "binary" : "ASCII"), 
                 full_path);
    }
    
cleanup:
    close(fd);
    return ret;
}

// 处理PASV命令（进入被动模式）
int enter_pasv_mode(Client_t *client) {
    // 关闭已有的PASV socket
    if (client->pasv_sock != -1) {
        close(client->pasv_sock);
        client->pasv_sock = -1;
    }
    
    // 创建数据连接socket
    client->pasv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->pasv_sock == -1) {
        LOG_WARN("PASV: Failed to create socket: %s", strerror(errno));
        return send_response(client->control_sock, 425, "Can't open data connection");
    }
    
    // 设置socket选项
    int opt = 1;
    if (setsockopt(client->pasv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        LOG_WARN("PASV: Failed to set socket options: %s", strerror(errno));
    }
    
    // 绑定到一个可用端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // 绑定到所有可用接口
    
    int port;
    int bound = 0;
    
    // 尝试在端口范围内绑定
    for (port = client->config->data_port_min; port <= client->config->data_port_max; port++) {
        addr.sin_port = htons(port);
        if (bind(client->pasv_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            bound = 1;
            break;
        }
    }
    
    if (!bound) {
        LOG_WARN("PASV: Failed to bind to any port in range %d-%d: %s",
                 client->config->data_port_min, client->config->data_port_max, strerror(errno));
        close(client->pasv_sock);
        client->pasv_sock = -1;
        return send_response(client->control_sock, 425, "Can't open data connection");
    }
    
    // 开始监听
    if (listen(client->pasv_sock, 1) == -1) {
        LOG_WARN("PASV: Failed to listen on socket: %s", strerror(errno));
        close(client->pasv_sock);
        client->pasv_sock = -1;
        return send_response(client->control_sock, 425, "Can't open data connection");
    }
    
    // 获取服务器实际IP地址（从控制连接的socket中获取）
    struct sockaddr_in server_addr;
    socklen_t server_len = sizeof(server_addr);
    if (getsockname(client->control_sock, (struct sockaddr *)&server_addr, &server_len) == -1) {
        LOG_WARN("PASV: Failed to get server address from control socket: %s", strerror(errno));
        close(client->pasv_sock);
        client->pasv_sock = -1;
        return send_response(client->control_sock, 425, "Can't open data connection");
    }
    
    // 构造并发送PASV响应
    uint32_t ip = ntohl(server_addr.sin_addr.s_addr);
    uint16_t data_port = ntohs(addr.sin_port);
    int p1 = data_port / 256;
    int p2 = data_port % 256;
    
    if (send_response(client->control_sock, 227, 
                     "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
                     (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                     (ip >> 8) & 0xFF, ip & 0xFF, p1, p2) != 0) {
        LOG_WARN("PASV: Failed to send response");
        close(client->pasv_sock);
        client->pasv_sock = -1;
        return -1;
    }
    
    LOG_INFO("PASV: Entered passive mode on %s:%d", 
             inet_ntoa(server_addr.sin_addr), data_port);
    return 0;
}

// 创建数据连接
int create_data_connection(Client_t *client) {
    if (client->pasv_sock == -1) {
        LOG_WARN("create_data_connection: PASV mode not active");
        return -1;
    }
    
    // 设置超时，防止客户端不建立数据连接
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    
    if (setsockopt(client->pasv_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        LOG_WARN("create_data_connection: Failed to set socket timeout: %s", strerror(errno));
    }
    
    // 接受数据连接
    struct sockaddr_in data_addr;
    socklen_t data_len = sizeof(data_addr);
    client->data_sock = accept(client->pasv_sock, (struct sockaddr *)&data_addr, &data_len);
    
    if (client->data_sock == -1) {
        LOG_WARN("create_data_connection: Failed to accept data connection: %s", strerror(errno));
        return -1;
    }
    
    LOG_INFO("create_data_connection: Data connection established with %s:%d",
             inet_ntoa(data_addr.sin_addr), ntohs(data_addr.sin_port));
    
    return 0;
}

// 关闭客户端连接并清理资源
void close_client(Client_t *client) {
    if (client->control_sock != -1) {
        close(client->control_sock);
        client->control_sock = -1;
    }
    
    if (client->data_sock != -1) {
        close(client->data_sock);
        client->data_sock = -1;
    }
    
    if (client->pasv_sock != -1) {
        close(client->pasv_sock);
        client->pasv_sock = -1;
    }
    
    client->is_active = 0;
    memset(client->cwd, 0, sizeof(client->cwd));
}

// 处理QUIT命令
int handle_quit(Client_t *client, const char *arg) {
    send_response(client->control_sock, 221, "Goodbye");
    return 0;
}

// 处理PWD命令
int handle_pwd(Client_t *client, const char *arg) {
    // 计算相对于根目录的路径
    char rel_path[PATH_MAX];
    if (strlen(client->cwd) > strlen(client->config->root_dir)) {
        strcpy(rel_path, client->cwd + strlen(client->config->root_dir));
    } else {
        strcpy(rel_path, "/");
    }
    return send_response(client->control_sock, 257, "\"%s\" is current directory", rel_path);
}

// 处理SYST指令（返回系统类型）
int handle_syst(Client_t *client, const char *arg) {
    // 按照FTP规范，返回"UNIX Type: L8"表示类Unix系统，8位数据
    return send_response(client->control_sock, 215, "UNIX Type: L8");
}

// 处理FEAT指令（返回服务器支持的特性）
int handle_feat(Client_t *client, const char *arg) {
    // 构建符合标准的多行响应
    char response[1024] = {0};
    size_t len = 0;
    
    // 起始行：211-开头，包含"Features:"
    len += snprintf(response + len, sizeof(response) - len, 
                   "211-Features:\r\n");
    
    // 特性列表：每行一个特性，以空格开头
    const char *features[] = {
        " PASV",          // 支持被动模式
        " SIZE",          // 支持文件大小查询
        " SYST",          // 支持系统类型查询
        " FEAT",          // 支持特性列表查询
        " CWD",           // 支持更改目录
        " PWD",           // 支持显示当前目录
        " LIST",          // 支持文件列表
        " RETR",          // 支持文件下载
        " TYPE",          // 支持传输模式切换
        NULL              // 结束标志
    };
    
    // 拼接所有特性
    for (int i = 0; features[i] != NULL; i++) {
        len += snprintf(response + len, sizeof(response) - len,
                       "%s\r\n", features[i]);
    }
    
    // 结束行：211开头，包含"End"
    len += snprintf(response + len, sizeof(response) - len,
                   "211 End\r\n");
    
    // 一次性发送完整响应
    if (send(client->control_sock, response, len, 0) == -1) {
        LOG_WARN("FEAT: Failed to send features: %s", strerror(errno));
        return -1;
    }
    
    LOG_INFO("FEAT: Sent feature list to client %s",
             inet_ntoa(client->client_addr.sin_addr));
    return 0;
}

// 处理SIZE指令（返回指定文件大小）
int handle_size(Client_t *client, const char *filename) {
    // 检查是否提供了文件名参数
    if (!filename || *filename == '\0') {
        return send_response(client->control_sock, 501, "Syntax error in parameters or arguments");
    }
    
    // 解析并验证文件路径
    char full_path[PATH_MAX];
    if (!resolve_path(client, filename, full_path)) {
        return send_response(client->control_sock, 550, "File not found or access denied");
    }
    
    // 获取文件信息
    struct stat st;
    if (stat(full_path, &st) == -1) {
        return send_response(client->control_sock, 550, "File not found");
    }
    
    // 检查是否为常规文件
    if (!S_ISREG(st.st_mode)) {
        return send_response(client->control_sock, 550, "Not a regular file");
    }
    
    // 返回文件大小（以字节为单位）
    return send_response(client->control_sock, 213, "%lld", (long long)st.st_size);
}

// 处理TYPE命令（设置传输模式）
int handle_type(Client_t *client, const char *arg) {
    // 参数校验：必须提供类型参数（如"A"或"I"）
    if (!arg || *arg == '\0') {
        return send_response(client->control_sock, 501, "Syntax error in parameters or arguments");
    }

    // 解析参数（忽略大小写）
    if (strcasecmp(arg, "I") == 0) {
        // 设置为二进制模式（Image模式）
        client->transfer_type = TRANSFER_BINARY;
        return send_response(client->control_sock, 200, "Type set to I");
    } else if (strcasecmp(arg, "A") == 0) {
        // 设置为ASCII模式（默认模式）
        client->transfer_type = TRANSFER_ASCII;
        return send_response(client->control_sock, 200, "Type set to A");
    } else {
        // 不支持的类型
        return send_response(client->control_sock, 504, "Command not implemented for that parameter");
    }
}

// 日志函数实现
void LOG_ERROR(const char *fmt, ...) {
    va_list args;
    time_t now;
    char time_str[20];
    
    pthread_mutex_lock(&log_mutex);
    time(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] ERROR: ", time_str);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    pthread_mutex_unlock(&log_mutex);
}

void LOG_WARN(const char *fmt, ...) {
    va_list args;
    time_t now;
    char time_str[20];
    
    pthread_mutex_lock(&log_mutex);
    time(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] WARN: ", time_str);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    pthread_mutex_unlock(&log_mutex);
}

void LOG_INFO(const char *fmt, ...) {
    va_list args;
    time_t now;
    char time_str[20];
    
    pthread_mutex_lock(&log_mutex);
    time(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] INFO: ", time_str);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    pthread_mutex_unlock(&log_mutex);
}
