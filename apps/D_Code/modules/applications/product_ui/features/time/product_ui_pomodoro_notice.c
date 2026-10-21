/** @file product_ui_pomodoro_notice.c @brief Nonmodal notice; navigation deferred beyond the caller's page update. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_pomodoro_notice.h"
#include "product_ui_alarm_notice.h"
#if D_FEATURE_OTA
#include "modules/applications/product_ui/features/settings/product_ui_ota_notice.h"
#endif
#include "product_ui_pomodoro_page.h"
#include "product_ui_pomodoro_service.h"
#include "product_ui_pomodoro_view.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "platform/screen/power/d_screen_power.h"
#include "platform/screen/transition/d_page_transition.h"

static lv_obj_t *s_notice;
static uint32_t s_presented;
static uint32_t s_awakened;
static uint32_t s_visible_event;
static uint32_t s_open_event;
static uint32_t s_started_ms;
static uint8_t s_pending;
static uint8_t s_armed;

uint8_t product_ui_pomodoro_notice_pending(void) { return s_pending; }

static void product_ui_pomodoro_notice_open(void *context)
{
    uint32_t event_id = (uint32_t)(uintptr_t)context;
    product_ui_pomodoro_snapshot_t snapshot;
    s_open_event = 0U;
    product_ui_pomodoro_get_snapshot(&snapshot);
    if (snapshot.quiescing || snapshot.model.event_id != event_id) return;
    if (product_ui_navigate_to("pomodoro") == 0) {
        product_ui_pomodoro_page_show_timer();
        product_ui_pomodoro_acknowledge(event_id);
    } else product_ui_toast_show("无法打开番茄钟，请重试", 1500U);
}

static void product_ui_pomodoro_notice_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) { s_notice = NULL; s_armed = 0U; }
    else if (code == LV_EVENT_PRESSED) s_armed = 1U;
    else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED)
        s_armed = 0U;
    else if (code == LV_EVENT_CLICKED && s_armed && !s_open_event) {
        s_armed = 0U;
        if (lv_async_call(product_ui_pomodoro_notice_open, (void *)(uintptr_t)s_visible_event) == LV_RESULT_OK)
            s_open_event = s_visible_event;
    }
}

static uint8_t product_ui_pomodoro_notice_show(const product_ui_pomodoro_snapshot_t *snapshot)
{
    lv_obj_t *label;
    lv_obj_t *button = lv_button_create(lv_layer_top());
    if (button == NULL) return 0U;
    lv_obj_set_size(button, 296, 40);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    label = lv_label_create(button);
    if (label == NULL) { lv_obj_delete(button); return 0U; }
    lv_obj_add_style(label, product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
    lv_label_set_text_fmt(label, "%s完成 · 查看番茄钟", product_ui_pomodoro_phase_name(snapshot->model.completed_phase));
    lv_obj_center(label);
    s_notice = button;
    s_visible_event = snapshot->model.event_id;
    s_presented = s_visible_event;
    s_started_ms = lv_tick_get();
    lv_obj_add_event_cb(button, product_ui_pomodoro_notice_event, LV_EVENT_ALL, NULL);
    d_screen_request_composed_frame();
    return 1U;
}

void product_ui_pomodoro_notice_process(void)
{
    product_ui_pomodoro_snapshot_t snapshot;
    uint8_t blocked;
    product_ui_pomodoro_get_snapshot(&snapshot);
    s_pending = snapshot.model.event_id != 0U && snapshot.model.event_id != snapshot.model.acknowledged_event;
    if (s_pending && !snapshot.quiescing && s_awakened != snapshot.model.event_id) {
        s_awakened = snapshot.model.event_id;
        d_screen_power_wake();
    }
    blocked = product_ui_control_center_is_busy() || product_ui_feedback_is_modal() || product_ui_alarm_notice_blocking();
#if D_FEATURE_OTA
    blocked = blocked || product_ui_ota_notice_blocking();
#endif
    if (s_notice != NULL && (!s_pending || snapshot.quiescing || blocked ||
        snapshot.model.event_id != s_visible_event || (uint32_t)(lv_tick_get() - s_started_ms) >= 8000U)) {
        if (blocked && s_pending) s_presented = 0U;
        lv_obj_delete(s_notice);
        d_screen_request_composed_frame();
    }
    if (snapshot.quiescing && s_open_event) {
        (void)lv_async_call_cancel(product_ui_pomodoro_notice_open, (void *)(uintptr_t)s_open_event);
        s_open_event = 0U;
    }
    if (s_pending && !snapshot.quiescing && !blocked && !s_notice && s_presented != snapshot.model.event_id)
        (void)product_ui_pomodoro_notice_show(&snapshot);
}
#endif
