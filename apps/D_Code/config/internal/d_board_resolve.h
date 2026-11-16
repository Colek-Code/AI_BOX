/**
 * @file d_board_resolve.h
 * @brief 将板型选择解析为硬件所属的固件宏，不承载应用功能策略。
 */

#ifndef D_BOARD_RESOLVE_H
#define D_BOARD_RESOLVE_H

#include "board/wl82/d_board_memory_config.h"

/* 所有受支持的根界面共用的显示基础能力。 */
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

/* 显式启用的 Product UI 配置复用显示模块已有的有界时序。 */
#if D_BOARD_IS_GC9307 && D_DEBUG_UI_PERFORMANCE
#define D_GC9307_FRAME_TRACE_ENABLE 1
#endif

/* 画面采集独占诊断 UART 带宽。 */
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
