/** @file product_ui_gallery_page.h @brief 图库的有限媒体列表投影及可重试页面生命周期。 */

#ifndef __PRODUCT_UI_GALLERY_PAGE_H__
#define __PRODUCT_UI_GALLERY_PAGE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

/**
 * @brief 将静态图库描述注册到页面管理器。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
int product_ui_gallery_register_page(int page_type);
/**
 * @brief 创建图库页面框架并根据真实存储投影呈现首屏。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 成功返回 0，框架或内容创建失败返回 -1；部分对象由页面管理器清理。
 */
int product_ui_gallery_page_create(lv_obj_t *parent);

#endif
#endif /* __PRODUCT_UI_GALLERY_PAGE_H__ */
