#include "lucp.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

// ================================ LOGGING ===========================

// 静态全局变量：存储当前注册的日志回调（默认NULL）
static LucpLogCallback g_log_callback = NULL;

// 注册日志回调函数的实现
void lucp_set_log_callback(LucpLogCallback callback) {
    g_log_callback = callback;
}

// 库内部使用的日志打印函数（带文件和行号）
// 内部函数，通过宏封装后使用，自动传入__FILE__和__LINE__
static void lucp_log_internal(LucpLogLevel level, const char *file, int line, const char *format, ...) {
    if (!g_log_callback) {
        return; // 未注册回调，不打印日志
    }
    // 调用可变参数回调函数（需要用va_list处理）
    va_list args;
    va_start(args, format);
    // 注意：回调函数的参数是"format, ..."，这里需要用va_arg转发
    // 由于回调函数定义为可变参数，需用vprintf风格的方式调用
    // 这里通过stdarg.h的宏实现参数转发
    g_log_callback(level, file, line, format, args);
    va_end(args);
}
// 新增：宏封装，简化内部调用（自动传入当前文件和行号）
#define LUCP_LOG(level, format, ...) \
    lucp_log_internal(level, __FILE__, __LINE__, format, ##__VA_ARGS__)


// ================================ LOGGING ===========================



/* ---------- 编译期探测本机字节序 ---------- */
static inline int is_big_endian(void)
{
    union {
        uint32_t i;
        uint8_t c[4];
    } u = {0x01020304};
    return u.c[0] == 0x01; /* 成立则为大端 */
}

/* ---------- 32 位主机↔网络字节序 ---------- */
static inline uint32_t htonl_c(uint32_t host32)
{
    if (is_big_endian())
        return host32;
    /* 小端：手动逆序 */
    return ((host32 & 0xFF000000u) >> 24) | ((host32 & 0x00FF0000u) >> 8) |
           ((host32 & 0x0000FF00u) << 8) | ((host32 & 0x000000FFu) << 24);
}

static inline uint32_t ntohl_c(uint32_t net32) { return htonl_c(net32); /* 完全对称 */ }

/* ---------- 16 位主机↔网络字节序 ---------- */
static inline uint16_t htons_c(uint16_t host16)
{
    if (is_big_endian())
        return host16;
    return (uint16_t) ((host16 >> 8) | (host16 << 8));
}

static inline uint16_t ntohs_c(uint16_t net16) { return htons_c(net16); /* 完全对称 */ }
/* ---------- 编译期探测本机字节序 ---------- */

/**
 * 将 lucp_frame_t 结构体打包到缓冲区中。
 * 成功时返回写入的字节数（>0），出错时返回 -1。
 */
int lucp_frame_pack(const lucp_frame_t* frame, uint8_t* buf, size_t buflen)
{
    if (!frame || !buf)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_frame_pack: NULL input");
        return -1;
    }

    // Header = 14 bytes
    if (buflen < (size_t) (14 + frame->textInfo_len))
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_frame_pack: Output buffer too small (%d required)", 14 + frame->textInfo_len);
        return -1;
    }
    if (frame->textInfo_len > LUCP_MAX_TEXTINFO_LEN)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_frame_pack: textInfo length %u exceeds max %d", frame->textInfo_len, LUCP_MAX_TEXTINFO_LEN);
        return -1;
    }

    size_t offset  = 0;
    uint32_t magic = htonl_c(frame->magic);
    uint32_t seq   = htonl_c(frame->seq_num);
    uint16_t plen  = htons_c(frame->textInfo_len);

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

    if (frame->textInfo_len > 0)
    {
        memcpy(buf + offset, frame->textInfo, frame->textInfo_len);
        offset += frame->textInfo_len;
    }

    LUCP_LOG(LUCP_LOG_DEBUG, "Frame packed (msgType=%u, seq=%u, status=%u, textInfo_len=%u)",
             frame->msgType, frame->seq_num, frame->status, frame->textInfo_len);

    return offset;
}

/**
 * 从缓冲区中解包 lucp_frame_t 结构。
 * 成功时返回消耗的字节数（>0），不完整时返回 0，出错时返回 -1。
 */
int lucp_frame_unpack(lucp_frame_t* frame, const uint8_t* buf, size_t buflen)
{
    if (!frame || !buf)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_frame_unpack: NULL input");
        return -1;
    }
    if (buflen < 14)
    {
        // header不完整
        return 0;
    }

    uint32_t magic;
    memcpy(&magic, buf, 4);
    magic = ntohl_c(magic);
    if (magic != LUCP_MAGIC)
    {
        LUCP_LOG(LUCP_LOG_WARN, "lucp_frame_unpack: Invalid magic 0x%08x", magic);
        return -1;
    }
    frame->magic         = magic;
    frame->version_major = buf[4];
    frame->version_minor = buf[5];

    uint32_t seq;
    memcpy(&seq, buf + 6, 4);
    frame->seq_num = ntohl_c(seq);

    frame->msgType = buf[10];
    frame->status  = buf[11];

    uint16_t plen;
    memcpy(&plen, buf + 12, 2);
    frame->textInfo_len = ntohs_c(plen);

    if (frame->textInfo_len > LUCP_MAX_TEXTINFO_LEN)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_frame_unpack: textInfo length %u exceeds max %d", frame->textInfo_len, LUCP_MAX_TEXTINFO_LEN);
        return -1;
    }
    if (buflen < (size_t) (14 + frame->textInfo_len))
    {
        // 等待更多数据
        return 0;
    }

    if (frame->textInfo_len > 0)
        memcpy(frame->textInfo, buf + 14, frame->textInfo_len);

    LUCP_LOG(LUCP_LOG_DEBUG, "Frame unpacked (msgType=%u, seq=%u, status=%u, textInfo_len=%u)",
             frame->msgType, frame->seq_num, frame->status, frame->textInfo_len);

    return 14 + frame->textInfo_len;
}

/**
 * 使用提供的字段和 textInfo 初始化一个 lucp_frame_t。
 */
void lucp_frame_make(lucp_frame_t* frame,
                     uint32_t seq,
                     uint8_t msgType,
                     uint8_t status,
                     const char* textInfo,
                     uint16_t textInfo_len)
{
    if (!frame)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_frame_make: NULL frame");
        return;
    }
    if (textInfo_len > LUCP_MAX_TEXTINFO_LEN)
    {
        LUCP_LOG(LUCP_LOG_WARN, "lucp_frame_make: textInfo_len %u exceeds max %d. Truncating.", textInfo_len, LUCP_MAX_TEXTINFO_LEN);
        textInfo_len = LUCP_MAX_TEXTINFO_LEN;
    }
    frame->magic         = LUCP_MAGIC;
    frame->version_major = LUCP_VER_MAJOR;
    frame->version_minor = LUCP_VER_MINOR;
    frame->seq_num       = seq;
    frame->msgType       = msgType;
    frame->status        = status;
    frame->textInfo_len  = textInfo_len;
    if (textInfo && textInfo_len > 0)
        memcpy(frame->textInfo, textInfo, textInfo_len);

    LUCP_LOG(LUCP_LOG_INFO, "Frame made (msgType=%u, seq=%u, status=%u, textInfo_len=%u)",
             msgType, seq, status, textInfo_len);
}

#ifndef _WIN32 
#include <sys/select.h>
#include <unistd.h>
/**
 * 初始化LUCP网络上下文。
 */
void lucp_net_ctx_init(lucp_net_ctx_t* ctx, int fd)
{
    if (!ctx)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_ctx_init: NULL ctx");
        return;
    }
    ctx->fd       = fd;
    ctx->rbuf_len = 0;
    memset(ctx->rbuf, 0, sizeof(ctx->rbuf));
    LUCP_LOG(LUCP_LOG_INFO, "Network context initialized with fd=%d", fd);
}

/**
 * 将整个缓冲区写入套接字。
 * 成功时返回 0，出错时返回 -1。
 */
static int full_write(int fd, const void* buf, size_t len)
{
    size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(fd, (const uint8_t*) buf + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            LUCP_LOG(LUCP_LOG_ERROR, "full_write: Write error: %s", strerror(errno));
            return -1;
        }
        if (n == 0)
        {
            LUCP_LOG(LUCP_LOG_WARN, "full_write: Write returned 0 (connection closed?)");
            return -1;
        }
        written += (size_t) n;
    }
    return 0;
}

/**
 * 发送要给LUCP帧
 */
int lucp_net_send(lucp_net_ctx_t* ctx, const lucp_frame_t* frame)
{
    if (!ctx || !frame)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_send: NULL input");
        return -1;
    }
    uint8_t buf[14 + LUCP_MAX_TEXTINFO_LEN];
    int len = lucp_frame_pack(frame, buf, sizeof(buf));
    if (len < 0)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_send: Frame pack failed");
        return -1;
    }
    if (full_write(ctx->fd, buf, (size_t) len) < 0)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_send: Socket write failed");
        return -1;
    }
    LUCP_LOG(LUCP_LOG_INFO, "Frame sent (msgType=%u, seq=%u)", frame->msgType, frame->seq_num);
    return 0;
}

/**
 * 接收完整的LUCP帧，处理部分读取和TCP粘包
 */
int lucp_net_recv(lucp_net_ctx_t* ctx, lucp_frame_t* frame)
{
    if (!ctx || !frame)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_recv: NULL input");
        return -1;
    }

    // 尝试从缓冲区中解析
    int parsed = lucp_frame_unpack(frame, ctx->rbuf, ctx->rbuf_len);
    if (parsed > 0)
    {
        // 移动剩余字节到缓冲区开头，供下次读取
        size_t remain = ctx->rbuf_len - parsed;
        if (remain > 0)
            memmove(ctx->rbuf, ctx->rbuf + parsed, remain);
        ctx->rbuf_len = remain;
        LUCP_LOG(LUCP_LOG_INFO, "Frame received from buffer (msgType=%u, seq=%u)", frame->msgType, frame->seq_num);
        return 0;
    }
    else if (parsed < 0)
    {
        // 缓冲区中存在损坏帧，丢弃
        ctx->rbuf_len = 0;
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_recv: Corrupted frame, buffer cleared");
        return -1;
    }

    // 需要更多数据，从网络读取
    while (1)
    {
        ssize_t n = read(ctx->fd, ctx->rbuf + ctx->rbuf_len, sizeof(ctx->rbuf) - ctx->rbuf_len);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_recv: Read error: %s", strerror(errno));
            return -1;
        }
        if (n == 0)
        {
            LUCP_LOG(LUCP_LOG_WARN, "lucp_net_recv: Socket closed by peer");
            return -1;
        }
        ctx->rbuf_len += (size_t) n;
        if (ctx->rbuf_len > sizeof(ctx->rbuf))
        {
            LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_recv: Buffer overflow");
            ctx->rbuf_len = 0;
            return -1;
        }
        parsed = lucp_frame_unpack(frame, ctx->rbuf, ctx->rbuf_len);
        if (parsed > 0)
        {
            // 移动剩余字节到缓冲区开头，供下次读取
            size_t remain = ctx->rbuf_len - parsed;
            if (remain > 0)
                memmove(ctx->rbuf, ctx->rbuf + parsed, remain);
            ctx->rbuf_len = remain;
            LUCP_LOG(LUCP_LOG_INFO, "Frame received from network (msgType=%u, seq=%u)", frame->msgType, frame->seq_num);
            return 0;
        }
        else if (parsed < 0)
        {
            LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_recv: Corrupted frame, buffer cleared");
            ctx->rbuf_len = 0;
            return -1;
        }
        // else继续读取
    }
}

/**
 * 发送一个LUCP帧，并等待预期回复，期间会进行重试。
 */
int lucp_net_send_with_retries(lucp_net_ctx_t* ctx,
                               lucp_frame_t* frame,
                               lucp_frame_t* reply,
                               uint8_t expect_cmd,
                               int n_retries,
                               int timeout_ms)
{
    if (!ctx || !frame || !reply)
    {
        LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_send_with_retries: NULL input");
        return -1;
    }

    for (int i = 0; i < n_retries; ++i)
    {
        LUCP_LOG(LUCP_LOG_DEBUG, "Send attempt %d", i + 1);
        if (lucp_net_send(ctx, frame) < 0)
        {
            LUCP_LOG(LUCP_LOG_WARN, "lucp_net_send_with_retries: Send attempt %d failed", i + 1);
            continue;
        }

        // 用select等待回复
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(ctx->fd, &rfds);
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int rv = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
        if (rv < 0 && errno != EINTR)
        {
            LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_send_with_retries: select() error: %s", strerror(errno));
            continue;
        }
        if (rv == 0)
        {
            LUCP_LOG(LUCP_LOG_WARN, "lucp_net_send_with_retries: Timeout waiting for reply (attempt %d)", i + 1);
            continue;
        }
        if (FD_ISSET(ctx->fd, &rfds))
        {
            if (lucp_net_recv(ctx, reply) == 0 && reply->msgType == expect_cmd &&
                reply->seq_num == frame->seq_num)
            {
                LUCP_LOG(LUCP_LOG_INFO, "lucp_net_send_with_retries: Received expected reply (msgType=%u, seq=%u)", reply->msgType, reply->seq_num);
                return 0;
            }
            else
            {
                LUCP_LOG(LUCP_LOG_WARN, "lucp_net_send_with_retries: Unexpected reply or seq/msgType mismatch");
            }
        }
        // else继续重试
    }
    LUCP_LOG(LUCP_LOG_ERROR, "lucp_net_send_with_retries: Failed after %d retries", n_retries);
    return -1;
}
#endif // !_WIN32