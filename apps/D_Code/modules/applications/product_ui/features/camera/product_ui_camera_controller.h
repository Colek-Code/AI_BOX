/**
 * @file product_ui_camera_controller.h
 * @brief 相机页面意图及屏幕占用投影；硬件与后台资源由 camera_preview 拥有。
 * @author D_Code
 * @date 2026-09-11
 */
#ifndef __PRODUCT_UI_CAMERA_CONTROLLER_H__
#define __PRODUCT_UI_CAMERA_CONTROLLER_H__
#include "modules/services/camera/camera_preview.h"

typedef struct {
    camera_preview_snapshot_t snapshot;
    uint32_t session;
    uint32_t shutdown_epoch;
    const char *status;
    const char *button;
    uint8_t is_attached;
    uint8_t has_start_intent;
    uint8_t has_screen_hold;
    uint8_t was_screen_off;
    uint8_t can_retry;
} product_ui_camera_controller_t;

/**
 * @brief 清空控制器并附着页面，根据屏幕及关机门禁形成首次启动意图。
 * @param[out] controller 由页面持有的控制器；NULL 时忽略。
 * @note 随后立即更新；屏幕已关闭或关机阻断时不形成启动意图。
 */
void product_ui_camera_enter(product_ui_camera_controller_t *controller);
/**
 * @brief 观察预览、屏幕及关机代次，推进已有启动意图并协调自动灭屏占用。
 * @param[in,out] controller 已附着且由页面持有的控制器；NULL 或未附着时忽略。
 * @note 仅首次进入或唤醒意图在 BUSY 时保留等待；其他错误不自动重试。灭屏及退出只请求停止，后台资源完成由预览服务确认。
 */
void product_ui_camera_update(product_ui_camera_controller_t *controller);
/**
 * @brief 刷新可重试能力后，为显式用户操作提交一次启动意图。
 * @param[in,out] controller 页面持有的控制器，未附着或不可重试时忽略。
 */
void product_ui_camera_retry(product_ui_camera_controller_t *controller);
/**
 * @brief 解除页面附着，撤销启动意图、请求会话停止并释放本控制器的灭屏占用。
 * @param[in,out] controller 页面持有的控制器；NULL 或已经离开时忽略。
 * @note 不等待预览硬件停止，不释放预览服务拥有的异步资源。
 */
void product_ui_camera_leave(product_ui_camera_controller_t *controller);

#endif
