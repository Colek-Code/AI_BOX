/** @file qwen_request_gate.c @brief 协调 SDK 响应请求与唯一发送线程的数据提取，锁不跨 SDK 调用。 */
#include "config/app_config.h"
#if D_FEATURE_AI_ALARM
#include "qwen_request_gate.h"
#include "system/spinlock.h"
static DEFINE_SPINLOCK(s_request_lock);
static qwen_request_owner_e s_owner;
static uint8_t s_submitting, s_generating;

/**
 * @brief 取得提交占用；完整合同见 qwen_request_gate.h。
 */
uint8_t qwen_request_begin(qwen_request_owner_e owner)
{
    uint8_t accepted = 0U;
    if (owner != QWEN_REQUEST_NORMAL && owner != QWEN_REQUEST_TOOL) return 0U;
    spin_lock(&s_request_lock);
    if (s_owner == QWEN_REQUEST_NONE && !s_submitting && !s_generating) {
        s_owner = owner;
        s_submitting = 1U;
        accepted = 1U;
    }
    spin_unlock(&s_request_lock);
    return accepted;
}

/**
 * @brief 归还提交占用并登记受理结果；完整合同见 qwen_request_gate.h。
 */
void qwen_request_submitted(int accepted)
{
    spin_lock(&s_request_lock);
    if (s_submitting) {
        if (!accepted) s_owner = QWEN_REQUEST_NONE;
        s_submitting = 0U;
    }
    spin_unlock(&s_request_lock);
}

/**
 * @brief 取得唯一提取占用；完整合同见 qwen_request_gate.h。
 */
uint8_t qwen_request_generate_begin(void)
{
    uint8_t accepted;
    spin_lock(&s_request_lock);
    accepted = !s_submitting && !s_generating;
    if (accepted) s_generating = 1U;
    spin_unlock(&s_request_lock);
    return accepted;
}

/**
 * @brief 完成分类并归还提取占用；完整合同见 qwen_request_gate.h。
 */
qwen_request_owner_e qwen_request_generate_end(int response_frame, uint8_t empty)
{
    qwen_request_owner_e owner = QWEN_REQUEST_NONE;
    /* SDK 已受理的请求可能排在控制命令后，当前提取为空时仍须保留其所有者，
     * 直到取到响应或旧传输被明确清理。 */
    (void)empty;
    spin_lock(&s_request_lock);
    if (s_generating) {
        if (response_frame < 0) {
            owner = QWEN_REQUEST_BROKEN;
            s_owner = QWEN_REQUEST_NONE;
        } else if (response_frame > 0) {
            owner = s_owner;
            s_owner = QWEN_REQUEST_NONE;
        }
        s_generating = 0U;
    }
    spin_unlock(&s_request_lock);
    return owner;
}

/**
 * @brief 清理已排空传输的请求门禁；完整合同见 qwen_request_gate.h。
 */
void qwen_request_gate_reset(void)
{
    spin_lock(&s_request_lock);
    s_owner = QWEN_REQUEST_NONE;
    s_submitting = s_generating = 0U;
    spin_unlock(&s_request_lock);
}
#endif
