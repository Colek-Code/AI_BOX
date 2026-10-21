#ifndef __MAIN_PAGE_ASSETS_H__
#define __MAIN_PAGE_ASSETS_H__

#include "config/app_config.h"
#include "lvgl.h"

LV_IMAGE_DECLARE(main_page_img_wallpaper);
LV_IMAGE_DECLARE(main_page_img_lvgl_logo);

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL)
LV_FONT_DECLARE(cipher_clock_font_54);
#endif

#endif /* __MAIN_PAGE_ASSETS_H__ */
