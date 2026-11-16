/** @file ws_protocol.h @brief 不依赖套接字的有界 RFC6455 帧编解码合同。 */
#ifndef D_WS_PROTOCOL_H
#define D_WS_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#define WS_TEXT 1U
#define WS_BINARY 2U
#define WS_CLOSE 8U
#define WS_PING 9U
#define WS_PONG 10U
#define WS_RX_FRAME_START 1U
#define WS_RX_FRAME_END 2U
#define WS_RX_MESSAGE_END 4U
#define WS_RX_MESSAGE_START 8U
typedef int (*ws_frame_callback_t)(void *user, uint8_t type, uint8_t flags,
                                   const uint8_t *bytes, uint32_t length);
typedef struct {
    uint8_t header[14], control[125], scratch[256];
    uint8_t used, needed, opcode, message_type, fin, masked, expect_mask;
    uint8_t fragmented, first, failed, closed, utf8_left, utf8_min, utf8_max;
    uint64_t remaining, offset, message_length;
    uint32_t maximum;
    ws_frame_callback_t callback;
    void *user;
} ws_decoder_t;
/**
 * @brief 初始化独立于套接字的流式帧解码器。
 * @param[out] decoder 非空解码状态输出。
 * @param[in] expect_mask 非零要求接收带掩码帧，0 要求无掩码。
 * @param[in] maximum 单条数据消息的累计字节上限。
 * @param[in] callback 可选帧分段回调，非零返回使解码器失败。
 * @param[in] user 传递给回调的借用上下文。
 */
void ws_decoder_init(ws_decoder_t *decoder, uint8_t expect_mask, uint32_t maximum,
                     ws_frame_callback_t callback, void *user);
/**
 * @brief 消费本段字节，校验分片、掩码、UTF-8 和控制帧。
 * @param[in,out] decoder 已初始化解码器。
 * @param[in] data 本次输入，零长度时允许为空。
 * @param[in] length 输入字节数。
 * @return 成功返回 0；参数、协议或回调错误返回 -1。
 * @note 回调只在调用期间借用字节；协议或回调错误会锁存 failed，需重新初始化。CLOSE 后不再接受数据。
 */
int ws_decoder_feed(ws_decoder_t *decoder, const uint8_t *data, uint32_t length);
/**
 * @brief 生成有界 RFC6455 帧头。
 * @param[out] out 至少 14 字节的帧头输出。
 * @param[in] opcode 数据、续帧或控制操作码。
 * @param[in] fin 非零表示消息末帧。
 * @param[in] length 负载字节数。
 * @param[in] mask 可选 4 字节掩码，为空生成无掩码头。
 * @return 成功返回帧头字节数，参数或控制帧约束无效返回 0。
 */
unsigned ws_frame_header(uint8_t out[14], uint8_t opcode, uint8_t fin,
                         uint32_t length, const uint8_t *mask);
/**
 * @brief 检查关闭负载的长度、关闭码和 UTF-8 原因文本。
 * @param[in] data 关闭负载，零长度时允许为空。
 * @param[in] length 负载字节数，不得超过 125。
 * @return 有效返回非零，无效返回 0。
 */
int ws_close_payload_valid(const uint8_t *data, uint32_t length);
/**
 * @brief 检查一段完整文本是否为有效 UTF-8。
 * @param[in] data 完整文本，零长度时允许为空。
 * @param[in] length 文本字节数。
 * @return 有效返回非零，无效返回 0。
 */
int ws_text_valid(const uint8_t *data, uint32_t length);
#endif
