/** @file product_ui_clock.h @brief Trusted local wall-clock source shared by UI and background services. */
#ifndef PRODUCT_UI_CLOCK_H
#define PRODUCT_UI_CLOCK_H
#include "product_ui_clock_model.h"
#include "framework/d_error.h"
/** Process lifetime owner; optional failure is reported without stopping the desktop. */
d_err_t product_ui_clock_init(void);
void product_ui_clock_get_snapshot(product_ui_clock_snapshot_t *snapshot);
d_err_t product_ui_clock_set_manual(const product_ui_datetime_t *value, uint32_t expected_revision);
d_err_t product_ui_clock_use_automatic(uint32_t expected_revision);
void product_ui_clock_stop(void);
/** Freeze corrections (not elapsed time) while reminder persistence drains. */
void product_ui_clock_quiesce_begin(void);
void product_ui_clock_quiesce_cancel(void);
/** No RTC fallback. Returns availability and writes only a validated Beijing time. */
product_ui_time_unavailable_reason_e product_ui_clock_read(product_ui_datetime_t *value);
#endif
