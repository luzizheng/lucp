#include "lucpd_cfg.h"
#include "lucpd_utils.h"
#include <arpa/inet.h>
#include <errno.h>
#include <lucp.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define RATE_LIMIT_SECONDS 3
// 会话状态
typedef enum
{
    LUCP_SESSION_INIT,
    LUCP_SESSION_WAITING_UPLOAD_REQUEST,
    LUCP_SESSION_WAITING_FTP_LOGIN_RESULT,
    LUCP_SESSION_WAITING_FTP_DOWNLOAD_RESULT,
    LUCP_SESSION_WAITING_CLOUD_UPLOAD_RESULT,
    LUCP_SESSION_COMPLETED,
    LUCP_SESSION_ERROR
} LucpSessionState;

// 会话结构
typedef struct
{
    int fd;
    LucpSessionState state;
    uint32_t seq_num;
    uint64_t last_active_ms;
    LucpdConfig_t* config;
    atomic_bool* server_running;
} LucpSession_t;

static LucpdConfig_t g_lucpdcfg;

// Signal handler for graceful shutdown
atomic_bool server_running = true;
int listen_fd              = -1;
void handle_sig(int sig)
{
    (void) sig;
    server_running = false;
    if (listen_fd >= 0)
        close(listen_fd);
    printf("[Server] Shutting down...\n");
}

static void* session_thread(void* arg)
{
    LucpSession_t* sess = (LucpSession_t*) arg;
    lucp_net_ctx_t netctx;
    lucp_net_ctx_init(&netctx, sess->fd);
    lucp_frame_t frame, reply;
    char payload[256];
    int running = 1;
    // uint64_t session_start = get_now_ms();

    log_debug("[Session %d] Started.", sess->fd);
    sess->state          = LUCP_SESSION_INIT;
    sess->last_active_ms = get_now_ms();

    while (running && *sess->server_running)
    {
        // 超时管理
        uint64_t now = get_now_ms();
        if ((int) (now - sess->last_active_ms) > sess->config->protocol.session_timeout_ms)
        {
            log_debug("[Session %d] Session timeout.", sess->fd);
            sess->state = LUCP_SESSION_ERROR;
        }

        switch (sess->state)
        {
        case LUCP_SESSION_INIT: {
            // 等待 LUCP_MTYP_UPLOAD_REQUEST
            int ret = lucp_net_recv(&netctx, &frame);
            if (ret == 0 && frame.msgType == LUCP_MTYP_UPLOAD_REQUEST)
            {
                // 检查版本
                if (sess->config->protocol.validate_version)
                {
                    if (frame.version_major != LUCP_VER_MAJOR)
                    {
                        lucp_frame_make(&reply,
                                        frame.seq_num,
                                        LUCP_MTYP_ACK_START,
                                        LUCP_STAT_FAILED,
                                        "Bad version",
                                        11);
                        lucp_net_send(&netctx, &reply);
                        sess->state = LUCP_SESSION_ERROR;
                        break;
                    }
                }
                sess->seq_num        = frame.seq_num;
                sess->state          = LUCP_SESSION_WAITING_UPLOAD_REQUEST;
                sess->last_active_ms = get_now_ms();

                // Send LUCP_MTYP_ACK_START ACK
                lucp_frame_make(
                    &reply, sess->seq_num, LUCP_MTYP_ACK_START, LUCP_STAT_SUCCESS, NULL, 0);
                lucp_net_send(&netctx, &reply);
                log_debug(
                    "[Session %d] State: INIT -> WAITING_UPLOAD_REQUEST, sent LUCP_MTYP_ACK_START.",
                    sess->fd);
            }
            else if (ret < 0)
            {
                sess->state = LUCP_SESSION_ERROR;
            }
            break;
        }
        case LUCP_SESSION_WAITING_UPLOAD_REQUEST: {
            // Simulate log prep (archive + FTP upload), send LUCP_MTYP_NOTIFY_DONE
            int prep_status = LUCP_STAT_SUCCESS;
            memset(payload, 0, sizeof(payload));
            int r = rand() % 10;
            if (r < 8)
            {
                prep_status = 1;
                snprintf(payload, sizeof(payload), "demo_logfile_20250925.log");
            }
            else if (r == 8)
            {
                prep_status = LUCP_STAT_ARCHIVE_FAILED;
                snprintf(payload, sizeof(payload), "Archive failed: disk full");
            }
            else
            {
                prep_status = LUCP_STAT_FTP_UPLOAD_FAILED;
                snprintf(payload, sizeof(payload), "FTP upload failed: connection timeout");
            }
            usleep(700 * 1000); // 700ms
            lucp_frame_make(&reply,
                            sess->seq_num,
                            LUCP_MTYP_NOTIFY_DONE,
                            prep_status,
                            payload,
                            strlen(payload));
            lucp_net_send(&netctx, &reply);
            log_debug("[Session %d] Sent LUCP_MTYP_NOTIFY_DONE(0x%x) (status=0x%x).",
                      sess->fd,
                      LUCP_MTYP_NOTIFY_DONE,
                      prep_status);
            if (prep_status != LUCP_STAT_SUCCESS)
            {
                sess->state = LUCP_SESSION_ERROR;
            }
            else
            {
                sess->state = LUCP_SESSION_WAITING_FTP_LOGIN_RESULT;
            }
            sess->last_active_ms = get_now_ms();
            break;
        }
        case LUCP_SESSION_WAITING_FTP_LOGIN_RESULT: {
            int ret = lucp_net_recv(&netctx, &frame);
            if (ret == 0 && frame.msgType == LUCP_MTYP_FTP_LOGIN_RESULT)
            {
                log_debug("[Session %d] Got LUCP_MTYP_FTP_LOGIN_RESULT(0x%x) (FTP login result, "
                          "status=0x%x).",
                          sess->fd,
                          LUCP_MTYP_FTP_LOGIN_RESULT,
                          frame.status);
                if (frame.status != LUCP_STAT_SUCCESS)
                {
                    sess->state = LUCP_SESSION_ERROR;
                }
                else
                {
                    sess->state = LUCP_SESSION_WAITING_FTP_DOWNLOAD_RESULT;
                }
                sess->last_active_ms = get_now_ms();
            }
            else if (ret < 0)
            {
                sess->state = LUCP_SESSION_ERROR;
            }
            break;
        }
        case LUCP_SESSION_WAITING_FTP_DOWNLOAD_RESULT: {
            int ret = lucp_net_recv(&netctx, &frame);
            if (ret == 0 && frame.msgType == LUCP_MTYP_FTP_DOWNLOAD_RESULT)
            {
                log_debug("[Session %d] Got LUCP_MTYP_FTP_DOWNLOAD_RESULT(0x%x) (Download result, "
                          "status=0x%x).",
                          sess->fd,
                          LUCP_MTYP_FTP_DOWNLOAD_RESULT,
                          frame.status);
                if (frame.status != LUCP_STAT_SUCCESS)
                {
                    sess->state = LUCP_SESSION_ERROR;
                }
                else
                {
                    sess->state = LUCP_SESSION_COMPLETED;
                }
                sess->last_active_ms = get_now_ms();
            }
            else if (ret < 0)
            {
                sess->state = LUCP_SESSION_ERROR;
            }
            break;
        }
        case LUCP_SESSION_COMPLETED:
            log_debug("[Session %d] Session completed!.", sess->fd);
            running = 0;
            break;
        case LUCP_SESSION_ERROR:
            log_debug("[Session %d] Session error!.", sess->fd);
            running = 0;
            break;
        default:
            running = 0;
        }
    }

    log_debug("[Session %d] Thread exit", sess->fd);
    close(sess->fd);
    free(sess);
    return NULL;
}

int main(int argc, char** argv)
{
    lucpd_cfg_load_with_entryArgs(&g_lucpdcfg, argc, argv);

    // Set up signal handler
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    srand(time(NULL));
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        perror("socket");
        exit(1);
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(g_lucpdcfg.network.ip);
    addr.sin_port        = htons(g_lucpdcfg.network.port);

    if (bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(listen_fd);
        exit(1);
    }
    if (listen(listen_fd, g_lucpdcfg.network.max_clients) < 0)
    {
        perror("listen");
        close(listen_fd);
        exit(1);
    }
    printf("[Server] Listening on %s:%d (max_clients=%d)\n",
           g_lucpdcfg.network.ip,
           g_lucpdcfg.network.port,
           g_lucpdcfg.network.max_clients);

    int client_count = 0;
    while (server_running)
    {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd     = accept(listen_fd, (struct sockaddr*) &cli_addr, &cli_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        if (client_count >= g_lucpdcfg.network.max_clients)
        {
            printf("[Server] Max clients reached, rejecting connection\n");
            close(client_fd);
            continue;
        }
        LucpSession_t* sess  = calloc(1, sizeof(LucpSession_t));
        sess->fd             = client_fd;
        sess->config         = &g_lucpdcfg;
        sess->server_running = &server_running;
        pthread_t tid;
        pthread_create(&tid, NULL, session_thread, sess);
        pthread_detach(tid);
        client_count++;
    }
    close(listen_fd);
    printf("[Server] Exiting main loop\n");
    sleep(1); // Let threads finish
    return 0;
}
