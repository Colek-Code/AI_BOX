/** @file qwen_request_gate.h @brief 提供非阻塞的 SDK 响应槽所有权门禁，不接管业务资源。 */
#ifndef QWEN_REQUEST_GATE_H
#define QWEN_REQUEST_GATE_H
#include <stdint.h>
typedef enum {
    QWEN_REQUEST_NONE = 0, QWEN_REQUEST_NORMAL, QWEN_REQUEST_TOOL,
    QWEN_REQUEST_BROKEN
} qwen_request_owner_e;
/**
 * @brief 尝试为普通请求或工具请求占用 SDK 响应槽。
 * @param[in] owner 只接受 QWEN_REQUEST_NORMAL 或 QWEN_REQUEST_TOOL。
 * @return 1 为取得提交占用；所有者类型无效、响应槽已有归属或提交/提取在途时为 0。
 * @note 成功后必须调用 submitted 报告 SDK 是否接受，包括 SDK 拒绝路径；没有占用成功则不调用配对完成。SDK 及其同步回调在锁外执行。
 */
uint8_t qwen_request_begin(qwen_request_owner_e owner);
/**
 * @brief 结束已取得的提交占用，并根据 SDK 结果保留或清除响应归属。
 * @param[in] accepted 非零表示 SDK 已接受请求；零表示拒绝。
 * @note 仅配对成功的 request_begin；成功接受后仍须等待响应被提取或旧传输清理，不把受理当作响应完成。
 */
void qwen_request_submitted(int accepted);
/**
 * @brief 由唯一发送线程尝试取得一次 SDK 数据提取占用。
 * @return 1 为取得提取占用；提交或提取已在途时为 0，发送线程须跳过本轮。
 * @note 成功后调用 generate_end 配对，不在自旋锁中执行 SDK 提取。
 */
uint8_t qwen_request_generate_begin(void);
/**
 * @brief 结束本次提取，并按响应分类返回该帧所有者。
 * @param[in] response_frame 1 表示响应，0 表示其他有效帧，-1 表示无法分类。
 * @param[in] empty 保留的空提取标记，当前实现不据此清除所有者。
 * @return 响应帧返回此前所有者并清槽；分类失败返回 BROKEN 并清槽；其他帧或没有提取占用时返回 NONE。
 * @note 空提取可能仍在等待 SDK 控制命令，非响应帧也不结束响应归属；截止时间及最终 reset 由业务/传输所有者管理。BROKEN 的帧不得发送。
 */
qwen_request_owner_e qwen_request_generate_end(int response_frame, uint8_t empty);
/**
 * @brief 在旧传输所有执行者实际退出后清空响应槽及在途标志。
 * @note 调用方先确认旧发送/接收线程和 SDK 使用者均已结束；本函数只清本地门禁，不停止线程或取消 SDK 操作。
 */
void qwen_request_gate_reset(void);
#endif
