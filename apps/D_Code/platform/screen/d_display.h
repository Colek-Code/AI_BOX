/**
 * @file    d_display.h
 * @brief   当前屏幕 Profile 提供的显示接口。
 */

#ifndef D_DISPLAY_H
#define D_DISPLAY_H

#include "framework/d_error.h"
#include <stdint.h>

d_err_t d_display_init(void);
uint16_t d_display_get_width(void);
uint16_t d_display_get_height(void);
d_err_t d_display_set_brightness(uint8_t percent);
uint8_t d_display_get_brightness(void);
d_err_t d_display_set_backlight(uint8_t on);

#endif /* D_DISPLAY_H */
