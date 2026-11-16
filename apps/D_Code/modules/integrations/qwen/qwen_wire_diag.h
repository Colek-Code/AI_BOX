/** @file qwen_wire_diag.h @brief SDK 解析前的只读 WSS 字段诊断接口。 */
#ifndef QWEN_WIRE_DIAG_H
#define QWEN_WIRE_DIAG_H
#include <stdint.h>

/**
 * @brief 在 SDK 解析前只读观察收到的 WSS 文本字段。
 * @param[in] data 调用期间借用的原始帧。
 * @param[in] length 原始帧字节数。
 * @note 仅接收线程调用；不修改输入，不调用 SDK，不改变业务或传输决策。
 */
void qwen_wire_diag_rx(const uint8_t *data, uint32_t length);
/**
 * @brief 工具解码后把请求编号关联到紧邻的 RX 诊断序列。
 * @param[in] request_id 可空、调用期间借用的工具请求编号。
 * @note 仅接收线程调用，不保留该指针。
 */
void qwen_wire_diag_tool_request(const char *request_id);
/**
 * @brief 套接字返回后记录发送的选定字段和结果请求编号。
 * @param[in] data 调用期间借用的原始帧。
 * @param[in] length 原始字节数。
 * @param[in] sent 套接字发送返回值。
 * @param[in] owner 非空发送所有者诊断名称。
 * @note 仅发送线程调用；不输出参数对象、结果内容或认证字段。
 */
void qwen_wire_diag_tx(const uint8_t *data, uint32_t length, int sent, const char *owner);
#endif
