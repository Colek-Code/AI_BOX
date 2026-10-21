/**
 * @file d_page_manager.h
 * @brief LVGL page registry, lifecycle and navigation interface.
 */

#ifndef D_PAGE_MANAGER_H
#define D_PAGE_MANAGER_H

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "platform/screen/transition/d_page_transition.h"
#include "d_page_types.h"

void page_manager_init(void);
int page_manager_register(const page_desc_t *desc);
int page_manager_switch_to(int page_type, page_transition_policy_e policy);
void page_manager_back_to_main(void);
int page_manager_get_current(void);
/* Borrowed on the LVGL thread; never retain this pointer across navigation. */
lv_obj_t *page_manager_get_current_container(void);
void page_manager_update(uint32_t elapsed_ms);
void page_manager_handle_gesture(int gesture_type);

#endif /* D_LVGL_ENABLE */
#endif /* D_PAGE_MANAGER_H */
