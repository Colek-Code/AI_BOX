/**
 * @file d_page_transition.c
 * @brief Board-profile implementation of the page transition boundary.
 */

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "platform/screen/transition/d_page_transition.h"

#if defined(D_LCD_ENABLE) && D_BOARD_IS_GC9307
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307.h"
#endif

void d_page_transition_begin(page_transition_policy_e policy,
                             uint32_t transition_id)
{
#if defined(D_LCD_ENABLE) && D_BOARD_IS_GC9307
    if (policy == PAGE_TRANSITION_TE_FRAME) {
        lv_port_disp_gc9307_begin_full_frame_transition(transition_id);
    }
#else
    (void)policy;
    (void)transition_id;
#endif
}

void d_screen_request_composed_frame(void)
{
#if defined(D_LCD_ENABLE) && D_BOARD_IS_GC9307 && (D_UI_TARGET == D_UI_PRODUCT)
    lv_port_disp_gc9307_request_composed_frame();
#endif
}

#endif /* D_LVGL_ENABLE */
