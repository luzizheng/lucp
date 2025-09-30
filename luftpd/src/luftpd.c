#include "luftpd_cfg.h"
#include "luftpd_utils.h"
#include <stdio.h>
#include <pthread.h>


// 全局变量
LuftpdConfig_t g_luftpd_config;
LuftpdClient_t *g_clients = NULL;
int g_client_count = 0;
pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

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
