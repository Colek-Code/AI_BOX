/** @file product_ui_orientation.c @brief UI 串行协调显示翻转及偏好保存，不持有控制中心对象。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_orientation.h"
#include "product_ui_appearance.h"
#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "platform/screen/d_display.h"
#include "system/includes.h"

/* 仅由 UI 执行器访问。显示与保存完成前，外观服务持有维护可见的事务占用；
 * 此处不保留控制中心对象。 */
static struct {
    uint32_t appearance_id;
    uint32_t display_id;
    uint8_t display_applied;
    uint8_t notice_shown;
    uint8_t registered;
} s_orientation;

/**
 * @brief 预留外观队列并受理切换方向的显示请求。
 * @param[in] command 仅接受 PRODUCT_UI_SYSTEM_CMD_TOGGLE_ROTATION。
 * @param[in] payload 必须为 NULL，当前命令无载荷。
 * @param[in] size 必须为零，当前命令无载荷字节。
 * @param[in] context 注册回调的借用上下文；当前未使用。
 * @return 成功受理返回 D_OK；命令格式无效、不可用、忙或下层失败时返回相应错误。
 * @note 按钮事件中只受理请求，不在 LVGL 事件分发内切换显示或输入设备；显示提交失败即向外观事务确认失败。
 */
static int product_ui_orientation_command(uint16_t command, const void *payload,
                                           uint16_t size, void *context)
{
    d_display_rotation_snapshot_t display;
    d_err_t result;
    uint32_t appearance_id;
    uint32_t display_id;
    uint16_t target;
    (void)context;
    if (command != PRODUCT_UI_SYSTEM_CMD_TOGGLE_ROTATION || size || payload) return D_ERR_INVALID_ARG;
    d_display_get_rotation_snapshot(&display);
    if (!display.is_available) return D_ERR_NOT_READY;
    if (display.is_busy || s_orientation.appearance_id) return D_ERR_BUSY;
    target = display.actual_degrees == 0U ? 180U : 0U;
    result = product_ui_appearance_request_rotation(target, &appearance_id);
    if (result != D_OK) return result;
    result = d_display_request_rotation(target, &display_id);
    if (result != D_OK) {
        product_ui_appearance_rotation_applied(appearance_id, result);
        return result;
    }
    s_orientation.appearance_id = appearance_id;
    s_orientation.display_id = display_id;
    s_orientation.display_applied = 0U;
    s_orientation.notice_shown = 0U;
    /* 按钮的 LVGL 事件分发期间只受理，显示与输入设备切换留给周期处理。 */
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_SYSTEM, PRODUCT_UI_SERVICE_READY,
        display.actual_degrees == 180U ? PRODUCT_UI_SYSTEM_STATE_ROTATION_ON : 0U, D_OK);
    return D_OK;
}

/**
 * @brief 应用启动方向并注册 UI 系统翻转命令。
 */
void product_ui_orientation_init(void)
{
    product_ui_appearance_snapshot_t appearance;
    d_display_rotation_snapshot_t display;
    d_err_t result;
    product_ui_appearance_get_snapshot(&appearance);
    d_display_get_rotation_snapshot(&display);
    if (!display.is_available) return;
    result = d_display_set_initial_rotation(appearance.rotation_degrees);
    printf("[DISPLAY-ROTATION] boot default=%u requested=%u source=%s record=%d apply=%d\n",
           (unsigned)D_SCREEN_DEFAULT_ROTATION_DEGREES, appearance.rotation_degrees,
           appearance.has_rotation_override ? "user" : "product", appearance.record_error, result);
    if (result != D_OK) return;
    s_orientation.registered = product_ui_service_register_command(PRODUCT_UI_SERVICE_SYSTEM,
        product_ui_orientation_command, NULL) == 0;
    product_ui_orientation_process();
}

/**
 * @brief 推进显示与存储两段完成确认，更新系统服务投影和一次性提示。
 */
void product_ui_orientation_process(void)
{
    d_display_rotation_snapshot_t display;
    product_ui_appearance_snapshot_t appearance;
    uint32_t mask = 0U;
    d_err_t result;
    d_display_process_rotation();
    d_display_get_rotation_snapshot(&display);
    product_ui_appearance_get_snapshot(&appearance);
    if (s_orientation.appearance_id && !s_orientation.display_applied &&
        !display.is_busy && display.completed_id == s_orientation.display_id) {
        product_ui_appearance_rotation_applied(s_orientation.appearance_id, display.result);
        s_orientation.display_applied = 1U;
        product_ui_appearance_get_snapshot(&appearance);
    }
    if (s_orientation.appearance_id && !s_orientation.notice_shown &&
        display.is_busy && display.result != D_OK) {
        product_ui_toast_show("翻转未完成，显示暂不可用", 1800U);
        s_orientation.notice_shown = 1U;
    }
    if (s_orientation.appearance_id && !s_orientation.notice_shown && appearance.is_timed_out) {
        product_ui_toast_show(s_orientation.display_applied ? "已翻转，仍在保存" : "翻转仍在处理中", 1800U);
        s_orientation.notice_shown = 1U;
    }
    if (s_orientation.appearance_id && appearance.completed_id == s_orientation.appearance_id &&
        !appearance.is_pending && !appearance.is_in_progress) {
        if (appearance.result != D_OK) {
            product_ui_toast_show(display.result == D_OK ? "已翻转，本次未保存" : "翻转失败", 1800U);
        }
        printf("[DISPLAY-ROTATION] preference=%u result=%d actual=%u saved=%u\n",
               appearance.completed_id, appearance.result, display.actual_degrees, appearance.persisted_rotation_degrees);
        s_orientation.appearance_id = 0U;
    }
    if (!s_orientation.registered) return;
    if (display.actual_degrees == 180U) mask |= PRODUCT_UI_SYSTEM_STATE_ROTATION_ON;
    if (display.is_available && !display.is_busy && appearance.is_service_ready &&
        !appearance.is_pending && !appearance.is_in_progress && !appearance.is_quiescing &&
        !appearance.is_record_protected) mask |= PRODUCT_UI_SYSTEM_CAP_ROTATION;
    result = display.result != D_OK ? display.result : appearance.result;
    if (result == D_OK && appearance.is_timed_out) result = D_ERR_TIMEOUT;
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_SYSTEM,
        display.is_available ? PRODUCT_UI_SERVICE_READY : PRODUCT_UI_SERVICE_UNAVAILABLE, mask, result);
}
#endif
