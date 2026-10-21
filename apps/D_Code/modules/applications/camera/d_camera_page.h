/**
 * @file d_camera_page.h
 * @brief 相机模块 - 公共接口
 * @note  模块化设计，通过 D_FEATURE_CAMERA 宏控制开关
 *        全屏摄像头预览 + LVGL UI 叠加
 */

#ifndef __D_CAMERA_PAGE_H__
#define __D_CAMERA_PAGE_H__

#include "config/app_config.h"

#if D_FEATURE_CAMERA

/**
 * @brief 注册相机模块
 * @note  由模块系统自动调用
 */
void d_camera_module_register(void);

/**
 * @brief 检查相机是否已初始化
 * @return 1 已初始化, 0 未初始化
 */
int camera_page_is_initialized(void);

#endif /* D_FEATURE_CAMERA */
#endif /* __D_CAMERA_PAGE_H__ */
