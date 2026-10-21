/**
 * @file product_ui_gc9307_adapter.h
 * @brief Product UI adapters backed by the GC9307 board profile.
 */

#ifndef PRODUCT_UI_GC9307_ADAPTER_H
#define PRODUCT_UI_GC9307_ADAPTER_H

#include "config/app_config.h"
#include <stdint.h>

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && \
    (D_BOARD_IS_GC9307)

int product_ui_status_gc9307_init(void);
void product_ui_status_gc9307_process(uint32_t elapsed_ms);
int product_ui_settings_gc9307_init(void);
void product_ui_settings_gc9307_process(void);

#if D_FEATURE_WIFI
int product_ui_connection_gc9307_init(void);
#endif

#endif

#endif /* PRODUCT_UI_GC9307_ADAPTER_H */
