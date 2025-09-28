#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "[WARN ] " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  fprintf(stdout, "[INFO ] " fmt "\n", ##__VA_ARGS__)

typedef struct {
    char ip[16];
    uint16_t port;
    char root_dir[PATH_MAX];
    int max_connections;
    int data_port_min;
    int data_port_max;
} FtpServerConfig;

typedef struct {
    int control_sock;
    int data_sock;
    int pasv_sock;
    struct sockaddr_in client_addr;
    pthread_t thread_id;
    int is_active;
    char cwd[PATH_MAX];
} Client_t;

FtpServerConfig g_config = {
    .ip = "0.0.0.0",
    .port = 2121,
    .root_dir = "/tmp",
    .max_connections = 10,
    .data_port_min = 30000,
    .data_port_max = 30100
};

void send_response(int sock, int code, const char *msg) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%d %s\r\n", code, msg);
    send(sock, buf, strlen(buf), 0);
}

void* client_thread(void* arg);

int start_server() {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        LOG_ERROR("socket() failed: %s", strerror(errno));
        return -1;
    }
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_config.port);
    addr.sin_addr.s_addr = inet_addr(g_config.ip);
    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind() failed: %s", strerror(errno));
        close(listenfd);
        return -1;
    }
    if (listen(listenfd, g_config.max_connections) < 0) {
        LOG_ERROR("listen() failed: %s", strerror(errno));
        close(listenfd);
        return -1;
    }
    LOG_INFO("FTP server listening on %s:%d", g_config.ip, g_config.port);
    while (1) {
        Client_t *client = calloc(1, sizeof(Client_t));
        socklen_t len = sizeof(client->client_addr);
        client->control_sock = accept(listenfd, (struct sockaddr*)&client->client_addr, &len);
        if (client->control_sock < 0) {
            LOG_WARN("accept() failed: %s", strerror(errno));
            free(client);
            continue;
        }
    strncpy(client->cwd, g_config.root_dir, PATH_MAX-1);
    client->cwd[PATH_MAX-1] = '\0';
        client->is_active = 1;
        pthread_create(&client->thread_id, NULL, client_thread, client);
        pthread_detach(client->thread_id);
    }
    close(listenfd);
    return 0;
}

void handle_LIST(Client_t *client, char *arg);
void handle_SIZE(Client_t *client, char *arg);
void handle_RETR(Client_t *client, char *arg);
void handle_CWD(Client_t *client, char *arg);
void handle_PWD(Client_t *client);
void handle_TYPE(Client_t *client, char *arg);
int setup_pasv(Client_t *client);

void* client_thread(void* arg) {
    Client_t *client = (Client_t*)arg;
    send_response(client->control_sock, 220, "Welcome to Log Upload FTP Server");
    char buf[512], cmd[16], cmd_arg[256];
    int logged_in = 0;
    while (client->is_active) {
        int n = recv(client->control_sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;
        buf[n] = 0;
        LOG_INFO("Recv: %s", buf);
        sscanf(buf, "%15s %255[^\r\n]", cmd, cmd_arg);
        if (!logged_in) {
            if (strcasecmp(cmd, "USER") == 0) {
                send_response(client->control_sock, 331, "Anonymous login ok, send password");
            } else if (strcasecmp(cmd, "PASS") == 0) {
                send_response(client->control_sock, 230, "Login successful");
                logged_in = 1;
            } else {
                send_response(client->control_sock, 530, "Please login");
            }
            continue;
        }
        if (strcasecmp(cmd, "PASV") == 0) {
            if (setup_pasv(client) == 0)
                send_response(client->control_sock, 227, "Entering Passive Mode");
            else
                send_response(client->control_sock, 425, "Can't open passive connection");
        } else if (strcasecmp(cmd, "LIST") == 0) {
            handle_LIST(client, cmd_arg);
        } else if (strcasecmp(cmd, "SIZE") == 0) {
            handle_SIZE(client, cmd_arg);
        } else if (strcasecmp(cmd, "RETR") == 0) {
            handle_RETR(client, cmd_arg);
        } else if (strcasecmp(cmd, "CWD") == 0) {
            handle_CWD(client, cmd_arg);
        } else if (strcasecmp(cmd, "PWD") == 0) {
            handle_PWD(client);
        } else if (strcasecmp(cmd, "TYPE") == 0) {
            handle_TYPE(client, cmd_arg);
        } else if (strcasecmp(cmd, "QUIT") == 0) {
            send_response(client->control_sock, 221, "Goodbye");
            break;
        } else {
            send_response(client->control_sock, 502, "Command not implemented");
        }
    }
    close(client->control_sock);
    if (client->pasv_sock > 0) close(client->pasv_sock);
    if (client->data_sock > 0) close(client->data_sock);
    free(client);
    LOG_INFO("Client thread exit");
    return NULL;
}

// 下面是各指令的简要实现（可扩展健壮性和日志）
void handle_LIST(Client_t *client, char *arg) {
    (void)arg;
    send_response(client->control_sock, 150, "Opening data connection for LIST");
    if (client->data_sock <= 0) {
        LOG_ERROR("LIST: data_sock not open");
        send_response(client->control_sock, 425, "No data connection");
        return;
    }
    char list_buf[4096] = {0};
    size_t list_len = 0;
    DIR *dir = opendir(client->cwd);
    if (!dir) {
        LOG_ERROR("LIST: opendir failed: %s", strerror(errno));
        send_response(client->control_sock, 550, "Failed to open directory");
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char path[PATH_MAX];
        int path_len = snprintf(path, sizeof(path), "%s/%s", client->cwd, entry->d_name);
        if (path_len < 0 || path_len >= (int)sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) == 0) {
            char line[512];
            int line_len = snprintf(line, sizeof(line), "%c%s\r\n", S_ISDIR(st.st_mode) ? 'd' : '-', entry->d_name);
            if (line_len > 0 && list_len + line_len < sizeof(list_buf)) {
                memcpy(list_buf + list_len, line, line_len);
                list_len += line_len;
            }
        }
    }
    closedir(dir);
    send(client->data_sock, list_buf, list_len, 0);
    close(client->data_sock); client->data_sock = -1;
    send_response(client->control_sock, 226, "Transfer complete");
}
void handle_SIZE(Client_t *client, char *arg) {
    if (!arg || strlen(arg) == 0) {
        send_response(client->control_sock, 501, "SIZE needs filename");
        return;
    }
    char path[PATH_MAX];
    int path_len = snprintf(path, sizeof(path), "%s/%s", client->cwd, arg);
    if (path_len < 0 || path_len >= (int)sizeof(path)) {
        send_response(client->control_sock, 550, "File path too long");
        return;
    }
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        char sizebuf[64];
        snprintf(sizebuf, sizeof(sizebuf), "%ld", st.st_size);
        send_response(client->control_sock, 213, sizebuf);
    } else {
        send_response(client->control_sock, 550, "File not found");
    }
}
void handle_RETR(Client_t *client, char *arg) {
    if (!arg || strlen(arg) == 0) {
        send_response(client->control_sock, 501, "RETR needs filename");
        return;
    }
    if (client->data_sock <= 0) {
        LOG_ERROR("RETR: data_sock not open");
        send_response(client->control_sock, 425, "No data connection");
        return;
    }
    char path[PATH_MAX];
    int path_len = snprintf(path, sizeof(path), "%s/%s", client->cwd, arg);
    if (path_len < 0 || path_len >= (int)sizeof(path)) {
        send_response(client->control_sock, 550, "File path too long");
        return;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        send_response(client->control_sock, 550, "File not found");
        return;
    }
    send_response(client->control_sock, 150, "Opening data connection for RETR");
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (send(client->data_sock, buf, n, 0) < 0) break;
    }
    fclose(fp);
    close(client->data_sock); client->data_sock = -1;
    send_response(client->control_sock, 226, "Transfer complete");
}
void handle_CWD(Client_t *client, char *arg) {
    if (!arg || strlen(arg) == 0) {
        send_response(client->control_sock, 501, "CWD needs directory");
        return;
    }
    char new_path[PATH_MAX];
    int new_path_len;
    if (arg[0] == '/') {
        new_path_len = snprintf(new_path, sizeof(new_path), "%s%s", g_config.root_dir, arg);
    } else {
        new_path_len = snprintf(new_path, sizeof(new_path), "%s/%s", client->cwd, arg);
    }
    if (new_path_len < 0 || new_path_len >= (int)sizeof(new_path)) {
        send_response(client->control_sock, 550, "Directory path too long");
        return;
    }
    struct stat st;
    if (stat(new_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        strncpy(client->cwd, new_path, PATH_MAX-1);
        client->cwd[PATH_MAX-1] = '\0';
        send_response(client->control_sock, 250, "Directory changed");
    } else {
        send_response(client->control_sock, 550, "Directory not found");
    }
}
void handle_PWD(Client_t *client) {
    char msg[PATH_MAX+32];
    int msg_len = snprintf(msg, sizeof(msg), "\"%s\"", client->cwd);
    if (msg_len < 0 || msg_len >= (int)sizeof(msg)) {
        send_response(client->control_sock, 257, "PWD error");
    } else {
        send_response(client->control_sock, 257, msg);
    }
}
void handle_TYPE(Client_t *client, char *arg) {
    if (!arg || strlen(arg) == 0) {
        send_response(client->control_sock, 501, "TYPE needs argument");
        return;
    }
    if (strcasecmp(arg, "I") == 0 || strcasecmp(arg, "A") == 0) {
        send_response(client->control_sock, 200, "Type set");
    } else {
        send_response(client->control_sock, 504, "Type not supported");
    }
}
int setup_pasv(Client_t *client) {
    int pasv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (pasv_fd < 0) {
        LOG_ERROR("PASV: socket() failed: %s", strerror(errno));
        return -1;
    }
    int port = g_config.data_port_min + rand() % (g_config.data_port_max - g_config.data_port_min + 1);
    struct sockaddr_in pasv_addr = {0};
    pasv_addr.sin_family = AF_INET;
    pasv_addr.sin_addr.s_addr = inet_addr(g_config.ip);
    pasv_addr.sin_port = htons(port);
    int opt = 1;
    setsockopt(pasv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(pasv_fd, (struct sockaddr*)&pasv_addr, sizeof(pasv_addr)) < 0) {
        LOG_ERROR("PASV: bind() failed: %s", strerror(errno));
        close(pasv_fd);
        return -1;
    }
    if (listen(pasv_fd, 1) < 0) {
        LOG_ERROR("PASV: listen() failed: %s", strerror(errno));
        close(pasv_fd);
        return -1;
    }
    client->pasv_sock = pasv_fd;
    // 获取本地监听 IP
    struct sockaddr_in local_addr;
    socklen_t addrlen = sizeof(local_addr);
    getsockname(client->control_sock, (struct sockaddr*)&local_addr, &addrlen);
    unsigned char *ip = (unsigned char*)&local_addr.sin_addr.s_addr;
    char resp[128];
    int resp_len = snprintf(resp, sizeof(resp), "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
        ip[0], ip[1], ip[2], ip[3], port/256, port%256);
    if (resp_len < 0 || resp_len >= (int)sizeof(resp)) {
        send_response(client->control_sock, 227, "PASV response error");
    } else {
        send_response(client->control_sock, 227, resp);
    }
    // 等待客户端连接
    struct sockaddr_in cli_addr; socklen_t cli_len = sizeof(cli_addr);
    client->data_sock = accept(pasv_fd, (struct sockaddr*)&cli_addr, &cli_len);
    if (client->data_sock < 0) {
        LOG_ERROR("PASV: accept() failed: %s", strerror(errno));
        close(pasv_fd); client->pasv_sock = -1;
        return -1;
    }
    close(pasv_fd); client->pasv_sock = -1;
    return 0;
}

int main() {
    start_server();
    return 0;
}
