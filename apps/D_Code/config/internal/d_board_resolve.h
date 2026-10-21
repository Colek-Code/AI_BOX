/**
 * @file d_board_resolve.h
 * @brief Resolve the selected board into hardware-owned firmware macros.
 */

#ifndef D_BOARD_RESOLVE_H
#define D_BOARD_RESOLVE_H

#include "board/wl82/d_board_memory_config.h"

/* Common display infrastructure used by every supported root UI. */
#define D_LCD_ENABLE
#define D_LVGL_ENABLE

#if D_BOARD_TARGET == D_BOARD_COPY_GC9307_HYN
#define D_BOARD_IS_GC9307                  1
#define D_BOARD_IS_EMI_ILI948X             0
#define D_LCD_GC9307_SDK_LCD_ENABLE
#define D_GC9307_TE_SYNC_ENABLE
#define D_TOUCH_HYN_DRIVER_ENABLE
#define D_TOUCH_HYN_SWAP_XY                1
#define D_TOUCH_HYN_REVERSE_X              1
#define D_TOUCH_HYN_REVERSE_Y              1
#define D_TOUCH_GESTURE_ENABLE
#define D_PWM_ENABLE
#define D_LCD_BACKLIGHT_PWM_ENABLE
#else
#define D_BOARD_IS_GC9307                  0
#define D_BOARD_IS_EMI_ILI948X             1
#define D_LCD_EMI_ILI948X_ENABLE
#define D_LCD_BL_ACTIVE_LOW
#define D_PWM_ENABLE
#define D_LCD_BACKLIGHT_PWM_ENABLE
#endif

/* A visual capture owns the diagnostic UART bandwidth. */
#if D_DEBUG_VISUAL_CAPTURE
#undef D_GC9307_FRAME_TRACE_ENABLE
#undef D_PRODUCT_UI_PAGER_TRACE_ENABLE
#undef D_PRODUCT_UI_INTERACTION_TRACE_ENABLE
#endif

#if defined(D_PWM_ENABLE) || defined(D_LCD_BACKLIGHT_PWM_ENABLE)
#ifndef CONFIG_PWM_ENABLE
#define CONFIG_PWM_ENABLE
#endif
#endif

#endif /* D_BOARD_RESOLVE_H */
