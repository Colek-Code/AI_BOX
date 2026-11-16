#ifndef __MAIN_PAGE_ASSETS_H__
#define __MAIN_PAGE_ASSETS_H__

#include "config/app_config.h"
#include "lvgl.h"

/** @brief 旧 Cipher 首页的静态只读壁纸描述符及其像素，由 LVGL 借用。 */
LV_IMAGE_DECLARE(main_page_img_wallpaper);
/** @brief 静态只读 LVGL 标志图像描述符，调用方不得释放数据。 */
LV_IMAGE_DECLARE(main_page_img_lvgl_logo);

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL)
/** @brief Orbitron 来源的五十四像素时钟字体；保留生成字模与原始授权说明。 */
LV_FONT_DECLARE(cipher_clock_font_54);
#endif

#endif /* __MAIN_PAGE_ASSETS_H__ */
