/**
 * @file d_sdk_display_config.h
 * @brief JieLi display geometry and screen synchronization configuration.
 */

#ifndef D_SDK_DISPLAY_CONFIG_H
#define D_SDK_DISPLAY_CONFIG_H

#if D_BOARD_IS_GC9307
#define D_LVGL_LOGICAL_LANDSCAPE            1
#define MY_DISP_HOR_RES                     320
#define MY_DISP_VER_RES                     240
#define D_GC9307_PHYS_HOR_RES               240
#define D_GC9307_PHYS_VER_RES               320
#define D_GC9307_ROTATION_DEGREES           90
#define D_GC9307_MADCTL_VALUE               0x48
#define D_GC9307_TE_SCANLINE                10
#define D_GC9307_TE_EDGE_POSITIVE           1
#define D_GC9307_FRAME_RATE_E8_0            0x13
#define D_GC9307_FRAME_RATE_E8_1            0x00
#if (D_GC9307_ROTATION_DEGREES != 90) && (D_GC9307_ROTATION_DEGREES != 270)
#error "GC9307 landscape rotation must be 90 or 270 degrees"
#endif
#if (D_GC9307_TE_EDGE_POSITIVE != 0) && (D_GC9307_TE_EDGE_POSITIVE != 1)
#error "GC9307 TE edge selector must be 0 or 1"
#endif
#elif (D_UI_TARGET == D_UI_LAUNCHER_DEMO)
#define D_LVGL_LOGICAL_LANDSCAPE            1
#define MY_DISP_HOR_RES                     480
#define MY_DISP_VER_RES                     320
#else
#define D_LVGL_LOGICAL_LANDSCAPE            0
#define MY_DISP_HOR_RES                     320
#define MY_DISP_VER_RES                     480
#endif

#if D_BOARD_IS_GC9307
#define USE_LCD_TE                          0
#else
#define USE_LCD_TE                          1
#endif

/* LVGL landscape rotation is performed by the CPU, not LCD MADCTL. */
#define HORIZONTAL_SCREEN                   0

#endif /* D_SDK_DISPLAY_CONFIG_H */
