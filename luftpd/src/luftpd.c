#include "luftpd_cfg.h"
#include "luftpd_utils.h"
#include <stdio.h>
#include <pthread.h>
#include <logMgr.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>



// 全局变量
LuftpdConfig_t g_luftpd_config;
LuftpdClient_t *g_clients = NULL;
int g_client_count = 0;
pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifndef LUCPD_APPID
#define LUCPD_APPID "luftpd"
#endif

// 函数声明
int luftpd_start_server();
void *luftpd_handle_client(void *arg);
int luftpd_send_response(int sock, const char *fmt, ...);
int luftpd_parse_command(const char *cmd, char *out_cmd, char *out_arg);
int luftpd_handle_cwd(LuftpdClient_t *client, const char *arg);
int luftpd_handle_list(LuftpdClient_t *client, const char *arg);
int luftpd_handle_retr(LuftpdClient_t *client, const char *arg);
int luftpd_handle_syst(LuftpdClient_t *client, const char *arg);
int luftpd_handle_type(LuftpdClient_t *client, const char *arg);
int luftpd_handle_feat(LuftpdClient_t *client, const char *arg);
int luftpd_handle_size(LuftpdClient_t *client, const char *arg);
int luftpd_enter_pasv_mode(LuftpdClient_t *client);
int lufptd_create_data_connection(LuftpdClient_t *client);
int lufptd_close_client(LuftpdClient_t *client);

// 处理CWD命令
int luftpd_handle_cwd(LuftpdClient_t *client, const char *arg) {
    char resolved_path[PATH_MAX];
    
    if (luftpd_resolve_path(client->config->root_dir, 
                           client->current_dir, 
                           arg, 
                           resolved_path, 
                           sizeof(resolved_path)) != 0) {
        return luftpd_send_response(client->control_sock, "550 Failed to resolve path");
    }
    
    // 检查目录是否存在
    struct stat st;
    if (stat(resolved_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return luftpd_send_response(client->control_sock, "550 Directory not found");
    }
    
    // 更新当前目录
    // 计算相对于root_dir的路径
    const char *relative_path = resolved_path + strlen(client->config->root_dir);
    if (*relative_path == '\0') {
        strcpy(client->current_dir, "/");
    } else {
        strncpy(client->current_dir, relative_path, PATH_MAX - 1);
        client->current_dir[PATH_MAX - 1] = '\0';
    }
    
    return luftpd_send_response(client->control_sock, "250 Directory successfully changed");
}



// 处理LIST命令
int luftpd_handle_list(LuftpdClient_t *client, const char *arg) {
    char resolved_path[PATH_MAX];
    
    if (luftpd_resolve_path(client->config->root_dir, 
                           client->current_dir, 
                           arg, 
                           resolved_path, 
                           sizeof(resolved_path)) != 0) {
        return luftpd_send_response(client->control_sock, "550 Failed to resolve path");
    }
    
    // 检查目录是否存在
    struct stat st;
    if (stat(resolved_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return luftpd_send_response(client->control_sock, "550 Directory not found");
    }
    
    // 建立数据连接
    if (lufptd_create_data_connection(client) != 0) {
        return luftpd_send_response(client->control_sock, "425 Can't open data connection");
    }
    
    luftpd_send_response(client->control_sock, "150 Opening ASCII mode data connection for file list");
    
    DIR *dir = opendir(resolved_path);
    if (!dir) {
        lufptd_close_client(client);
        return luftpd_send_response(client->control_sock, "550 Failed to open directory");
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", resolved_path, entry->d_name);
        
        if (stat(full_path, &st) != 0) {
            continue;
        }
        
        char listing[512];
        char time_str[64];
        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(time_str, sizeof(time_str), "%b %d %H:%M", tm_info);
        
        if (S_ISDIR(st.st_mode)) {
            snprintf(listing, sizeof(listing), "drwxr-xr-x 1 ftp ftp %8ld %s %s\r\n", 
                    st.st_size, time_str, entry->d_name);
        } else {
            snprintf(listing, sizeof(listing), "-rw-r--r-- 1 ftp ftp %8ld %s %s\r\n", 
                    st.st_size, time_str, entry->d_name);
        }
        
        send(client->data_sock, listing, strlen(listing), 0);
    }
    
    closedir(dir);
    close(client->data_sock);
    client->data_sock = -1;
    
    return luftpd_send_response(client->control_sock, "226 Transfer complete");
}

// 处理RETR命令
int luftpd_handle_retr(LuftpdClient_t *client, const char *arg) {
    char resolved_path[PATH_MAX];
    
    if (luftpd_resolve_path(client->config->root_dir, 
                           client->current_dir, 
                           arg, 
                           resolved_path, 
                           sizeof(resolved_path)) != 0) {
        return luftpd_send_response(client->control_sock, "550 Failed to resolve path");
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat(resolved_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return luftpd_send_response(client->control_sock, "550 File not found");
    }
    
    // 建立数据连接
    if (lufptd_create_data_connection(client) != 0) {
        return luftpd_send_response(client->control_sock, "425 Can't open data connection");
    }
    
    luftpd_send_response(client->control_sock, "150 Opening data connection for file transfer");
    
    FILE *file = fopen(resolved_path, "rb");
    if (!file) {
        lufptd_close_client(client);
        return luftpd_send_response(client->control_sock, "550 Failed to open file");
    }
    
    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(client->data_sock, buffer, bytes_read, 0) < 0) {
            break;
        }
    }
    
    fclose(file);
    close(client->data_sock);
    client->data_sock = -1;
    
    return luftpd_send_response(client->control_sock, "226 Transfer complete");
}

// 处理SYST命令
int luftpd_handle_syst(LuftpdClient_t *client, const char *arg) {
    return luftpd_send_response(client->control_sock, "215 UNIX Type: L8");
}

// 处理TYPE命令
int luftpd_handle_type(LuftpdClient_t *client, const char *arg) {
    if (strcasecmp(arg, "A") == 0 || strcasecmp(arg, "ASCII") == 0) {
        client->transfer_type = LUFTPD_TRANSFER_TYPE_ASCII;
        return luftpd_send_response(client->control_sock, "200 Switching to ASCII mode");
    } else if (strcasecmp(arg, "I") == 0 || strcasecmp(arg, "BINARY") == 0) {
        client->transfer_type = LUFTPD_TRANSFER_TYPE_BINARY;
        return luftpd_send_response(client->control_sock, "200 Switching to Binary mode");
    } else {
        return luftpd_send_response(client->control_sock, "500 Unrecognized TYPE command");
    }
}

// 处理FEAT命令
int luftpd_handle_feat(LuftpdClient_t *client, const char *arg) {
    luftpd_send_response(client->control_sock, "211-Features:");
    luftpd_send_response(client->control_sock, " PASV");
    luftpd_send_response(client->control_sock, " SIZE");
    luftpd_send_response(client->control_sock, "211 End");
    return 0;
}

// 处理SIZE命令
int luftpd_handle_size(LuftpdClient_t *client, const char *arg) {
    char resolved_path[PATH_MAX];
    
    if (luftpd_resolve_path(client->config->root_dir, 
                           client->current_dir, 
                           arg, 
                           resolved_path, 
                           sizeof(resolved_path)) != 0) {
        return luftpd_send_response(client->control_sock, "550 Failed to resolve path");
    }
    
    struct stat st;
    if (stat(resolved_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return luftpd_send_response(client->control_sock, "550 File not found");
    }
    
    return luftpd_send_response(client->control_sock, "213 %ld", st.st_size);
}

// 进入PASV模式
int luftpd_enter_pasv_mode(LuftpdClient_t *client) {
    // 关闭之前的PASV socket
    if (client->pasv_sock != -1) {
        close(client->pasv_sock);
        client->pasv_sock = -1;
    }
    
    // 创建PASV socket
    client->pasv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->pasv_sock < 0) {
        return -1;
    }
    
    // 设置SO_REUSEADDR
    int reuse = 1;
    setsockopt(client->pasv_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // 绑定到随机端口
    struct sockaddr_in pasv_addr;
    memset(&pasv_addr, 0, sizeof(pasv_addr));
    pasv_addr.sin_family = AF_INET;
    pasv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    // 在配置的端口范围内尝试绑定
    int port;
    for (port = client->config->data_port_min; port <= client->config->data_port_max; port++) {
        pasv_addr.sin_port = htons(port);
        if (bind(client->pasv_sock, (struct sockaddr*)&pasv_addr, sizeof(pasv_addr)) == 0) {
            break;
        }
    }
    
    if (port > client->config->data_port_max) {
        close(client->pasv_sock);
        client->pasv_sock = -1;
        return -1;
    }
    
    if (listen(client->pasv_sock, 1) < 0) {
        close(client->pasv_sock);
        client->pasv_sock = -1;
        return -1;
    }
    
    // 获取服务器IP（简化处理，使用配置的IP）
    uint32_t ip = inet_addr(client->config->ip);
    if (ip == INADDR_NONE) {
        ip = htonl(INADDR_ANY);
    }
    
    unsigned char *ip_bytes = (unsigned char*)&ip;
    
    // 发送PASV响应
    luftpd_send_response(client->control_sock, 
                        "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
                        ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
                        port >> 8, port & 0xFF);
    
    return 0;
}

// 创建数据连接
int lufptd_create_data_connection(LuftpdClient_t *client) {
    if (client->pasv_sock == -1) {
        return -1;
    }
    
    // 设置非阻塞并接受连接
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client->pasv_sock, &readfds);
    
    int result = select(client->pasv_sock + 1, &readfds, NULL, NULL, &tv);
    if (result <= 0) {
        return -1;
    }
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    client->data_sock = accept(client->pasv_sock, (struct sockaddr*)&client_addr, &client_len);
    
    // 关闭PASV socket
    close(client->pasv_sock);
    client->pasv_sock = -1;
    
    return client->data_sock >= 0 ? 0 : -1;
}

// 关闭客户端连接
int lufptd_close_client(LuftpdClient_t *client) {
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
    return 0;
}

// 处理客户端连接的线程函数
void *luftpd_handle_client(void *arg) {
    LuftpdClient_t *client = (LuftpdClient_t *)arg;
    char buffer[1024];
    
    // 发送欢迎消息
    luftpd_send_response(client->control_sock, "220 Welcome to Luftpd FTP Server");
    
    while (client->is_active) {
        int bytes_received = recv(client->control_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        char command[16];
        char argument[256];
        
        if (luftpd_parse_command(buffer, command, argument) != 0) {
            luftpd_send_response(client->control_sock, "500 Syntax error");
            continue;
        }
        
        // 处理命令
        if (strcmp(command, "USER") == 0) {
            luftpd_send_response(client->control_sock, "331 User name okay, need password");
        } else if (strcmp(command, "PASS") == 0) {
            luftpd_send_response(client->control_sock, "230 User logged in, proceed");
        } else if (strcmp(command, "SYST") == 0) {
            luftpd_handle_syst(client, argument);
        } else if (strcmp(command, "FEAT") == 0) {
            luftpd_handle_feat(client, argument);
        } else if (strcmp(command, "PWD") == 0 || strcmp(command, "XPWD") == 0) {
            luftpd_send_response(client->control_sock, "257 \"%s\"", client->current_dir);
        } else if (strcmp(command, "CWD") == 0) {
            luftpd_handle_cwd(client, argument);
        } else if (strcmp(command, "TYPE") == 0) {
            luftpd_handle_type(client, argument);
        } else if (strcmp(command, "PASV") == 0) {
            luftpd_enter_pasv_mode(client);
        } else if (strcmp(command, "LIST") == 0 || strcmp(command, "NLST") == 0) {
            luftpd_handle_list(client, argument);
        } else if (strcmp(command, "RETR") == 0) {
            luftpd_handle_retr(client, argument);
        } else if (strcmp(command, "SIZE") == 0) {
            luftpd_handle_size(client, argument);
        } else if (strcmp(command, "QUIT") == 0) {
            luftpd_send_response(client->control_sock, "221 Goodbye");
            break;
        } else {
            luftpd_send_response(client->control_sock, "502 Command not implemented");
        }
    }
    
    // 清理客户端资源
    lufptd_close_client(client);
    
    // 从全局客户端列表中移除
    pthread_mutex_lock(&g_clients_mutex);
    for (int i = 0; i < g_client_count; i++) {
        if (g_clients[i].thread_id == client->thread_id) {
            // 移动数组元素
            for (int j = i; j < g_client_count - 1; j++) {
                g_clients[j] = g_clients[j + 1];
            }
            g_client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_clients_mutex);
    
    return NULL;
}

// 启动FTP服务器
int luftpd_start_server() {
    // 加载配置
    if (luftpd_cfg_load_with_file(&g_luftpd_config, LUFTPD_DEFAULT_CONFIG_FILE) != 0) {
        printf("Using default configuration\n");
    }
    
    // 创建根目录
    mkdir(g_luftpd_config.root_dir, 0755);
    
    // 创建服务器socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return -1;
    }
    
    // 设置SO_REUSEADDR
    int reuse = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(g_luftpd_config.ip);
    server_addr.sin_port = htons(g_luftpd_config.port);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return -1;
    }
    
    // 监听
    if (listen(server_sock, g_luftpd_config.max_connections) < 0) {
        perror("listen");
        close(server_sock);
        return -1;
    }
    
    printf("FTP server started on %s:%d\n", g_luftpd_config.ip, g_luftpd_config.port);
    printf("Root directory: %s\n", g_luftpd_config.root_dir);
    
    // 初始化客户端数组
    g_clients = malloc(g_luftpd_config.max_connections * sizeof(LuftpdClient_t));
    if (!g_clients) {
        close(server_sock);
        return -1;
    }
    
    // 主循环
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }
        
        pthread_mutex_lock(&g_clients_mutex);
        
        if (g_client_count >= g_luftpd_config.max_connections) {
            luftpd_send_response(client_sock, "421 Too many connections, try again later");
            close(client_sock);
            pthread_mutex_unlock(&g_clients_mutex);
            continue;
        }
        
        // 初始化客户端结构
        LuftpdClient_t *client = &g_clients[g_client_count];
        memset(client, 0, sizeof(LuftpdClient_t));
        client->control_sock = client_sock;
        client->data_sock = -1;
        client->pasv_sock = -1;
        client->client_addr = client_addr;
        client->is_active = 1;
        client->transfer_type = LUFTPD_TRANSFER_TYPE_BINARY;
        strcpy(client->current_dir, "/");
        client->config = &g_luftpd_config;
        
        // 创建线程处理客户端
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, luftpd_handle_client, client) == 0) {
            client->thread_id = (int)thread_id;
            g_client_count++;
            printf("Client connected: %s:%d\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        } else {
            close(client_sock);
        }
        
        pthread_mutex_unlock(&g_clients_mutex);
    }
    
    close(server_sock);
    free(g_clients);
    return 0;
}





int main(int argc, char *argv[]) {

    dlt_init_client(LUCPD_APPID);

    const char *config_file = LUFTPD_DEFAULT_CONFIG_FILE;
    if (argc > 1) {
        config_file = argv[1];
    }

    // 加载配置文件
    if (luftpd_cfg_load_with_file(&g_luftpd_config, config_file) != 0) {
        fprintf(stderr, "Failed to load config file: %s\n", config_file);

        goto free_log;
        return 1;
    }

    printf("Starting luftpd server on %s:%d, root dir: %s\n",
           g_luftpd_config.ip, g_luftpd_config.port, g_luftpd_config.root_dir);

    // 启动服务器
    if (luftpd_start_server() != 0) {
        fprintf(stderr, "Failed to start luftpd server\n");
        goto free_log;
        return 1;
    }


free_log:
    dlt_free_client(LUCPD_APPID);

    return 0;
}