/**
 * @file lv_port_disp_gc9307.h
 * @brief GC9307-specific LVGL display-port controls.
 * D++: Owned by the D_Code GC9307/HYN screen profile.
 */

#ifndef __LV_PORT_DISP_GC9307_H__
#define __LV_PORT_DISP_GC9307_H__

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

/* D++: Collect one page transition and send it as one TE-aligned physical frame. */
void lv_port_disp_gc9307_begin_full_frame_transition(
    unsigned int transition_id);

/* LVGL thread only; latest overlay position wins before the next refresh. */
void lv_port_disp_gc9307_request_composed_frame(void);

#if D_CAMERA_PREVIEW_ENABLE
#include "lvgl.h"
/* LVGL thread only. Arm before invalidating the complete image; zero means unsupported. */
unsigned int lv_port_disp_gc9307_begin_region_update(const lv_area_t *area);
/* Cancels only the supplied request; an already running physical transfer finishes normally. */
void lv_port_disp_gc9307_cancel_region_update(unsigned int request_id);
#endif

#endif
#endif /* __LV_PORT_DISP_GC9307_H__ */
