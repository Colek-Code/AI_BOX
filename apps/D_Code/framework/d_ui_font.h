/**
 * @file d_ui_font.h
 * @brief Single owner for the default LVGL fallback font chain.
 */

#ifndef D_UI_FONT_H
#define D_UI_FONT_H

#include "config/app_config.h"
#include "d_error.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

/**
 * @brief Build Montserrat -> CJK -> emoji -> optional tail in static storage.
 * @param fallback_tail Static font descriptor, or NULL.
 */
d_err_t d_ui_font_init(const lv_font_t *fallback_tail);

/** @brief Return the project default font; valid before and after init. */
const lv_font_t *d_ui_font_get_default(void);

#endif /* D_LVGL_ENABLE */
#endif /* D_UI_FONT_H */
