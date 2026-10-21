/**
 * @file product_ui_music_page.h
 * @brief Private Product UI music page registration interface.
 */

#ifndef __PRODUCT_UI_MUSIC_PAGE_H__
#define __PRODUCT_UI_MUSIC_PAGE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

int product_ui_music_register_page(int page_type);
int product_ui_music_page_create(lv_obj_t *parent);

#endif
#endif /* __PRODUCT_UI_MUSIC_PAGE_H__ */
