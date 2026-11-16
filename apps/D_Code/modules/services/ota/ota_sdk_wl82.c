/** @file ota_sdk_wl82.c @brief 串行维护被动升级所有权及其异步完成回调。 */
#include "config/app_config.h"
#if D_FEATURE_OTA
#include "ota_port.h"
#include "system/includes.h"
#include "update/dual_bank_updata_api.h"

/* SDK 现有导出，net_update.c 使用它确定首包对齐。 */
extern uint32_t get_target_udate_addr(void);
static struct {
    uint8_t owned;
    uint8_t pending;
    uint8_t in_call;
    d_err_t result;
    d_err_t write_error;
    uint16_t first_size;
} s_sdk;

/**
 * @brief 在 OTA 锁内发布当前命令结果并清除 pending。
 * @param[in] result 本次 VERIFY 或 COMMIT 完成结果。
 */
static void ota_sdk_result(d_err_t result)
{
    ota_port_lock();
    s_sdk.result = result;
    s_sdk.pending = 0U;
    ota_port_unlock();
}
/**
 * @brief 只锁存异步写错误，不完成后续 VERIFY 或 COMMIT。
 * @param[in] error SDK 通过指针值编码的错误，非零表示失败。
 * @return 固定返回 0。
 * @note 部分写入仅缓冲而不触发回调，不能用此回调结算其他命令。
 */
static int ota_written(void *error)
{
    /* 写入可能仅进入缓冲而不产生回调；不能用此回调
     * 完成后续 VERIFY/COMMIT，只锁存写错误。 */
    if ((intptr_t)error != 0) {
        ota_port_lock(); s_sdk.write_error = D_ERR_IO; ota_port_unlock();
    }
    return 0;
}
/**
 * @brief 把 SDK 校验回调转换为 OTA 完成结果。
 * @param[in] passed 仅 1 表示校验通过。
 * @return 固定返回 0。
 */
static int ota_verified(int passed)
{ ota_sdk_result(passed == 1 ? D_OK : D_ERR_IO); return 0; }
/**
 * @brief 把 SDK 启动信息提交回调转换为 OTA 完成结果。
 * @param[in] error 0 表示提交成功，其他值失败。
 * @return 固定返回 0。
 */
static int ota_committed(int error)
{ ota_sdk_result(error == 0 ? D_OK : D_ERR_IO); return 0; }

/**
 * @brief 在调用 SDK 命令前同时标记同步在途与等待回调。
 */
static void ota_sdk_call_begin(void)
{
    ota_port_lock();
    s_sdk.pending = s_sdk.in_call = 1U;
    s_sdk.result = D_ERR_BUSY;
    ota_port_unlock();
}

/**
 * @brief 结束同步调用标记，并等待本命令回调发布结果。
 * @param[in] immediate SDK 提交函数的立即返回值，0 表示未报告立即错误。
 * @return 立即提交失败返回 D_ERR_IO；否则返回最终回调结果。
 * @note SDK 无取消或队列受理机制；缺失结果时保留所有者，独立观察器继续报告期限。
 */
static d_err_t ota_sdk_wait(uint32_t immediate)
{
    d_err_t result;
    ota_port_lock(); s_sdk.in_call = 0U; ota_port_unlock();
    if (immediate != 0U) return D_ERR_IO;
    /* SDK 不提供取消或队列受理保证；结果缺失时保留当前工作任务
     * 与静态缓冲。独立观察器暴露超时，
     * 迟到回调仍可完成同一操作。 */
    for (;;) {
        uint8_t pending;
        ota_port_lock(); pending = s_sdk.pending; result = s_sdk.result; ota_port_unlock();
        if (!pending) return result;
        ota_service_observe_deadline();
        ota_port_wait(10U);
    }
}

/**
 * @brief 独占初始化被动升级上下文，并计算首次写入对齐长度；完整合同见 ota_port.h。
 */
d_err_t ota_sdk_begin(void)
{
    uint32_t result, address, remainder;
    if (s_sdk.owned || get_dual_bank_passive_update_max_buf() != 0U) return D_ERR_BUSY;
    result = dual_bank_passive_update_init(0U, 0U, OTA_PACKET_SIZE, NULL);
    s_sdk.write_error = D_OK;
    s_sdk.owned = get_dual_bank_passive_update_max_buf() != 0U;
    if (result || !s_sdk.owned) {
        /* SDK 部分初始化后仍保留上下文时，清理结果尚不确定。 */
        s_sdk.pending = s_sdk.owned;
        return D_ERR_IO;
    }
    ota_port_lock(); s_sdk.in_call = 1U; ota_port_unlock();
    result = dual_bank_update_allow_check(0U);
    ota_port_lock(); s_sdk.in_call = 0U; ota_port_unlock();
    if (result) return D_ERR_IO;
    address = get_target_udate_addr();
    remainder = address % OTA_PACKET_SIZE;
    s_sdk.first_size = (uint16_t)(remainder ? OTA_PACKET_SIZE - remainder : OTA_PACKET_SIZE);
    return D_OK;
}
/**
 * @brief 读取目标更新地址对齐后的首包长度；完整合同见 ota_port.h。
 */
uint16_t ota_sdk_first_size(void) { return s_sdk.first_size; }

/**
 * @brief 由 OTA 工作任务串行提交一个 SDK 数据包；完整合同见 ota_port.h。
 */
d_err_t ota_sdk_write(const uint8_t *bytes, uint16_t length)
{
    uint32_t immediate;
    d_err_t result;
    if (!s_sdk.owned || !bytes || !length || length > OTA_PACKET_SIZE || ota_sdk_pending())
        return D_ERR_INVALID_ARG;
    ota_port_lock(); s_sdk.in_call = 1U; ota_port_unlock();
    /* 当前固定 SDK 将数据复制到内部 cbuffer；若投递 Flash 写入，
     * 返回前会等待该写入的信号量。仅缓冲的包不产生回调，
     * 本次输入缓冲不逃逸调用。 */
    immediate = dual_bank_update_write((void *)bytes, length, ota_written);
    ota_port_lock();
    s_sdk.in_call = 0U;
    result = immediate ? D_ERR_IO : s_sdk.write_error;
    ota_port_unlock();
    return result;
}
/**
 * @brief 提交被动升级校验并等待同次结果回调；完整合同见 ota_port.h。
 */
d_err_t ota_sdk_verify(void)
{
    d_err_t result;
    if (!s_sdk.owned || ota_sdk_pending()) return D_ERR_NOT_READY;
    ota_sdk_call_begin();
    result = ota_sdk_wait(dual_bank_update_verify(NULL, NULL, ota_verified));
    ota_port_lock();
    if (s_sdk.write_error != D_OK) result = s_sdk.write_error;
    ota_port_unlock();
    return result;
}
/**
 * @brief 提交升级启动信息并等待完成回调；完整合同见 ota_port.h。
 */
d_err_t ota_sdk_commit(void)
{
    if (!s_sdk.owned || ota_sdk_pending()) return D_ERR_NOT_READY;
    ota_sdk_call_begin();
    return ota_sdk_wait(dual_bank_update_burn_boot_info(ota_committed));
}
/**
 * @brief 读取 SDK 命令等待回调或同步调用在途状态；完整合同见 ota_port.h。
 */
uint8_t ota_sdk_pending(void)
{
    uint8_t pending;
    ota_port_lock(); pending = s_sdk.pending || s_sdk.in_call; ota_port_unlock();
    return pending;
}
/**
 * @brief 仅在 SDK 调用及回调均不在途时释放被动升级上下文；完整合同见 ota_port.h。
 */
d_err_t ota_sdk_close(void)
{
    if (!s_sdk.owned) return D_OK;
    if (ota_sdk_pending()) return D_ERR_BUSY;
    /* 仅由 OTA 工作任务调用 exit，不在 dw_update 回调任务中销毁。 */
    if (dual_bank_passive_update_exit(NULL) != 0U || get_dual_bank_passive_update_max_buf() != 0U)
        return D_ERR_BUSY;
    s_sdk.owned = 0U;
    return D_OK;
}
#endif
