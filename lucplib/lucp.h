#ifndef LUCP_H
#define LUCP_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * LUCP Protocol Constants
 */
#define LUCP_MAGIC 0x4C554350 // ASCII 'LUCP'
#define LUCP_VER_MAJOR 1
#define LUCP_VER_MINOR 0
#define LUCP_MAX_PAYLOAD 1010  // Business logic: payload 0~1010

/**
 * LUCP Frame Structure
 */
typedef struct {
    uint32_t magic;             // Must be LUCP_MAGIC ('LUCP')
    uint8_t  version_major;     // Protocol major version
    uint8_t  version_minor;     // Protocol minor version
    uint32_t seq_num;           // Sequence number (for retransmission, etc.)
    uint8_t  cmd;               // Command type
    uint8_t  status;            // Status/result code
    uint16_t payload_len;       // Payload length (0~1010)
    uint8_t  payload[LUCP_MAX_PAYLOAD]; // Payload data (may be raw or text, not null-terminated)
} lucp_frame_t;

/**
 * Packs a LUCP frame into a byte buffer.
 * Returns total bytes written on success, -1 on error.
 */
int lucp_frame_pack(const lucp_frame_t* frame, uint8_t* buf, size_t buflen);

/**
 * Unpacks a LUCP frame from a byte buffer.
 * Returns bytes consumed on success, 0 if incomplete, -1 on error.
 */
int lucp_frame_unpack(lucp_frame_t* frame, const uint8_t* buf, size_t buflen);

/**
 * Initializes a LUCP frame with specified fields and payload.
 */
void lucp_frame_make(lucp_frame_t* frame, uint32_t seq, uint8_t cmd, uint8_t status,
                     const void* payload, uint16_t payload_len);


#ifndef _WIN32 
/**
 * LUCP Network Context (for reassembly and socket state)
 */
typedef struct {
    int fd;                 // Socket file descriptor
    uint8_t rbuf[2048];     // Receive buffer for partial reads
    size_t rbuf_len;        // Number of bytes currently in rbuf
} lucp_net_ctx_t;

/**
 * Initializes a LUCP network context with a connected socket fd.
 */
void lucp_net_ctx_init(lucp_net_ctx_t* ctx, int fd);

/**
 * Sends a LUCP frame over the network.
 * Returns 0 on success, -1 on error.
 */
int lucp_net_send(lucp_net_ctx_t* ctx, const lucp_frame_t* frame);

/**
 * Receives a complete LUCP frame (handles TCP sticking/fragmentation).
 * Returns 0 on success, -1 on error.
 */
int lucp_net_recv(lucp_net_ctx_t* ctx, lucp_frame_t* frame);

/**
 * Sends a LUCP frame and waits for expected reply with retries.
 * Returns 0 on success, -1 on error.
 */
int lucp_net_send_with_retries(
    lucp_net_ctx_t* ctx,
    lucp_frame_t* frame,
    lucp_frame_t* reply,
    uint8_t expect_cmd,
    int n_retries,
    int timeout_ms
);
#endif // !_WIN32

#ifdef __cplusplus
}
#endif // __cplusplus


#endif // LUCP_H