#include "log_server.h"
#include <stdio.h>
#include <signal.h>


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
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nReceived termination signal. Stopping server...\n");
        if (g_server) {
            dlt_server_stop(g_server);
        }
    }
}

int main(int argc, char *argv[]) {
    
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
        fprintf(stderr, "Failed to initialize DLT server\n");
        return 1;
    }
    
    printf("Starting DLT server on port %d...\n", g_server->port);
    dlt_server_start(g_server);
    
    // 清理
    dlt_server_destroy(g_server);
    g_server = NULL;
    
    printf("Server stopped\n");
    return 0;
}
