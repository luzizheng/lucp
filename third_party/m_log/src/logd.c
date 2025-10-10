#include "log_server.h"
#include <stdio.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

// 全局服务端实例
static DltServer *g_server = NULL;

typedef struct{
    const char *config_file;
    int port;
} MainEntryArgs;

void print_usage(const char *prog_name) {
    printf("Usage: %s [-c cfgfile] [-p port]\n", prog_name);
    printf("  -c cfgfile : Specify configuration file (default: %s)\n",DLT_CFG_FILE_PATH);
    printf("  -p port    : Specify listening port (default: %d)\n",DLT_DEFAULT_SERVER_PORT);
    printf("  -h         : Show this help message\n");
}

void parse_arguments(int argc, char *argv[], MainEntryArgs *args) {
    args->config_file = DLT_CFG_FILE_PATH; // 默认配置文件
    args->port = DLT_DEFAULT_SERVER_PORT;                    // 默认端口

    // 解析命令行参数，需要检测参数合法性，和未定义参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            args->config_file = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            args->port = atoi(argv[++i]);
            if (args->port <= 0 || args->port > 65535) {
                fprintf(stderr, "Invalid port number: %s\n", argv[i]);
                print_usage(argv[0]);
                exit(1);
            }
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

// 信号处理函数
static void signal_handler(int sig) {
    switch(sig) {
        case SIGHUP:
            // 重新加载配置
            syslog(LOG_INFO, "Received SIGHUP, reloading configuration");
            break;
        case SIGTERM:
            // 优雅退出
            syslog(LOG_INFO, "Received SIGTERM, Stopping server...");
            if (g_server) {
                dlt_server_stop(g_server);
            }
            break;
        case SIGINT:
            // 优雅退出
            syslog(LOG_INFO, "Received SIGTERM, Stopping server...");
            if (g_server) {
                dlt_server_stop(g_server);
            }
            break;
        default:
            break;
    }
}



static void daemonize(const char *name) {
    pid_t pid;
    
    // 创建子进程
    if ((pid = fork()) < 0) {
        perror("fork");
        exit(1);
    } else if (pid != 0) {
        exit(0); // 父进程退出
    }
    
    // 成为会话组长
    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }
    
    // 处理信号
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 第二次fork
    if ((pid = fork()) < 0) {
        perror("fork");
        exit(1);
    } else if (pid != 0) {
        exit(0);
    }
    
    // 设置文件掩码
    umask(0);
    
    // 切换工作目录
    chdir("/");
    
    // 关闭所有打开的文件描述符
    for (int i = 0; i < sysconf(_SC_OPEN_MAX); i++) {
        close(i);
    }
    
    // 重定向标准流
    open("/dev/null", O_RDONLY); // stdin
    open("/dev/null", O_RDWR);   // stdout  
    open("/dev/null", O_RDWR);   // stderr
    
    // 使用syslog记录日志
    openlog(name, LOG_PID, LOG_DAEMON);
}


int main(int argc, char *argv[]) {

     // 将进程daemon化
    daemonize("logd");
    syslog(LOG_INFO, "logd daemon started");

    MainEntryArgs args;
    parse_arguments(argc, argv, &args);
    dlt_load_cfg(args.config_file, 1);
    g_dlt_general_cfg.server_port = args.port;

    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化并启动服务端
    g_server = dlt_server_init(&g_dlt_general_cfg);
    if (!g_server) {
        syslog(LOG_ERR, "Failed to initialize DLT server");
        return 1;
    }
    
    syslog(LOG_DEBUG, "Starting DLT server on port %d...", g_server->port);
    dlt_server_start(g_server);
    
    // 清理
    dlt_server_destroy(g_server);
    g_server = NULL;
    
    syslog(LOG_DEBUG, "Server stopped.");
    return 0;
}
