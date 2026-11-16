/**
 * @file d_gallery_page.h
 * @brief 相册模块 - 公共接口
 * @note  预览SD卡中的照片，左右滑动切换，上滑返回
 */

#ifndef D_GALLERY_PAGE_H
#define D_GALLERY_PAGE_H

#include "config/app_config.h"

#if D_FEATURE_GALLERY

/**
 * @brief 保留的历史相册注册声明。
 * @note 当前自研树未提供此符号实现或调用；真实装配使用 d_gallery_page.c 的 D_UI_MODULE_DEFINE 描述符，不能把本声明当作可调用入口。
 */
void d_gallery_module_register(void);

#endif /* D_FEATURE_GALLERY */

#endif /* D_GALLERY_PAGE_H */
