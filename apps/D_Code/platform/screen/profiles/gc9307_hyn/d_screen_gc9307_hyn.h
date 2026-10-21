/**
 * @file d_screen_gc9307_hyn.h
 * @brief GC9307 + HYN/CST8xxT screen profile backend.
 */

#ifndef D_SCREEN_GC9307_HYN_H
#define D_SCREEN_GC9307_HYN_H

#include <stdint.h>

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#include "framework/d_error.h"
#include "platform/screen/lcd/d_lcd_compat.h"

d_err_t d_screen_gc9307_hyn_init(const struct lcd_platform_data *pdata,
                                 uint16_t *width, uint16_t *height);
void d_screen_gc9307_hyn_idle(void);

/* D++: Shared profile transform keeps touch events and LVGL page gestures in one logical coordinate space. */
void d_screen_gc9307_hyn_native_to_logical(int32_t native_x, int32_t native_y,
                                            int32_t *logical_x, int32_t *logical_y);
void d_screen_gc9307_hyn_delta_to_logical(int32_t native_dx, int32_t native_dy,
                                           int32_t *logical_dx, int32_t *logical_dy);

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
#endif /* D_SCREEN_GC9307_HYN_H */
