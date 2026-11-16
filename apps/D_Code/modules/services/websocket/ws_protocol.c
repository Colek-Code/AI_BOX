/** @file ws_protocol.c @brief 流式帧解析，不依据对端长度动态分配内存。 */
#include "ws_protocol.h"
#include <string.h>

/**
 * @brief 推进一个字节的 UTF-8 校验状态。
 * @param[in,out] left 仍需的续字节数。
 * @param[in,out] low 下一个续字节的允许下界。
 * @param[in,out] high 下一个续字节的允许上界。
 * @param[in] c 当前字节。
 * @return 当前字节合法返回 0，否则返回 -1。
 */
static int ws_utf8(uint8_t *left, uint8_t *low, uint8_t *high, uint8_t c)
{
    if (*left) {
        if (c < *low || c > *high) return -1;
        --*left; *low = 0x80; *high = 0xbf;
    } else if (c < 0x80) {
        return 0;
    } else {
        *low = 0x80; *high = 0xbf;
        if (c >= 0xc2 && c <= 0xdf) *left = 1;
        else if (c >= 0xe0 && c <= 0xef) {
            *left = 2;
            if (c == 0xe0) *low = 0xa0;
            if (c == 0xed) *high = 0x9f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            *left = 3;
            if (c == 0xf0) *low = 0x90;
            if (c == 0xf4) *high = 0x8f;
        } else return -1;
    }
    return 0;
}

/**
 * @brief 检查一段完整文本是否为有效 UTF-8；完整合同见 ws_protocol.h。
 */
int ws_text_valid(const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint8_t left = 0, low = 0, high = 0;
    if (!data && length) return 0;
    for (i = 0; i < length; ++i) if (ws_utf8(&left, &low, &high, data[i])) return 0;
    return left == 0;
}

/**
 * @brief 检查关闭负载的长度、关闭码和 UTF-8 原因文本；完整合同见 ws_protocol.h。
 */
int ws_close_payload_valid(const uint8_t *data, uint32_t length)
{
    uint16_t code;
    if (length > 125 || length == 1 || (length && !data)) return 0;
    if (!length) return 1;
    code = (uint16_t)(((unsigned)data[0] << 8) | data[1]);
    if (code < 1000 || code >= 5000 || code == 1004 || code == 1005 ||
        code == 1006 || (code >= 1016 && code < 3000) || code == 1015) return 0;
    return ws_text_valid(data + 2, length - 2);
}

/**
 * @brief 初始化独立于套接字的流式帧解码器；完整合同见 ws_protocol.h。
 */
void ws_decoder_init(ws_decoder_t *d, uint8_t mask, uint32_t maximum,
                     ws_frame_callback_t callback, void *user)
{
    memset(d, 0, sizeof(*d));
    d->expect_mask = !!mask; d->maximum = maximum;
    d->needed = 2; d->callback = callback; d->user = user;
}

/**
 * @brief 校验完整帧头并更新消息分片与剩余字节状态。
 * @param[in,out] d 已累积完整头的解码器。
 * @return 有效返回 0，协议或消息容量错误返回 -1。
 */
static int ws_decode_header(ws_decoder_t *d)
{
    uint64_t length = d->header[1] & 127U;
    unsigned i, count = length == 126 ? 2 : length == 127 ? 8 : 0;
    d->opcode = d->header[0] & 15U;
    d->fin = !!(d->header[0] & 128U);
    d->masked = !!(d->header[1] & 128U);
    if ((d->header[0] & 0x70) || d->masked != d->expect_mask) return -1;
    if (count) {
        length = 0;
        for (i = 0; i < count; ++i) length = (length << 8) | d->header[2 + i];
        if ((count == 2 && length < 126) ||
            (count == 8 && (length < 65536 || (d->header[2] & 128)))) return -1;
    }
    if (d->opcode >= 8) {
        if (d->opcode > 10 || !d->fin || length > 125) return -1;
    } else {
        if (d->opcode > 2 || (!d->opcode && !d->fragmented) ||
            (d->opcode && d->fragmented)) return -1;
        if (d->opcode) {
            d->message_type = d->opcode; d->message_length = 0; d->utf8_left = 0;
        }
        if (length > d->maximum || d->message_length > d->maximum - length) return -1;
        d->message_length += length;
        d->fragmented = !d->fin;
    }
    d->remaining = length; d->offset = 0; d->first = 1;
    return 0;
}

/**
 * @brief 构造帧及消息边界标志并同步交付借用负载。
 * @param[in,out] d 当前解码状态。
 * @param[in] data 只在回调执行期间有效的负载。
 * @param[in] count 本段负载字节数。
 * @return 无回调或回调成功返回 0；文本残缺或回调拒绝返回非零。
 */
static int ws_deliver(ws_decoder_t *d, const uint8_t *data, uint32_t count)
{
    uint8_t flags = d->first ? WS_RX_FRAME_START : 0;
    uint8_t type = d->opcode >= 8 ? d->opcode : d->message_type;
    if (d->first && (d->opcode == WS_TEXT || d->opcode == WS_BINARY)) flags |= WS_RX_MESSAGE_START;
    d->first = 0;
    if (!d->remaining) {
        flags |= WS_RX_FRAME_END;
        if (d->fin) flags |= WS_RX_MESSAGE_END;
        if (type == WS_TEXT && d->fin && d->utf8_left) return -1;
    }
    return d->callback ? d->callback(d->user, type, flags, data, count) : 0;
}

/**
 * @brief 消费本段字节，校验分片、掩码、UTF-8 和控制帧；完整合同见 ws_protocol.h。
 */
int ws_decoder_feed(ws_decoder_t *d, const uint8_t *data, uint32_t length)
{
    uint32_t pos = 0, count, i;
    if (!d || (!data && length) || d->failed || d->closed) return -1;
    while (pos < length || (d->used == d->needed && !d->remaining)) {
        if (d->used < d->needed) {
            d->header[d->used++] = data[pos++];
            if (d->used == 2) d->needed = (uint8_t)(2 +
                ((d->header[1] & 127) == 126 ? 2 : (d->header[1] & 127) == 127 ? 8 : 0) +
                ((d->header[1] & 128) ? 4 : 0));
            if (d->used != d->needed) continue;
            if (ws_decode_header(d)) goto bad;
        }
        count = length - pos;
        if (count > sizeof(d->scratch)) count = sizeof(d->scratch);
        if (count > d->remaining) count = (uint32_t)d->remaining;
        for (i = 0; i < count; ++i) {
            uint8_t c = data[pos + i];
            if (d->masked) c ^= d->header[d->needed - 4 + ((d->offset + i) & 3U)];
            d->scratch[i] = c;
            if (d->opcode >= 8) d->control[(unsigned)d->offset + i] = c;
            else if (d->message_type == WS_TEXT &&
                     ws_utf8(&d->utf8_left, &d->utf8_min, &d->utf8_max, c)) goto bad;
        }
        pos += count; d->offset += count; d->remaining -= count;
        if (d->opcode < 8) {
            if ((!d->remaining && d->fin && d->utf8_left) || ws_deliver(d, d->scratch, count)) goto bad;
        } else if (!d->remaining) {
            if (d->opcode == WS_CLOSE && !ws_close_payload_valid(d->control, (uint32_t)d->offset)) goto bad;
            if (ws_deliver(d, d->control, (uint32_t)d->offset)) goto bad;
        }
        if (!d->remaining) {
            if (d->opcode == WS_CLOSE) { d->closed = 1; return 0; }
            d->used = 0; d->needed = 2;
        } else if (!count) break;
    }
    return 0;
bad:
    d->failed = 1;
    return -1;
}

/**
 * @brief 生成有界 RFC6455 帧头；完整合同见 ws_protocol.h。
 */
unsigned ws_frame_header(uint8_t out[14], uint8_t opcode, uint8_t fin,
                         uint32_t length, const uint8_t *mask)
{
    unsigned n = 2, i;
    if (!out || (opcode > 2 && (opcode < 8 || opcode > 10)) ||
        (opcode >= 8 && (!fin || length > 125))) return 0;
    out[0] = (uint8_t)(opcode | (fin ? 128 : 0));
    if (length < 126) out[1] = (uint8_t)length;
    else if (length < 65536) {
        out[1] = 126; out[2] = (uint8_t)(length >> 8); out[3] = (uint8_t)length; n = 4;
    } else {
        out[1] = 127; memset(out + 2, 0, 4);
        for (i = 0; i < 4; ++i) out[6 + i] = (uint8_t)(length >> (24 - 8 * i));
        n = 10;
    }
    if (mask) { out[1] |= 128; memcpy(out + n, mask, 4); n += 4; }
    return n;
}
