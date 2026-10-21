/** Global upgrade progress overlay; the page and its draft stay alive beneath. */
#include "config/app_config.h"
#if D_FEATURE_OTA && defined(D_LVGL_ENABLE) && D_UI_TARGET == D_UI_PRODUCT
#include "product_ui_ota_notice.h"
#include "modules/services/ota/ota_service.h"
#include "modules/applications/product_ui/features/time/product_ui_time_controls.h"
#include "platform/screen/power/d_screen_power.h"
#include "platform/screen/transition/d_page_transition.h"
#include <string.h>

static struct {
    lv_obj_t *root, *status, *progress, *close;
    uint32_t revision;
    uint8_t pull, power, close_pending, armed;
} s_notice;

static const char *product_ui_ota_error_text(ota_error_e error)
{
    switch (error) {
    case OTA_ERROR_NETWORK_TIME: return "网络或联网校时不可用";
    case OTA_ERROR_TRANSPORT: return "下载连接或文件检查失败";
    case OTA_ERROR_PACKAGE: return "升级文件检查失败";
    case OTA_ERROR_MAINTENANCE: return "设备服务未能完成准备";
    case OTA_ERROR_WRITE: return "固件写入失败";
    case OTA_ERROR_VERIFY: return "固件校验失败";
    case OTA_ERROR_COMMIT: return "启动信息提交失败";
    case OTA_ERROR_DEADLINE: return "操作超时，等待底层完成";
    case OTA_ERROR_CLEANUP: return "正在等待设备服务停止";
    default: return "升级服务不可用";
    }
}

const char *product_ui_ota_state_text(unsigned state)
{
    static const char *const texts[] = {"等待检查", "正在检查升级包", "发现可下载升级包",
        "正在保存并停止设备服务", "正在下载并写入", "正在校验固件", "正在提交启动信息",
        "提交完成，即将重启", "升级失败，可重新检查", "升级状态未确认，请勿再次升级"};
    return state < sizeof(texts) / sizeof(texts[0]) ? texts[state] : "状态未知";
}

static void product_ui_ota_reset_input(void)
{
    lv_indev_t *input = NULL;
    s_notice.armed = 0U;
    while ((input = lv_indev_get_next(input)) != NULL) {
        uint8_t pressed = lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED;
        lv_indev_reset(input, NULL);
        if (pressed) lv_indev_wait_release(input);
    }
}

static void product_ui_ota_close_input(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) s_notice.armed = 1U;
    else if (code == LV_EVENT_CLICKED) {
        if (s_notice.armed) s_notice.close_pending = 1U;
        s_notice.armed = 0U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET ||
               code == LV_EVENT_DEFOCUSED) s_notice.armed = 0U;
}

int product_ui_ota_notice_prepare(void)
{
    lv_obj_t *root, *title, *status, *progress, *close;
    if (s_notice.root) return 0;
    root = lv_obj_create(lv_layer_sys());
    if (!root) return -1;
    product_ui_time_control_clean(root);
    lv_obj_set_size(root, 320, 240); lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    title = product_ui_time_label(root, "系统升级", PRODUCT_UI_STYLE_TITLE);
    status = product_ui_time_label(root, "正在准备", PRODUCT_UI_STYLE_BODY);
    progress = product_ui_time_label(root, "保持供电，勿长按电源键", PRODUCT_UI_STYLE_CAPTION);
    close = product_ui_time_button(root, "返回", product_ui_ota_close_input, NULL);
    if (!title || !status || !progress || !close) { lv_obj_delete(root); return -1; }
    lv_obj_set_pos(title, 12, 12); lv_obj_set_pos(status, 12, 58);
    lv_obj_set_width(status, 296); lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(progress, 12, 124); lv_obj_set_width(progress, 296);
    lv_label_set_long_mode(progress, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(close, 296, 40); lv_obj_set_pos(close, 12, 188);
    lv_obj_add_flag(close, LV_OBJ_FLAG_HIDDEN);
    s_notice.root = root; s_notice.status = status; s_notice.progress = progress; s_notice.close = close;
    s_notice.revision = UINT32_MAX;
    product_ui_ota_reset_input();
    product_ui_control_center_hold_pull(&s_notice.pull);
    s_notice.power = d_screen_power_hold_auto_off() == D_OK;
    d_screen_power_wake();
    d_screen_request_composed_frame();
    return 0;
}

uint8_t product_ui_ota_notice_blocking(void) { return s_notice.root != NULL; }

static void product_ui_ota_notice_hide(void)
{
    if (!s_notice.root) return;
    product_ui_ota_reset_input();
    lv_obj_delete(s_notice.root); s_notice.root = NULL;
    product_ui_control_center_release_pull(&s_notice.pull);
    if (s_notice.power) d_screen_power_release_auto_off();
    memset(&s_notice, 0, sizeof(s_notice));
    d_screen_request_composed_frame();
}

void product_ui_ota_notice_discard(void)
{
    if (!ota_service_maintenance_active()) product_ui_ota_notice_hide();
}

void product_ui_ota_notice_process(void)
{
    ota_snapshot_t snapshot;
    char text[160];
    uint8_t rejected;
    if (!s_notice.root) return;
    ota_service_get_snapshot(&snapshot);
    rejected = snapshot.state == OTA_FAILED && snapshot.error == OTA_ERROR_MAINTENANCE &&
        snapshot.detail == D_ERR_BUSY && !snapshot.worker_active && !snapshot.maintenance;
    if (rejected) s_notice.close_pending = 1U;
    if (s_notice.close_pending && !snapshot.worker_active && !snapshot.maintenance) {
        product_ui_ota_notice_hide();
        if (rejected) product_ui_toast_show("设备忙，本次未开始升级", 1500U);
        return;
    }
    s_notice.close_pending = 0U;
    if (s_notice.revision == snapshot.revision) return;
    s_notice.revision = snapshot.revision;
    lv_label_set_text(s_notice.status, product_ui_ota_state_text(snapshot.state));
    if (snapshot.error != OTA_ERROR_NONE)
        lv_snprintf(text, sizeof(text), "%s\n%s%s", product_ui_ota_error_text(snapshot.error),
            snapshot.worker_active ? "底层尚未确认停止，不能重试" : "服务已恢复，可返回后重新检查",
            snapshot.worker_active ? "\n保持供电，勿长按电源键" : "");
    else lv_snprintf(text, sizeof(text), "%u / %u KB\n保持供电，勿长按电源键",
        (unsigned)(snapshot.received / 1024U), (unsigned)((snapshot.total + 1023U) / 1024U));
    lv_label_set_text(s_notice.progress, text);
    if (!snapshot.worker_active && !snapshot.maintenance) lv_obj_remove_flag(s_notice.close, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_notice.close, LV_OBJ_FLAG_HIDDEN);
    d_screen_request_composed_frame();
}
#endif
