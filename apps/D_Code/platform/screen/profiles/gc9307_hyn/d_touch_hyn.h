/**
 * @file d_touch_hyn.h
 * @brief HYN/CST8xxT touch backend for the GC9307 screen path.
 */

#ifndef D_TOUCH_HYN_H
#define D_TOUCH_HYN_H

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_TOUCH_HYN_DRIVER_ENABLE)

#include "framework/d_error.h"
#include "platform/screen/lcd/d_lcd_compat.h"

typedef struct {
    u8 id;
    u8 z;
    u16 x;
    u16 y;
} d_touch_hyn_point_t;

d_err_t d_touch_hyn_init(const struct lcd_platform_data *pdata);
/* Native post-mount coordinates: 240x320. LVGL applies display rotation separately. */
void d_touch_hyn_get_state(s16 *x, s16 *y, unsigned char *status);

#endif /* D_LCD_ENABLE && D_TOUCH_HYN_DRIVER_ENABLE */
#endif /* D_TOUCH_HYN_H */
