#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>
#include "lucp.h"


#define DEMO_PORT 23456

// Simulate archiving and FTP upload step
void simulate_log_prep(int *status, char *payload, size_t payload_size) {
    // Randomly choose outcome: 80% success, 10% archive fail, 10% FTP fail
    int r = rand() % 10;
    if (r < 8) {
        *status = 1;
        snprintf(payload, payload_size, "demo_logfile_20250925.log");
    } else if (r == 8) {
        *status = 0x10;
        snprintf(payload, payload_size, "Archive failed: disk full");
    } else {
        *status = 0x11;
        snprintf(payload, payload_size, "FTP upload failed: connection timeout");
    }
    usleep(700 * 1000); // Simulate delay (700ms)
}

int main() {
    srand(time(NULL));
    int listenfd, connfd;
    struct sockaddr_in addr;
    // char msg[256];

    // Create and bind socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DEMO_PORT);

    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(listenfd); return 1;
    }
    if (listen(listenfd, 1) < 0) {
        perror("listen"); close(listenfd); return 1;
    }
    printf("LUCP demo server listening on port %d...\n", DEMO_PORT);

    connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) { perror("accept"); close(listenfd); return 1; }
    printf("LUCP demo server: client connected\n");

    lucp_net_ctx_t netctx;
    lucp_net_ctx_init(&netctx, connfd);
    lucp_frame_t frame, reply;

    // 1. Wait for 0x01 request
    if (lucp_net_recv(&netctx, &frame) == 0 && frame.cmd == 0x01) {
        printf("[Server] Received 0x01 request (seq=%u)\n", frame.seq_num);
        // 2. Immediately respond with 0x02 ACK
        lucp_frame_make(&reply, frame.seq_num, 0x02, 1, NULL, 0);
        lucp_net_send(&netctx, &reply);
        printf("[Server] Sent 0x02 ACK\n");

        // 3. Simulate log prep (archive + FTP upload)
        int prep_status = 0;
        char prep_payload[256] = {0};
        simulate_log_prep(&prep_status, prep_payload, sizeof(prep_payload));

        // 4. Send 0x03 with result
        lucp_frame_make(&reply, frame.seq_num, 0x03, prep_status, prep_payload, strlen(prep_payload));
        lucp_net_send(&netctx, &reply);
        printf("[Server] Sent 0x03 (status=0x%x, payload=\"%s\")\n", prep_status, prep_payload);

        // 5. Wait for 0x04 (FTP login confirm)
        if (lucp_net_recv(&netctx, &frame) == 0 && frame.cmd == 0x04) {
            printf("[Server] Received 0x04 (FTP login result, status=0x%x)\n", frame.status);
            // 6. Wait for 0x05 (download confirm)
            if (lucp_net_recv(&netctx, &frame) == 0 && frame.cmd == 0x05) {
                printf("[Server] Received 0x05 (download result, status=0x%x)\n", frame.status);
            }
        }
    }

    close(connfd);
    close(listenfd);
    printf("LUCP demo server: done\n");
    return 0;
}