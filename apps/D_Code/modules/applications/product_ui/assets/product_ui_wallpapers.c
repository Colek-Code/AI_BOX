/** @file product_ui_wallpapers.c @brief 由 build_product_ui_assets.py 生成的壁纸像素及目录。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/product_ui/assets/product_ui_wallpapers.h"

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint16_t s_wallpaper_deep_sea_main_pixels[] = {
#include "modules/applications/product_ui/assets/generated/product_ui_wallpaper_normal.inc"
};
static const lv_image_dsc_t s_wallpaper_deep_sea_main = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 320, .header.h = 240, .header.stride = 640,
    .data_size = sizeof(s_wallpaper_deep_sea_main_pixels), .data = (const uint8_t *)s_wallpaper_deep_sea_main_pixels,
};

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint16_t s_wallpaper_deep_sea_thumbnail_pixels[] = {
#include "modules/applications/product_ui/assets/generated/product_ui_wallpaper_deep_sea_thumb.inc"
};
static const lv_image_dsc_t s_wallpaper_deep_sea_thumbnail = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 80, .header.h = 60, .header.stride = 160,
    .data_size = sizeof(s_wallpaper_deep_sea_thumbnail_pixels), .data = (const uint8_t *)s_wallpaper_deep_sea_thumbnail_pixels,
};

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint16_t s_wallpaper_aurora_main_pixels[] = {
#include "modules/applications/product_ui/assets/generated/product_ui_wallpaper_aurora.inc"
};
static const lv_image_dsc_t s_wallpaper_aurora_main = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 320, .header.h = 240, .header.stride = 640,
    .data_size = sizeof(s_wallpaper_aurora_main_pixels), .data = (const uint8_t *)s_wallpaper_aurora_main_pixels,
};

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint16_t s_wallpaper_aurora_thumbnail_pixels[] = {
#include "modules/applications/product_ui/assets/generated/product_ui_wallpaper_aurora_thumb.inc"
};
static const lv_image_dsc_t s_wallpaper_aurora_thumbnail = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 80, .header.h = 60, .header.stride = 160,
    .data_size = sizeof(s_wallpaper_aurora_thumbnail_pixels), .data = (const uint8_t *)s_wallpaper_aurora_thumbnail_pixels,
};

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint16_t s_wallpaper_twilight_main_pixels[] = {
#include "modules/applications/product_ui/assets/generated/product_ui_wallpaper_twilight.inc"
};
static const lv_image_dsc_t s_wallpaper_twilight_main = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 320, .header.h = 240, .header.stride = 640,
    .data_size = sizeof(s_wallpaper_twilight_main_pixels), .data = (const uint8_t *)s_wallpaper_twilight_main_pixels,
};

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint16_t s_wallpaper_twilight_thumbnail_pixels[] = {
#include "modules/applications/product_ui/assets/generated/product_ui_wallpaper_twilight_thumb.inc"
};
static const lv_image_dsc_t s_wallpaper_twilight_thumbnail = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 80, .header.h = 60, .header.stride = 160,
    .data_size = sizeof(s_wallpaper_twilight_thumbnail_pixels), .data = (const uint8_t *)s_wallpaper_twilight_thumbnail_pixels,
};

static const product_ui_wallpaper_t s_wallpapers[] = {
    {"深海蓝", &s_wallpaper_deep_sea_main, &s_wallpaper_deep_sea_thumbnail},
    {"极光青", &s_wallpaper_aurora_main, &s_wallpaper_aurora_thumbnail},
    {"暮光紫", &s_wallpaper_twilight_main, &s_wallpaper_twilight_thumbnail},
};

/** @brief 读取静态壁纸目录数量。 */
uint8_t product_ui_wallpaper_count(void)
{
    return (uint8_t)(sizeof(s_wallpapers) / sizeof(s_wallpapers[0]));
}

/** @brief 按编号借用静态目录条目，越界返回 NULL。 */
const product_ui_wallpaper_t *product_ui_wallpaper_get(uint8_t id)
{
    return id < product_ui_wallpaper_count() ? &s_wallpapers[id] : NULL;
}
#endif
