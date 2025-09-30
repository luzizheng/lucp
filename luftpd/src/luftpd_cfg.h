#ifndef LUFTPD_CONFIG_H
#define LUFTPD_CONFIG_H
#include <stdint.h>
#include <netinet/in.h>

#define LUFTPD_DEFAULT_CONFIG_FILE "/etc/luftpd.conf"

#define LUFTPD_DEFAULT_IP           "0.0.0.0"
#define LUFTPD_DEFAULT_PORT         2121
#define LUFTPD_DEFAULT_ROOT_DIR     "/tmp/luftp_root"
#define LUFTPD_DEFAULT_MAX_CONNECTIONS 10
#define LUFTPD_DEFAULT_DATA_PORT_MIN 30000
#define LUFTPD_DEFAULT_DATA_PORT_MAX 30100

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif


typedef struct {
    char ip[16];
    uint16_t port;
    char root_dir[256];
    int max_connections;
    int data_port_min;
    int data_port_max;
} LuftpdConfig_t;

typedef enum{
    LUFTPD_TRANSFER_TYPE_ASCII = 0,
    LUFTPD_TRANSFER_TYPE_BINARY = 1
} LuftpdTransferType_t;

typedef struct 
{
    int control_sock;
    int data_sock;
    int pasv_sock;
    struct sockaddr_in client_addr;
    int thread_id;
    int is_active;
    char cwd[PATH_MAX];   /* 相对 root 的路径 */
    LuftpdTransferType_t transfer_type;
    LuftpdConfig_t *config;
} LuftpdClient_t;

#define LUFTPD_CONFIG_INITIALIZER { \
    .ip = LUFTPD_DEFAULT_IP, \
    .port = LUFTPD_DEFAULT_PORT, \
    .root_dir = LUFTPD_DEFAULT_ROOT_DIR, \
    .max_connections = LUFTPD_DEFAULT_MAX_CONNECTIONS, \
    .data_port_min = LUFTPD_DEFAULT_DATA_PORT_MIN, \
    .data_port_max = LUFTPD_DEFAULT_DATA_PORT_MAX \
}

int luftpd_cfg_load_with_file(LuftpdConfig_t* cfg, const char* config_file);




#endif // LUFTPD_CONFIG_H