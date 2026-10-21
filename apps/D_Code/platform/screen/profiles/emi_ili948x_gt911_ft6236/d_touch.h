/**
 * @file d_touch.h
 * @brief D_Code touch driver public interface.
 */

#ifndef D_TOUCH_H
#define D_TOUCH_H

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)

#include "framework/d_error.h"
#include "platform/screen/lcd/d_lcd_compat.h"

d_err_t d_touch_init(const struct lcd_platform_data *pdata);
void get_touch_x_y_status(s16 *x, s16 *y, unsigned char *status);

#endif /* D_LCD_ENABLE && D_LCD_EMI_ILI948X_ENABLE */
#endif /* D_TOUCH_H */
