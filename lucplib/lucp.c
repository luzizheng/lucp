#include "lucp.h"
#include <stdio.h>
#include <string.h>

/* ---------- 编译期探测本机字节序 ---------- */
static inline int is_big_endian(void) {
  union {
    uint32_t i;
    uint8_t c[4];
  } u = {0x01020304};
  return u.c[0] == 0x01; /* 成立则为大端 */
}

/* ---------- 32 位主机↔网络字节序 ---------- */
static inline uint32_t htonl_c(uint32_t host32) {
  if (is_big_endian())
    return host32;
  /* 小端：手动逆序 */
  return ((host32 & 0xFF000000u) >> 24) | ((host32 & 0x00FF0000u) >> 8) |
         ((host32 & 0x0000FF00u) << 8) | ((host32 & 0x000000FFu) << 24);
}

static inline uint32_t ntohl_c(uint32_t net32) {
  return htonl_c(net32); /* 完全对称 */
}

/* ---------- 16 位主机↔网络字节序 ---------- */
static inline uint16_t htons_c(uint16_t host16) {
  if (is_big_endian())
    return host16;
  return (uint16_t)((host16 >> 8) | (host16 << 8));
}

static inline uint16_t ntohs_c(uint16_t net16) {
  return htons_c(net16); /* 完全对称 */
}
/* ---------- 编译期探测本机字节序 ---------- */

/**
 * Packs a lucp_frame_t into a buffer.
 * Returns number of bytes written (>0) on success, -1 on error.
 */
int lucp_frame_pack(const lucp_frame_t *frame, uint8_t *buf, size_t buflen) {
  if (!frame || !buf) {
    fprintf(stderr, "[LUCP] lucp_frame_pack: NULL input\n");
    return -1;
  }

  // Header = 14 bytes
  if (buflen < (size_t)(14 + frame->textInfo_len)) {
    fprintf(stderr,
            "[LUCP] lucp_frame_pack: Output buffer too small (%d required)\n",
            14 + frame->textInfo_len);
    return -1;
  }
  if (frame->textInfo_len > LUCP_MAX_TEXTINFO_LEN) {
    fprintf(stderr,
            "[LUCP] lucp_frame_pack: textInfo length %u exceeds max %d\n",
            frame->textInfo_len, LUCP_MAX_TEXTINFO_LEN);
    return -1;
  }

  size_t offset = 0;
  uint32_t magic = htonl_c(frame->magic);
  uint32_t seq = htonl_c(frame->seq_num);
  uint16_t plen = htons_c(frame->textInfo_len);

  memcpy(buf + offset, &magic, 4);
  offset += 4;
  buf[offset++] = frame->version_major;
  buf[offset++] = frame->version_minor;
  memcpy(buf + offset, &seq, 4);
  offset += 4;
  buf[offset++] = frame->msgType;
  buf[offset++] = frame->status;
  memcpy(buf + offset, &plen, 2);
  offset += 2;

  if (frame->textInfo_len > 0) {
    memcpy(buf + offset, frame->textInfo, frame->textInfo_len);
    offset += frame->textInfo_len;
  }

  fprintf(
      stderr,
      "[LUCP] Frame packed (msgType=%u, seq=%u, status=%u, textInfo_len=%u)\n",
      frame->msgType, frame->seq_num, frame->status, frame->textInfo_len);

  return offset;
}

/**
 * Unpacks a lucp_frame_t from buffer.
 * Returns bytes consumed (>0) on success, 0 if incomplete, -1 on error.
 */
int lucp_frame_unpack(lucp_frame_t *frame, const uint8_t *buf, size_t buflen) {
  if (!frame || !buf) {
    fprintf(stderr, "[LUCP] lucp_frame_unpack: NULL input\n");
    return -1;
  }
  if (buflen < 14) {
    // Not enough for header
    return 0;
  }

  uint32_t magic;
  memcpy(&magic, buf, 4);
  magic = ntohl_c(magic);
  if (magic != LUCP_MAGIC) {
    fprintf(stderr, "[LUCP] lucp_frame_unpack: Invalid magic 0x%08x\n", magic);
    return -1;
  }
  frame->magic = magic;
  frame->version_major = buf[4];
  frame->version_minor = buf[5];

  uint32_t seq;
  memcpy(&seq, buf + 6, 4);
  frame->seq_num = ntohl_c(seq);

  frame->msgType = buf[10];
  frame->status = buf[11];

  uint16_t plen;
  memcpy(&plen, buf + 12, 2);
  frame->textInfo_len = ntohs_c(plen);

  if (frame->textInfo_len > LUCP_MAX_TEXTINFO_LEN) {
    fprintf(stderr,
            "[LUCP] lucp_frame_unpack: textInfo length %u exceeds max %d\n",
            frame->textInfo_len, LUCP_MAX_TEXTINFO_LEN);
    return -1;
  }
  if (buflen < (size_t)(14 + frame->textInfo_len)) {
    // Wait for more data
    return 0;
  }

  if (frame->textInfo_len > 0)
    memcpy(frame->textInfo, buf + 14, frame->textInfo_len);

  fprintf(stderr,
          "[LUCP] Frame unpacked (msgType=%u, seq=%u, status=%u, "
          "textInfo_len=%u)\n",
          frame->msgType, frame->seq_num, frame->status, frame->textInfo_len);

  return 14 + frame->textInfo_len;
}

/**
 * Initializes a lucp_frame_t with the provided fields and textInfo.
 */
void lucp_frame_make(lucp_frame_t *frame,
                     uint32_t seq,
                     uint8_t msgType,
                     uint8_t status,
                     const char *textInfo,
                     uint16_t textInfo_len) {
  if (!frame) {
    fprintf(stderr, "[LUCP] lucp_frame_make: NULL frame\n");
    return;
  }
  if (textInfo_len > LUCP_MAX_TEXTINFO_LEN) {
    fprintf(
        stderr,
        "[LUCP] lucp_frame_make: textInfo_len %u exceeds max %d. Truncating.\n",
        textInfo_len, LUCP_MAX_TEXTINFO_LEN);
    textInfo_len = LUCP_MAX_TEXTINFO_LEN;
  }
  frame->magic = LUCP_MAGIC;
  frame->version_major = LUCP_VER_MAJOR;
  frame->version_minor = LUCP_VER_MINOR;
  frame->seq_num = seq;
  frame->msgType = msgType;
  frame->status = status;
  frame->textInfo_len = textInfo_len;
  if (textInfo && textInfo_len > 0)
    memcpy(frame->textInfo, textInfo, textInfo_len);

  fprintf(
      stderr,
      "[LUCP] Frame made (msgType=%u, seq=%u, status=%u, textInfo_len=%u)\n",
      msgType, seq, status, textInfo_len);
}

#ifndef _WIN32
#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
/**
 * Initializes the LUCP network context.
 */
void lucp_net_ctx_init(lucp_net_ctx_t *ctx, int fd) {
  if (!ctx) {
    fprintf(stderr, "[LUCP] lucp_net_ctx_init: NULL ctx\n");
    return;
  }
  ctx->fd = fd;
  ctx->rbuf_len = 0;
  memset(ctx->rbuf, 0, sizeof(ctx->rbuf));
  fprintf(stderr, "[LUCP] Network context initialized with fd=%d\n", fd);
}

/**
 * Writes the entire buffer to the socket.
 * Returns 0 on success, -1 on error.
 */
static int full_write(int fd, const void *buf, size_t len) {
  size_t written = 0;
  while (written < len) {
    ssize_t n = write(fd, (const uint8_t *)buf + written, len - written);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "[LUCP] full_write: Write error: %s\n", strerror(errno));
      return -1;
    }
    if (n == 0) {
      fprintf(stderr,
              "[LUCP] full_write: Write returned 0 (connection closed?)\n");
      return -1;
    }
    written += (size_t)n;
  }
  return 0;
}

/**
 * Sends a LUCP frame.
 */
int lucp_net_send(lucp_net_ctx_t *ctx, const lucp_frame_t *frame) {
  if (!ctx || !frame) {
    fprintf(stderr, "[LUCP] lucp_net_send: NULL input\n");
    return -1;
  }
  uint8_t buf[14 + LUCP_MAX_TEXTINFO_LEN];
  int len = lucp_frame_pack(frame, buf, sizeof(buf));
  if (len < 0) {
    fprintf(stderr, "[LUCP] lucp_net_send: Frame pack failed\n");
    return -1;
  }
  if (full_write(ctx->fd, buf, (size_t)len) < 0) {
    fprintf(stderr, "[LUCP] lucp_net_send: Socket write failed\n");
    return -1;
  }
  fprintf(stderr, "[LUCP] Frame sent (msgType=%u, seq=%u)\n", frame->msgType,
          frame->seq_num);
  return 0;
}

/**
 * Receives a complete LUCP frame, handling partial reads and TCP sticking.
 */
int lucp_net_recv(lucp_net_ctx_t *ctx, lucp_frame_t *frame) {
  if (!ctx || !frame) {
    fprintf(stderr, "[LUCP] lucp_net_recv: NULL input\n");
    return -1;
  }

  // Try to parse from buffer first
  int parsed = lucp_frame_unpack(frame, ctx->rbuf, ctx->rbuf_len);
  if (parsed > 0) {
    // Move leftover bytes to start of buffer for next read
    size_t remain = ctx->rbuf_len - parsed;
    if (remain > 0)
      memmove(ctx->rbuf, ctx->rbuf + parsed, remain);
    ctx->rbuf_len = remain;
    fprintf(stderr, "[LUCP] Frame received from buffer (msgType=%u, seq=%u)\n",
            frame->msgType, frame->seq_num);
    return 0;
  } else if (parsed < 0) {
    // Corrupted frame in buffer, discard
    ctx->rbuf_len = 0;
    fprintf(stderr, "[LUCP] lucp_net_recv: Corrupted frame, buffer cleared\n");
    return -1;
  }

  // Need more data
  while (1) {
    ssize_t n = read(ctx->fd, ctx->rbuf + ctx->rbuf_len,
                     sizeof(ctx->rbuf) - ctx->rbuf_len);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "[LUCP] lucp_net_recv: Read error: %s\n",
              strerror(errno));
      return -1;
    }
    if (n == 0) {
      fprintf(stderr, "[LUCP] lucp_net_recv: Socket closed by peer\n");
      return -1;
    }
    ctx->rbuf_len += (size_t)n;
    if (ctx->rbuf_len > sizeof(ctx->rbuf)) {
      fprintf(stderr, "[LUCP] lucp_net_recv: Buffer overflow\n");
      ctx->rbuf_len = 0;
      return -1;
    }
    parsed = lucp_frame_unpack(frame, ctx->rbuf, ctx->rbuf_len);
    if (parsed > 0) {
      // Move leftover bytes to start of buffer for next read
      size_t remain = ctx->rbuf_len - parsed;
      if (remain > 0)
        memmove(ctx->rbuf, ctx->rbuf + parsed, remain);
      ctx->rbuf_len = remain;
      fprintf(stderr,
              "[LUCP] Frame received from network (msgType=%u, seq=%u)\n",
              frame->msgType, frame->seq_num);
      return 0;
    } else if (parsed < 0) {
      fprintf(stderr,
              "[LUCP] lucp_net_recv: Corrupted frame, buffer cleared\n");
      ctx->rbuf_len = 0;
      return -1;
    }
    // else (parsed == 0): keep reading
  }
}

/**
 * Sends a LUCP frame and waits for a matching reply, with retries and timeout.
 */
int lucp_net_send_with_retries(lucp_net_ctx_t *ctx,
                               lucp_frame_t *frame,
                               lucp_frame_t *reply,
                               uint8_t expect_cmd,
                               int n_retries,
                               int timeout_ms) {
  if (!ctx || !frame || !reply) {
    fprintf(stderr, "[LUCP] lucp_net_send_with_retries: NULL input\n");
    return -1;
  }

  for (int i = 0; i < n_retries; ++i) {
    if (lucp_net_send(ctx, frame) < 0) {
      fprintf(stderr,
              "[LUCP] lucp_net_send_with_retries: Send attempt %d failed\n",
              i + 1);
      continue;
    }

    // Wait for reply with select()
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(ctx->fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rv = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
    if (rv < 0 && errno != EINTR) {
      fprintf(stderr, "[LUCP] lucp_net_send_with_retries: select() error: %s\n",
              strerror(errno));
      continue;
    }
    if (rv == 0) {
      fprintf(stderr,
              "[LUCP] lucp_net_send_with_retries: Timeout waiting for reply "
              "(attempt %d)\n",
              i + 1);
      continue;
    }
    if (FD_ISSET(ctx->fd, &rfds)) {
      if (lucp_net_recv(ctx, reply) == 0 && reply->msgType == expect_cmd &&
          reply->seq_num == frame->seq_num) {
        fprintf(stderr,
                "[LUCP] lucp_net_send_with_retries: Received expected reply "
                "(msgType=%u, seq=%u)\n",
                reply->msgType, reply->seq_num);
        return 0;
      } else {
        fprintf(stderr, "[LUCP] lucp_net_send_with_retries: Unexpected reply "
                        "or seq/msgType mismatch\n");
      }
    }
    // else retry
  }
  fprintf(stderr,
          "[LUCP] lucp_net_send_with_retries: Failed after %d retries\n",
          n_retries);
  return -1;
}
#endif // !_WIN32