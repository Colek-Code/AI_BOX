/** Settings entry for explicit, generation-bound upgrade confirmation. */
#include "config/app_config.h"
#if D_FEATURE_OTA && defined(D_LVGL_ENABLE) && D_UI_TARGET == D_UI_PRODUCT
#include "product_ui_settings_details_common.h"
#include "product_ui_ota_notice.h"
#include "modules/services/ota/ota_service.h"
#include "modules/applications/product_ui/adapters/gc9307/product_ui_ota_gc9307.h"
#include "modules/applications/product_ui/features/time/product_ui_time_controls.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "framework/d_page_manager.h"
#include "framework/d_ui_module.h"
#include <string.h>

enum { OTA_UI_NONE, OTA_UI_CHECK, OTA_UI_CONFIRM, OTA_UI_CANCEL };
static struct {
    product_ui_page_view_t view;
    lv_obj_t *status, *detail, *action, *cancel, *pressed;
    ota_snapshot_t snapshot;
    uint32_t pressed_generation, pressed_request, pending_generation, pending_request;
    uint32_t revision;
    uint8_t pending, pressed_action, armed;
} s_page;

static void product_ui_ota_input(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_page.pressed = object; s_page.armed = 1U;
        s_page.pressed_generation = s_page.snapshot.generation;
        s_page.pressed_request = s_page.snapshot.request;
        s_page.pressed_action = object == s_page.cancel ? OTA_UI_CANCEL :
            s_page.snapshot.state == OTA_CONFIRM ? OTA_UI_CONFIRM : OTA_UI_CHECK;
    } else if (code == LV_EVENT_SCROLL_BEGIN) s_page.armed = 0U;
    else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED) {
        if (s_page.pressed == object) s_page.armed = 0U;
    }
    else if (code == LV_EVENT_CLICKED) {
        if (s_page.armed && s_page.pressed == object && !s_page.pending) {
            s_page.pending = s_page.pressed_action;
            s_page.pending_generation = s_page.pressed_generation;
            s_page.pending_request = s_page.pressed_request;
        }
        s_page.armed = 0U;
    }
}

static void product_ui_ota_page_refresh(void)
{
    char text[384];
    ota_service_get_snapshot(&s_page.snapshot);
    if (s_page.revision == s_page.snapshot.revision) return;
    s_page.revision = s_page.snapshot.revision;
    s_page.armed = 0U;
    lv_label_set_text(s_page.status, !s_page.snapshot.configured ? "未配置升级源" :
        product_ui_ota_state_text(s_page.snapshot.state));
    lv_snprintf(text, sizeof(text), "当前版本：%s\n目标版本：%s\n目标数据策略：%s\n\n%s",
        D_FIRMWARE_VERSION, s_page.snapshot.version[0] ? s_page.snapshot.version : "未提供",
        s_page.snapshot.vm_policy == OTA_VM_KEEP ? "保留 VM" :
        s_page.snapshot.vm_policy == OTA_VM_ERASE ? "清空 VM" : "未提供",
        s_page.snapshot.state == OTA_CONFIRM ?
        "确认后暂停番茄钟、停止对话和相机。请保持供电，勿长按电源键（8秒会复位），完成后重启。" :
        s_page.snapshot.error == OTA_ERROR_NETWORK_TIME ? "请连接 WiFi 并完成联网校时后重试。" :
        "只检查已配置地址，不代表已确认有更高版本。目标策略未提供时无法保证保留数据。");
    lv_label_set_text(s_page.detail, text);
    lv_label_set_text(lv_obj_get_child(s_page.action, 0), s_page.snapshot.state == OTA_CONFIRM ? "确认升级" : "检查升级包");
    if (s_page.snapshot.worker_active || !s_page.snapshot.configured)
        lv_obj_add_state(s_page.action, LV_STATE_DISABLED);
    else lv_obj_remove_state(s_page.action, LV_STATE_DISABLED);
    if (s_page.snapshot.state == OTA_CHECKING) lv_obj_remove_flag(s_page.cancel, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_page.cancel, LV_OBJ_FLAG_HIDDEN);
}

static int product_ui_settings_ota_create(lv_obj_t *parent)
{
    lv_obj_t *content;
    memset(&s_page, 0, sizeof(s_page)); s_page.revision = UINT32_MAX;
    content = product_ui_settings_detail_frame_create(parent, "系统升级", &s_page.view);
    if (!content) goto fail;
    s_page.status = product_ui_time_label(content, "", PRODUCT_UI_STYLE_BODY);
    s_page.detail = product_ui_time_label(content, "", PRODUCT_UI_STYLE_CAPTION);
    s_page.action = product_ui_time_button(content, "检查升级包", product_ui_ota_input, NULL);
    s_page.cancel = product_ui_time_button(content, "取消检查", product_ui_ota_input, NULL);
    if (!s_page.status || !s_page.detail || !s_page.action || !s_page.cancel) goto fail;
    lv_obj_set_width(s_page.status, 280); lv_label_set_long_mode(s_page.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_page.detail, 280); lv_label_set_long_mode(s_page.detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(s_page.action, 280, 40); lv_obj_set_size(s_page.cancel, 280, 40);
    lv_obj_add_event_cb(content, product_ui_ota_input, LV_EVENT_SCROLL_BEGIN, NULL);
    product_ui_ota_page_refresh();
    return 0;
fail:
    /* Frame root is the manager-owned container, including failed prepares. */
    product_ui_page_view_reset(&s_page.view);
    memset(&s_page, 0, sizeof(s_page));
    return -1;
}

static void product_ui_settings_ota_update(uint32_t elapsed)
{
    uint8_t action;
    d_err_t result = D_OK;
    product_ui_process(elapsed);
    if (!s_page.view.root) return;
    action = s_page.pending; s_page.pending = OTA_UI_NONE;
    if (action == OTA_UI_CHECK) result = ota_service_check(NULL);
    else if (action == OTA_UI_CANCEL) result = ota_service_cancel_check(s_page.pending_request);
    else if (action == OTA_UI_CONFIRM) {
        /* Allocation failure cannot start an invisible upgrade. This occurs in
         * the page processing phase, not inside an LVGL object callback. */
        if (product_ui_ota_notice_prepare() != 0) result = D_ERR_NO_MEMORY;
        else {
            result = ota_service_confirm(s_page.pending_generation, NULL);
            if (result != D_OK) product_ui_ota_notice_discard();
        }
    }
    if (result != D_OK) product_ui_toast_show(result == D_ERR_BUSY ? "设备忙，请稍后重试" : "操作未接受，请重新检查", 1500U);
    else if (action == OTA_UI_CANCEL) product_ui_toast_show("正在取消检查，请稍后", 1500U);
    product_ui_ota_page_refresh();
}
static void product_ui_settings_ota_destroy(void)
{
    s_page.armed = 0U;
    product_ui_page_view_reset(&s_page.view);
    memset(&s_page, 0, sizeof(s_page));
}
static int product_ui_settings_ota_init(void)
{ (void)ota_service_init(); return product_ui_ota_register_backend(); }
static const page_ops_t s_ota_ops = {.create = product_ui_settings_ota_create,
    .destroy = product_ui_settings_ota_destroy, .update = product_ui_settings_ota_update,
    .flags = PAGE_FLAG_NO_GESTURE};
static page_desc_t s_ota_desc = {.name = "系统升级", .ops = &s_ota_ops};
static int product_ui_settings_ota_register(int type)
{ s_ota_desc.page_type = type; return page_manager_register(&s_ota_desc); }
D_UI_MODULE_DEFINE(product_settings_ota, "系统升级", LV_SYMBOL_SETTINGS,
                   product_ui_settings_ota_register, product_ui_settings_ota_init);
#endif
