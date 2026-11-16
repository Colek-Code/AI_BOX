/**
 * @file product_ui_clock_model.h
 * @brief 仅整数运算的软件墙钟模型，所有操作须由调用方串行保护。
 * @author D_Code
 * @date 2026-09-21
 */
#ifndef PRODUCT_UI_CLOCK_MODEL_H
#define PRODUCT_UI_CLOCK_MODEL_H
#include "product_ui_time_domain.h"

#define PRODUCT_UI_CLOCK_MIN_YEAR 2024U
#define PRODUCT_UI_CLOCK_MAX_YEAR 2099U
#define PRODUCT_UI_CLOCK_MIN_UTC 1704038400LL
#define PRODUCT_UI_CLOCK_END_UTC 4102415999LL

typedef enum {
    PRODUCT_UI_CLOCK_NONE = 0,
    PRODUCT_UI_CLOCK_NETWORK,
    PRODUCT_UI_CLOCK_MANUAL,
} product_ui_clock_source_e;

typedef struct {
    int64_t utc_seconds;
    uint64_t monotonic_ms;
    uint32_t revision;
    uint32_t schedule_epoch;
    product_ui_clock_source_e source;
    uint8_t valid;
    uint8_t automatic;
} product_ui_clock_snapshot_t;

typedef struct {
    product_ui_clock_snapshot_t state;
    int64_t utc_ms;
    uint32_t last_ms;
} product_ui_clock_model_t;

/**
 * @brief 初始化尚未可信且默认自动模式的模型，建立非零内容与调度代次。
 * @param[out] model 调用方持有的模型，NULL 时忽略。
 * @param[in] now_ms 当前回绕单调毫秒计数。
 */
void product_ui_clock_model_init(product_ui_clock_model_t *model, uint32_t now_ms);
/**
 * @brief 用回绕毫秒差推进扩展单调时间及有效墙钟。
 * @param[in,out] model 串行保护的模型，NULL 时忽略。
 * @param[in] now_ms 定期采样的单调计数。
 * @note 差值达到半个 uint32_t 周期视为旧采样而忽略；须规律调用，不能跨过整个计数周期而不采样。墙钟超出产品上界时撤销有效性。
 */
void product_ui_clock_model_tick(product_ui_clock_model_t *model, uint32_t now_ms);
/**
 * @brief 校验手动北京时间并切换为有效手动来源。
 * @param[in,out] model 调用方串行保护的模型。
 * @param[in] value 借用的北京时间值，年份在 2024 至 2099。
 * @return 成功返回 TIME_OK；空参数、年份越界或转换失败返回 INVALID_ARGUMENT。
 * @note 成功同时推进墙钟代次与调度代次；不改 SDK 或 RTC。
 */
product_ui_time_result_e product_ui_clock_model_manual(
    product_ui_clock_model_t *model, const product_ui_datetime_t *value);
/**
 * @brief 在自动模式中接纳受 SDK 有符号 32 位边界限制的 UTC 样本。
 * @param[in,out] model 调用方串行保护的模型。
 * @param[in] utc_seconds 不早于产品最小时刻且不超过 INT32_MAX 的 UTC 秒数。
 * @return 成功或同来源一秒以内抖动返回 TIME_OK；非法输入返回 INVALID_ARGUMENT，手动模式返回 CONFLICT。
 * @note 已有网络时钟的一秒内样本不重置小数毫秒，避免整秒采样反复丢失分数时间。
 */
product_ui_time_result_e product_ui_clock_model_network(product_ui_clock_model_t *model, int64_t utc_seconds);
/**
 * @brief 从手动切回自动模式并清除旧来源的可信性，等待新样本。
 * @param[in,out] model 调用方串行保护的模型，NULL 或已经自动时忽略。
 */
void product_ui_clock_model_automatic(product_ui_clock_model_t *model);
/**
 * @brief 复制模型当前值快照。
 * @param[in] model 串行保护的借用模型。
 * @param[out] snapshot 接收值副本；任一指针为空时不操作。
 */
void product_ui_clock_model_snapshot(const product_ui_clock_model_t *model, product_ui_clock_snapshot_t *snapshot);
#endif
