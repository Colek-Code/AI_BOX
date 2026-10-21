/**
 * @file product_ui_clock_model.c
 * @brief Integer-only trusted time with explicit manual scheduling epochs.
 * @author D_Code
 * @date 2026-09-21
 */
#include "product_ui_clock_model.h"
#include "product_ui_time_domain_internal.h"
#include <string.h>

static void product_ui_clock_revision(uint32_t *revision)
{
    if (++*revision == 0U) ++*revision;
}

void product_ui_clock_model_init(product_ui_clock_model_t *model, uint32_t now_ms)
{
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    model->last_ms = now_ms;
    model->state.automatic = 1U;
    model->state.revision = 1U;
    model->state.schedule_epoch = 1U;
}

void product_ui_clock_model_tick(product_ui_clock_model_t *model, uint32_t now_ms)
{
    uint32_t elapsed;
    if (model == NULL) return;
    elapsed = (uint32_t)(now_ms - model->last_ms);
    /* Concurrent readers may have sampled the counter just before another reader. */
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

product_ui_time_result_e product_ui_clock_model_network(product_ui_clock_model_t *model, int64_t utc_seconds)
{
    int64_t difference;
    if (model == NULL || utc_seconds < PRODUCT_UI_CLOCK_MIN_UTC || utc_seconds > INT32_MAX)
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    if (!model->state.automatic) return PRODUCT_UI_TIME_ERR_CONFLICT;
    difference = utc_seconds - model->state.utc_seconds;
    /* Normal whole-second SDK sampling must not repeatedly reset fractional time. */
    if (model->state.valid && model->state.source == PRODUCT_UI_CLOCK_NETWORK &&
        difference >= -1 && difference <= 1) return PRODUCT_UI_TIME_OK;
    model->utc_ms = utc_seconds * 1000LL;
    model->state.utc_seconds = utc_seconds;
    model->state.valid = 1U;
    model->state.source = PRODUCT_UI_CLOCK_NETWORK;
    product_ui_clock_revision(&model->state.revision);
    return PRODUCT_UI_TIME_OK;
}

void product_ui_clock_model_automatic(product_ui_clock_model_t *model)
{
    if (model == NULL || model->state.automatic) return;
    model->state.automatic = 1U;
    model->state.valid = 0U;
    model->state.source = PRODUCT_UI_CLOCK_NONE;
    product_ui_clock_revision(&model->state.revision);
}

void product_ui_clock_model_snapshot(const product_ui_clock_model_t *model, product_ui_clock_snapshot_t *snapshot)
{
    if (model != NULL && snapshot != NULL) *snapshot = model->state;
}
