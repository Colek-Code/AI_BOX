/**
 * @file d_ui_startup.h
 * @brief Selected root UI, page-module registration and periodic update.
 */

#ifndef D_UI_STARTUP_H
#define D_UI_STARTUP_H

#include "config/app_config.h"
#include "framework/d_error.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

const char *d_ui_startup_name(void);
d_err_t d_ui_startup_run(void);
const lv_font_t *d_ui_startup_font_fallback_tail(void);
void d_ui_startup_update(uint32_t elapsed_ms);

#endif /* D_LVGL_ENABLE */
#endif /* D_UI_STARTUP_H */
