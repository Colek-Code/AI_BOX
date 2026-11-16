/** @file product_ui_wallpapers.h @brief 由 build_product_ui_assets.py 生成的壁纸静态目录。 */
#ifndef PRODUCT_UI_WALLPAPERS_H
#define PRODUCT_UI_WALLPAPERS_H
#include "lvgl.h"
#include <stdint.h>
typedef struct {
    const char *name; /**< 静态显示名称。 */
    const lv_image_dsc_t *image; /**< 借用的静态全尺寸图像。 */
    const lv_image_dsc_t *thumbnail; /**< 借用的静态缩略图。 */
} product_ui_wallpaper_t;
/** @brief 读取壁纸目录数量。 @return 静态目录中的条目数。 */
uint8_t product_ui_wallpaper_count(void);
/**
 * @brief 按目录编号借用壁纸。
 * @param[in] id 小于 product_ui_wallpaper_count() 的目录编号。
 * @return 编号有效时返回静态只读条目，否则返回 NULL；调用方不释放。
 */
const product_ui_wallpaper_t *product_ui_wallpaper_get(uint8_t id);
#endif
