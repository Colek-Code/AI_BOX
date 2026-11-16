/**
 * @file product_ui_clock_model.c
 * @brief 仅整数运算的软件墙钟模型，所有操作须由调用方串行保护。
 * @author D_Code
 * @date 2026-09-21
 */
#include "product_ui_clock_model.h"
#include "product_ui_time_domain_internal.h"
#include <string.h>

/**
 * @brief 推进非零内容或调度代次，回绕时跳过零。
 * @param[in,out] revision 调用链保证非空的代次字段。
 */
static void product_ui_clock_revision(uint32_t *revision)
{
    if (++*revision == 0U) ++*revision;
}

/**
 * @brief 初始化尚未可信且默认自动模式的模型，建立非零内容与调度代次。
 */
void product_ui_clock_model_init(product_ui_clock_model_t *model, uint32_t now_ms)
{
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    model->last_ms = now_ms;
    model->state.automatic = 1U;
    model->state.revision = 1U;
    model->state.schedule_epoch = 1U;
}

/**
 * @brief 用回绕毫秒差推进扩展单调时间及有效墙钟。
 */
void product_ui_clock_model_tick(product_ui_clock_model_t *model, uint32_t now_ms)
{
    uint32_t elapsed;
    if (model == NULL) return;
    elapsed = (uint32_t)(now_ms - model->last_ms);
    /* 并发读取方可能刚在另一个读取方之前采样，半周期判断用来拒绝此旧计数。 */
    if (elapsed >= 0x80000000U) return;
    model->last_ms = now_ms;
    model->state.monotonic_ms += elapsed;
    if (!model->state.valid) return;
    model->utc_ms += elapsed;
    model->state.utc_seconds = model->utc_ms / 1000LL;
    if (model->state.utc_seconds > PRODUCT_UI_CLOCK_END_UTC) {
        model->state.valid = 0U;
        model->state.source = PRODUCT_UI_CLOCK_NONE;
        product_ui_clock_revision(&model->state.revision);
    }
}

/**
 * @brief 校验手动北京时间并切换为有效手动来源。
 */
product_ui_time_result_e product_ui_clock_model_manual(
    product_ui_clock_model_t *model, const product_ui_datetime_t *value)
{
    int64_t seconds;
    if (model == NULL || value == NULL || value->date.year < PRODUCT_UI_CLOCK_MIN_YEAR ||
        value->date.year > PRODUCT_UI_CLOCK_MAX_YEAR ||
        product_ui_time_beijing_to_utc(value, &seconds) != PRODUCT_UI_TIME_OK)
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    model->utc_ms = seconds * 1000LL;
    model->state.utc_seconds = seconds;
    model->state.valid = 1U;
    model->state.automatic = 0U;
    model->state.source = PRODUCT_UI_CLOCK_MANUAL;
    product_ui_clock_revision(&model->state.revision);
    product_ui_clock_revision(&model->state.schedule_epoch);
    return PRODUCT_UI_TIME_OK;
}

/**
 * @brief 在自动模式中接纳受 SDK 有符号 32 位边界限制的 UTC 样本。
 */
product_ui_time_result_e product_ui_clock_model_network(product_ui_clock_model_t *model, int64_t utc_seconds)
{
    int64_t difference;
    if (model == NULL || utc_seconds < PRODUCT_UI_CLOCK_MIN_UTC || utc_seconds > INT32_MAX)
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    if (!model->state.automatic) return PRODUCT_UI_TIME_ERR_CONFLICT;
    difference = utc_seconds - model->state.utc_seconds;
    /* 普通 SDK 整秒采样不能反复重置已经累计的小数毫秒。 */
    if (model->state.valid && model->state.source == PRODUCT_UI_CLOCK_NETWORK &&
        difference >= -1 && difference <= 1) return PRODUCT_UI_TIME_OK;
    model->utc_ms = utc_seconds * 1000LL;
    model->state.utc_seconds = utc_seconds;
    model->state.valid = 1U;
    model->state.source = PRODUCT_UI_CLOCK_NETWORK;
    product_ui_clock_revision(&model->state.revision);
    return PRODUCT_UI_TIME_OK;
}

/**
 * @brief 从手动切回自动模式并清除旧来源的可信性，等待新样本。
 */
void product_ui_clock_model_automatic(product_ui_clock_model_t *model)
{
    if (model == NULL || model->state.automatic) return;
    model->state.automatic = 1U;
    model->state.valid = 0U;
    model->state.source = PRODUCT_UI_CLOCK_NONE;
    product_ui_clock_revision(&model->state.revision);
}

/**
 * @brief 复制模型当前值快照。
 */
void product_ui_clock_model_snapshot(const product_ui_clock_model_t *model, product_ui_clock_snapshot_t *snapshot)
{
    if (model != NULL && snapshot != NULL) *snapshot = model->state;
}
