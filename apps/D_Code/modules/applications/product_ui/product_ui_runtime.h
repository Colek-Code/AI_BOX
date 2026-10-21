/**
 * @file product_ui_runtime.h
 * @brief Internal contracts shared by Product UI work packages.
 */

#ifndef PRODUCT_UI_RUNTIME_H
#define PRODUCT_UI_RUNTIME_H

#include "modules/applications/product_ui/product_ui.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

void product_ui_status_snapshot(product_ui_status_t *status);
uint8_t product_ui_status_take_dirty(void);
uint8_t product_ui_module_available(const char *module_id);
void product_ui_process(uint32_t elapsed_ms);

#endif
#endif /* PRODUCT_UI_RUNTIME_H */
