#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "lucp.h"
#include <sys/select.h>

#define DEMO_SERVER_IP   "127.0.0.1"
#define DEMO_SERVER_PORT 32100

// Simulate FTP login: 80% success, 20% fail
int simulate_ftp_login(char *reason, size_t sz) {
    int r = rand() % 10;
    if (r < 8) return 1;
    snprintf(reason, sz, "FTP login failed: bad credentials");
    return 0x20;
}

// Simulate FTP download: 80% success, 20% fail
int simulate_ftp_download(char *reason, size_t sz) {
    int r = rand() % 10;
    if (r < 8) return 1;
    snprintf(reason, sz, "FTP download failed: network error");
    return 0x21;
}

int main() {
    srand(time(NULL) ^ getpid());
    int sockfd;
    struct sockaddr_in servaddr;

    // Create and connect socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(DEMO_SERVER_PORT);
    if (inet_pton(AF_INET, DEMO_SERVER_IP, &servaddr.sin_addr) != 1) {
        perror("inet_pton"); close(sockfd); return 1;
    }
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect"); close(sockfd); return 1;
    }
    printf("LUCP demo client: Connected to %s:%d\n", DEMO_SERVER_IP, DEMO_SERVER_PORT);

    lucp_net_ctx_t netctx;
    lucp_net_ctx_init(&netctx, sockfd);
    lucp_frame_t req, reply;
    uint32_t seq = 10001;
    char payload[256];

    // 1. Send 0x01 request
    lucp_frame_make(&req, seq, 0x01, 0, "Request log preparation", 23);
    lucp_net_send(&netctx, &req);
    printf("[Client] Sent 0x01 request\n");

    // 2. Wait for 0x02 ACK (2s timeout)
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds); FD_SET(sockfd, &rfds);
    tv.tv_sec = 2; tv.tv_usec = 0;
    int rv = select(sockfd+1, &rfds, NULL, NULL, &tv);
    if (rv > 0 && FD_ISSET(sockfd, &rfds)) {
        if (lucp_net_recv(&netctx, &reply) == 0 && reply.msgType == 0x02) {
            printf("[Client] Got 0x02 ACK (seq=%u)\n", reply.seq_num);
        } else {
            printf("[Client] Did not receive proper 0x02 ACK\n");
            close(sockfd); return 1;
        }
    } else {
        printf("[Client] Timeout waiting for 0x02\n");
        close(sockfd); return 1;
    }

    // 3. Wait for 0x03 (log ready) up to 5s
    FD_ZERO(&rfds); FD_SET(sockfd, &rfds);
    tv.tv_sec = 5; tv.tv_usec = 0;
    rv = select(sockfd+1, &rfds, NULL, NULL, &tv);
    if (rv > 0 && FD_ISSET(sockfd, &rfds)) {
        if (lucp_net_recv(&netctx, &reply) == 0 && reply.msgType == 0x03) {
            printf("[Client] Got 0x03 (status=0x%x, payload=\"%.*s\")\n",
                reply.status, reply.textInfo_len, reply.textInfo);
            if (reply.status != 1) {
                printf("[Client] Log preparation failed, reason: %.*s\n", reply.textInfo_len, reply.textInfo);
                close(sockfd); return 1;
            }
        } else {
            printf("[Client] Did not receive 0x03\n");
            close(sockfd); return 1;
        }
    } else {
        printf("[Client] Timeout waiting for 0x03\n");
        close(sockfd); return 1;
    }

    // 4. Simulate FTP login
    int status = simulate_ftp_login(payload, sizeof(payload));
    lucp_frame_make(&req, seq, 0x04, status, payload, strlen(payload));
    lucp_net_send(&netctx, &req);
    printf("[Client] Sent 0x04 (FTP login %s)\n", status == 1 ? "success" : "failure");
    if (status != 1) { close(sockfd); return 1; }

    // 5. Simulate FTP download
    status = simulate_ftp_download(payload, sizeof(payload));
    lucp_frame_make(&req, seq, 0x05, status, payload, strlen(payload));
    lucp_net_send(&netctx, &req);
    printf("[Client] Sent 0x05 (Download %s)\n", status == 1 ? "success" : "failure");

    close(sockfd);
    printf("LUCP demo client: done\n");
    return 0;
}