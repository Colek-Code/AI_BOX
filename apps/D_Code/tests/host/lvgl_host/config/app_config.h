/** @file app_config.h @brief 主机 Product UI 功能选择及生产 runtime_config 的兼容入口，不代表真实板级配置。 */
#ifndef PRODUCT_UI_HOST_APP_CONFIG_H
#define PRODUCT_UI_HOST_APP_CONFIG_H
#define D_LVGL_ENABLE 1
#define D_UI_PRODUCT 3
#define D_UI_TARGET D_UI_PRODUCT
#ifndef D_SCREEN_DEFAULT_ROTATION_DEGREES
#define D_SCREEN_DEFAULT_ROTATION_DEGREES 0
#endif
#ifdef PRODUCT_UI_ALARM_HOST_SDK_VM
#define VM_ITEM_MAX_NUM 256
#endif
#ifndef D_FEATURE_WIFI
#define D_FEATURE_WIFI 0
#endif
#ifndef D_FEATURE_QWEN
#define D_FEATURE_QWEN 1
#endif
#ifndef D_CAMERA_PREVIEW_ENABLE
#define D_CAMERA_PREVIEW_ENABLE 0
#endif
#define D_PRODUCT_UI_TRACE_ENABLE 0
#include "config/runtime_config.h"
#endif
