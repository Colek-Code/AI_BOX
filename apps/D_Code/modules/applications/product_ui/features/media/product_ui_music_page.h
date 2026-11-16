/** @file product_ui_music_page.h @brief 十项歌曲窗口及播放状态投影，页面生命周期不拥有播放资源。 */

#ifndef __PRODUCT_UI_MUSIC_PAGE_H__
#define __PRODUCT_UI_MUSIC_PAGE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

/**
 * @brief 将静态音乐页面描述注册到页面管理器。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
int product_ui_music_register_page(int page_type);
/**
 * @brief 创建音乐框架及已启用的播放器内容，功能关闭时创建不可用反馈。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 成功返回 0；框架、内容或反馈创建失败返回 -1，部分对象由页面管理器清理。
 */
int product_ui_music_page_create(lv_obj_t *parent);

#endif
#endif /* __PRODUCT_UI_MUSIC_PAGE_H__ */
