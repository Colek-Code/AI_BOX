/**
 * @file d_page_transition.h
 * @brief Platform-owned page transition policy boundary.
 */

#ifndef D_PAGE_TRANSITION_H
#define D_PAGE_TRANSITION_H

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include <stdint.h>

typedef enum {
    PAGE_TRANSITION_IMMEDIATE = 0,
    PAGE_TRANSITION_TE_FRAME,
} page_transition_policy_e;

void d_page_transition_begin(page_transition_policy_e policy,
                             uint32_t transition_id);

/* LVGL thread only: coalesce moving-overlay updates into the next complete frame. */
void d_screen_request_composed_frame(void);

#endif /* D_LVGL_ENABLE */
#endif /* D_PAGE_TRANSITION_H */
