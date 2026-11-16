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
 * @brief 保留的历史相机注册声明。
 * @note 当前自研树未提供此符号的实现或消费者，不能据此调用；实际模块装配使用 d_camera_page.c 的 D_UI_MODULE_DEFINE 描述符。
 */
void d_camera_module_register(void);

/**
 * @brief 读取旧相机页的本地初始化标志。
 * @return 调用过初始化并完成固定等待后为 1，退出并调用 uninit 后为 0。
 * @note 该标志不来自传感器／SDK 成功完成确认，不能单独证明相机硬件已就绪。
 */
int camera_page_is_initialized(void);

#endif /* D_FEATURE_CAMERA */
#endif /* __D_CAMERA_PAGE_H__ */
