/** @file product_ui_pomodoro_page.h @brief Product Pomodoro page and notification entry. */
#ifndef PRODUCT_UI_POMODORO_PAGE_H
#define PRODUCT_UI_POMODORO_PAGE_H
#include "modules/applications/product_ui/product_ui.h"
int product_ui_pomodoro_page_create(lv_obj_t *parent);
int product_ui_pomodoro_register_page(int page_type);
/** UI-thread request; switches internal view in the page processing phase. */
void product_ui_pomodoro_page_show_timer(void);
#endif
