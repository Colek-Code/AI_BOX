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
 * @brief 相册模块注册
 * @note  由模块系统自动调用
 */
void d_gallery_module_register(void);

#endif /* D_FEATURE_GALLERY */

#endif /* D_GALLERY_PAGE_H */
