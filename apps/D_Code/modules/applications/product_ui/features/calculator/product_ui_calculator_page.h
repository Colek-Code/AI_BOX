/**
 * @file product_ui_calculator_page.h
 * @brief Private calculator page integration interface.
 */

#ifndef __PRODUCT_UI_CALCULATOR_PAGE_H__
#define __PRODUCT_UI_CALCULATOR_PAGE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

int product_ui_calculator_page_create(lv_obj_t *parent);
int product_ui_calculator_register_page(int page_type);

#endif
#endif /* __PRODUCT_UI_CALCULATOR_PAGE_H__ */
