/**
 * @file product_ui_clock_model.h
 * @brief Pure product wall clock; manual input never changes SDK/RTC time.
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

/** Initialize an untrusted clock; caller serializes all model operations. */
void product_ui_clock_model_init(product_ui_clock_model_t *model, uint32_t now_ms);
/** Sample regularly, including without UI activity, to extend the uint32_t counter. */
void product_ui_clock_model_tick(product_ui_clock_model_t *model, uint32_t now_ms);
product_ui_time_result_e product_ui_clock_model_manual(
    product_ui_clock_model_t *model, const product_ui_datetime_t *value);
/** Network samples retain the signed-32-bit SDK input boundary. */
product_ui_time_result_e product_ui_clock_model_network(product_ui_clock_model_t *model, int64_t utc_seconds);
void product_ui_clock_model_automatic(product_ui_clock_model_t *model);
void product_ui_clock_model_snapshot(const product_ui_clock_model_t *model, product_ui_clock_snapshot_t *snapshot);
#endif
