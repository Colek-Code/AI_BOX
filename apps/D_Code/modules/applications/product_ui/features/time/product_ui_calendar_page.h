/**
 * @file product_ui_calendar_page.h
 * @brief Private Product UI calendar page interface.
 */

#ifndef PRODUCT_UI_CALENDAR_PAGE_H
#define PRODUCT_UI_CALENDAR_PAGE_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

int product_ui_calendar_register_page(int page_type);
int product_ui_calendar_page_create(lv_obj_t *parent);

#endif
#endif /* PRODUCT_UI_CALENDAR_PAGE_H */
