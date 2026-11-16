/**
 * @file product_ui_camera_controller.c
 * @brief 相机页面意图及屏幕占用投影；硬件与后台资源由 camera_preview 拥有。
 * @author D_Code
 * @date 2026-09-11
 */
#include "product_ui_camera_controller.h"
#include "platform/screen/power/d_screen_power.h"
#include <string.h>

/**
 * @brief 把预览阶段和错误投影为静态状态文字、按钮文字及重试能力。
 * @param[in,out] controller 已有最新预览快照的控制器。
 */
static void product_ui_camera_describe(product_ui_camera_controller_t *controller)
{
    const camera_preview_snapshot_t *snapshot = &controller->snapshot;
    controller->can_retry = snapshot->can_start && !controller->was_screen_off;
    controller->button = snapshot->phase == CAMERA_PREVIEW_ERROR ? "重试" : "继续预览";
    switch (snapshot->phase) {
    case CAMERA_PREVIEW_UNAVAILABLE:
        controller->status = "相机服务不可用";
        break;
    case CAMERA_PREVIEW_IDLE:
        controller->status = "预览已停止";
        break;
    case CAMERA_PREVIEW_STARTING:
        controller->status = "正在打开相机";
        break;
    case CAMERA_PREVIEW_RUNNING:
        controller->status = "实时预览";
        break;
    case CAMERA_PREVIEW_STOPPING:
        controller->status = snapshot->is_stop_overdue ? "停止超时，仍在等待" : "正在停止相机";
        break;
    case CAMERA_PREVIEW_ERROR:
        if (snapshot->error == CAMERA_PREVIEW_ERROR_NO_SENSOR) {
            controller->status = "未检测到摄像头";
        } else if (snapshot->error == CAMERA_PREVIEW_ERROR_START_TIMEOUT) {
            controller->status = "相机启动超时";
        } else if (snapshot->error == CAMERA_PREVIEW_ERROR_FRAME_TIMEOUT) {
            controller->status = "预览画面中断";
        } else {
            controller->status = "预览失败，请重试";
        }
        break;
    case CAMERA_PREVIEW_FAULT:
        controller->status = "相机异常，请重启设备";
        break;
    }
}

/**
 * @brief 清空控制器并附着页面，根据屏幕及关机门禁形成首次启动意图。
 */
void product_ui_camera_enter(product_ui_camera_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    camera_preview_get_snapshot(&controller->snapshot);
    controller->shutdown_epoch = controller->snapshot.shutdown_epoch;
    controller->is_attached = 1U;
    controller->was_screen_off = d_screen_power_is_off();
    controller->has_start_intent = !controller->was_screen_off && !controller->snapshot.is_shutdown_blocked;
    product_ui_camera_update(controller);
}

/**
 * @brief 观察预览、屏幕及关机代次，推进已有启动意图并协调自动灭屏占用。
 */
void product_ui_camera_update(product_ui_camera_controller_t *controller)
{
    uint8_t is_off, needs_hold;
    d_err_t result;
    if (controller == NULL || !controller->is_attached) {
        return;
    }
    is_off = d_screen_power_is_off();
    camera_preview_get_snapshot(&controller->snapshot);
    if (controller->snapshot.is_shutdown_blocked ||
        controller->shutdown_epoch != controller->snapshot.shutdown_epoch) {
        controller->shutdown_epoch = controller->snapshot.shutdown_epoch;
        controller->has_start_intent = 0U;
    } else if (controller->was_screen_off && !is_off) {
        controller->has_start_intent = 1U;
    }
    if (is_off) {
        controller->has_start_intent = 0U;
        if (!controller->was_screen_off && controller->session) {
            camera_preview_request_stop(controller->session, CAMERA_PREVIEW_STOP_SCREEN);
            camera_preview_get_snapshot(&controller->snapshot);
        }
    }
    controller->was_screen_off = is_off;
    if (controller->has_start_intent) {
        result = camera_preview_request_start(&controller->session);
        /* 仅新进入或唤醒意图等待前次清理；普通错误必须由用户点击重试。 */
        if (result != D_ERR_BUSY) {
            controller->has_start_intent = 0U;
        }
        camera_preview_get_snapshot(&controller->snapshot);
        if (controller->snapshot.is_shutdown_blocked) {
            controller->has_start_intent = 0U;
        }
    }
    needs_hold = !is_off && controller->snapshot.is_requested &&
                 controller->snapshot.session == controller->session;
    if (needs_hold && !controller->has_screen_hold) {
        controller->has_screen_hold = d_screen_power_hold_auto_off() == D_OK;
        if (!controller->has_screen_hold) {
            camera_preview_request_stop(controller->session, CAMERA_PREVIEW_STOP_ERROR);
        }
    } else if (!needs_hold && controller->has_screen_hold) {
        d_screen_power_release_auto_off();
        controller->has_screen_hold = 0U;
    }
    product_ui_camera_describe(controller);
}

/**
 * @brief 刷新可重试能力后，为显式用户操作提交一次启动意图。
 */
void product_ui_camera_retry(product_ui_camera_controller_t *controller)
{
    if (controller == NULL || !controller->is_attached) {
        return;
    }
    product_ui_camera_update(controller);
    if (!controller->can_retry) {
        return;
    }
    controller->has_start_intent = 1U;
    product_ui_camera_update(controller);
}

/**
 * @brief 解除页面附着，撤销启动意图、请求会话停止并释放本控制器的灭屏占用。
 */
void product_ui_camera_leave(product_ui_camera_controller_t *controller)
{
    if (controller == NULL || !controller->is_attached) {
        return;
    }
    controller->is_attached = 0U;
    controller->has_start_intent = 0U;
    if (controller->session) {
        camera_preview_request_stop(controller->session, CAMERA_PREVIEW_STOP_PAGE);
    }
    if (controller->has_screen_hold) {
        d_screen_power_release_auto_off();
    }
    controller->has_screen_hold = 0U;
}
