/** @file product_ui_alarm_notice.c @brief Alarm overlay retains the page beneath it until audio release. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_alarm_notice.h"
#include "product_ui_alarm_service.h"
#include "product_ui_time_controls.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "platform/screen/power/d_screen_power.h"
#include "platform/screen/transition/d_page_transition.h"
#include <string.h>

enum { ALARM_NOTICE_NONE, ALARM_NOTICE_STOP, ALARM_NOTICE_SNOOZE, ALARM_NOTICE_RETRY };
static struct {
    lv_obj_t *root, *status, *items, *stop, *snooze, *retry, *pressed;
    uint32_t group, pressed_group, command_group, awakened_group, retry_at;
    uint8_t armed, command, pull_held, power_held, pending;
    uint8_t restore_control_center;
} s_notice;

uint8_t product_ui_alarm_notice_blocking(void) { return s_notice.root != NULL; }
uint8_t product_ui_alarm_notice_pending(void) { return s_notice.pending; }

static void product_ui_alarm_notice_reset_touch(void)
{
    lv_indev_t *input = NULL;
    s_notice.armed = 0U;
    while ((input = lv_indev_get_next(input)) != NULL) {
        uint8_t pressed = lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED;
        lv_indev_reset(input, NULL);
        if (pressed) lv_indev_wait_release(input);
    }
}

static void product_ui_alarm_notice_input(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_notice.pressed = object; s_notice.pressed_group = s_notice.group; s_notice.armed = 1U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED) {
        /* The previous button may lose focus after the new one was pressed. */
        if (s_notice.pressed == object) s_notice.armed = 0U;
    } else if (code == LV_EVENT_CLICKED) {
        uint8_t accepted = s_notice.armed && s_notice.pressed == object && s_notice.pressed_group == s_notice.group;
        s_notice.armed = 0U;
        if (accepted && !s_notice.command) {
            s_notice.command = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
            s_notice.command_group = s_notice.pressed_group;
        }
    }
}

static void product_ui_alarm_notice_enable(lv_obj_t *object, uint8_t enabled)
{
    if (enabled) lv_obj_remove_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(lv_obj_get_child(object, 0),
        product_ui_color(enabled ? PRODUCT_UI_COLOR_TEXT : PRODUCT_UI_COLOR_TEXT_MUTED), 0);
}

static void product_ui_alarm_notice_text(lv_obj_t *object, const char *text)
{ if (strcmp(lv_label_get_text(object), text)) lv_label_set_text(object, text); }

static void product_ui_alarm_notice_hide(uint8_t restore)
{
    if (!s_notice.root) return;
    product_ui_alarm_notice_reset_touch();
    lv_obj_delete(s_notice.root); s_notice.root = NULL;
    product_ui_control_center_release_pull(&s_notice.pull_held);
    if (restore && s_notice.restore_control_center) product_ui_control_center_show(1U);
    s_notice.restore_control_center = 0U;
    if (s_notice.power_held) d_screen_power_release_auto_off();
    s_notice.power_held = 0U; s_notice.command = 0U;
    d_screen_request_composed_frame();
}

static int product_ui_alarm_notice_show(void)
{
    lv_obj_t *root = lv_obj_create(lv_layer_sys());
    lv_obj_t *title, *status, *list, *items, *stop, *snooze, *retry;
    if (!root) return -1;
    product_ui_time_control_clean(root);
    lv_obj_set_size(root, 320, 240); lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    title = product_ui_time_label(root, "闹钟响铃", PRODUCT_UI_STYLE_TITLE);
    status = product_ui_time_label(root, "正在交接声音", PRODUCT_UI_STYLE_CAPTION);
    list = lv_obj_create(root);
    if (!title || !status || !list) goto fail;
    lv_obj_set_pos(title, 12, 8); lv_obj_set_pos(status, 12, 38); lv_obj_set_width(status, 296);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    product_ui_time_control_clean(list); lv_obj_set_size(list, 296, 112); lv_obj_set_pos(list, 12, 74);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_PRESS_LOCK); lv_obj_set_scroll_dir(list, LV_DIR_VER);
    items = product_ui_time_label(list, "", PRODUCT_UI_STYLE_BODY);
    stop = product_ui_time_button(root, "停止", product_ui_alarm_notice_input, (void *)(uintptr_t)ALARM_NOTICE_STOP);
    snooze = product_ui_time_button(root, "5 分钟后提醒", product_ui_alarm_notice_input, (void *)(uintptr_t)ALARM_NOTICE_SNOOZE);
    retry = product_ui_time_button(root, "重试", product_ui_alarm_notice_input, (void *)(uintptr_t)ALARM_NOTICE_RETRY);
    if (!items || !stop || !snooze || !retry) goto fail;
    lv_obj_set_width(items, 292); lv_label_set_long_mode(items, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(stop, 140, 44); lv_obj_set_pos(stop, 12, 190);
    lv_obj_set_size(snooze, 148, 44); lv_obj_set_pos(snooze, 160, 190);
    lv_obj_set_size(retry, 60, 36); lv_obj_set_pos(retry, 248, 2);
    lv_obj_add_flag(retry, LV_OBJ_FLAG_HIDDEN);
    s_notice.root = root; s_notice.status = status; s_notice.items = items;
    s_notice.stop = stop; s_notice.snooze = snooze; s_notice.retry = retry;
    product_ui_alarm_notice_reset_touch();
    s_notice.restore_control_center = product_ui_control_center_is_open();
    product_ui_control_center_hold_pull(&s_notice.pull_held);
    s_notice.power_held = d_screen_power_hold_auto_off() == D_OK;
    d_screen_request_composed_frame();
    return 0;
fail:
    lv_obj_delete(root); return -1;
}

static void product_ui_alarm_notice_command(void)
{
    product_ui_alarm_command_t command = {0};
    d_err_t result;
    uint8_t action = s_notice.command;
    s_notice.command = ALARM_NOTICE_NONE;
    if (action == ALARM_NOTICE_NONE) return;
    command.group_generation = s_notice.command_group;
    command.action = action == ALARM_NOTICE_SNOOZE ? PRODUCT_UI_ALARM_SNOOZE_GROUP : PRODUCT_UI_ALARM_STOP_GROUP;
    result = action == ALARM_NOTICE_RETRY ? product_ui_alarm_retry_audio(command.group_generation) :
        product_ui_alarm_submit(&command, NULL, NULL);
    if (result != D_OK) product_ui_alarm_notice_text(s_notice.status,
        result == D_ERR_BUSY ? "提醒组已变化，请重新操作" : "操作失败，请重试");
}

void product_ui_alarm_notice_process(void)
{
    product_ui_alarm_snapshot_t snapshot;
    product_ui_alarm_record_t record;
    uint8_t index, can_snooze = 1U;
    uint8_t snooze_in_range;
    char lines[512];
    uint16_t length = 0U;
    product_ui_alarm_get_snapshot(&snapshot);
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_ALARM,
        !snapshot.loaded ? PRODUCT_UI_SERVICE_UNKNOWN : !snapshot.timer_ready ? PRODUCT_UI_SERVICE_ERROR :
        snapshot.storage_error || snapshot.audio_error ? PRODUCT_UI_SERVICE_DEGRADED : PRODUCT_UI_SERVICE_READY,
        PRODUCT_UI_ALARM_CAP_OPEN | (snapshot.enabled_count ? PRODUCT_UI_ALARM_STATE_ACTIVE : 0U),
        snapshot.timer_error ? snapshot.timer_error : snapshot.storage_error ? snapshot.storage_error : snapshot.audio_error);
    s_notice.pending = snapshot.missed_count != 0U;
    if (s_notice.root) product_ui_alarm_notice_command();
    product_ui_alarm_get_snapshot(&snapshot);
    if (!snapshot.ring_mask && !snapshot.stop_mask && !snapshot.audio_owned) {
        product_ui_alarm_notice_hide(!snapshot.quiescing); return;
    }
    if (snapshot.ring_mask && s_notice.awakened_group != snapshot.group_generation) {
        s_notice.awakened_group = snapshot.group_generation; d_screen_power_wake();
    }
    if (!s_notice.root) {
        if (s_notice.retry_at && (int32_t)(lv_tick_get() - s_notice.retry_at) < 0) return;
        if (product_ui_alarm_notice_show() != 0) { s_notice.retry_at = lv_tick_get() + 250U; return; }
        s_notice.retry_at = 0U;
    }
    if (s_notice.group != snapshot.group_generation) {
        product_ui_alarm_notice_reset_touch(); s_notice.group = snapshot.group_generation;
    }
    lines[0] = '\0';
    for (index = 0U; index < snapshot.count; index++) {
        if (product_ui_alarm_get_at(index, &record) != D_OK) continue;
        if (record.state != PRODUCT_UI_ALARM_WAIT_AUDIO && record.state != PRODUCT_UI_ALARM_RINGING && record.state != PRODUCT_UI_ALARM_STOPPING) continue;
        if (record.snooze_count >= PRODUCT_UI_ALARM_SNOOZE_LIMIT) can_snooze = 0U;
        /* At most 16 * (7-byte time prefix + 23-byte label + newline). */
        length += (uint16_t)lv_snprintf(lines + length, sizeof(lines) - length, "%02u:%02u  %s\n",
            record.rule.hour, record.rule.minute, record.rule.label);
    }
    product_ui_alarm_notice_text(s_notice.items, length ? lines : "正在释放声音，请稍候");
    product_ui_alarm_notice_text(s_notice.status, snapshot.audio_error != D_OK ?
        (snapshot.stop_mask || !snapshot.ring_mask ? "声音停止失败，请点重试后再退出" : "声音交接失败，仍保留提醒，可重试") :
        snapshot.stop_mask || !snapshot.ring_mask ? "正在停止声音，请稍候" : snapshot.audio_playing ? "正在响铃 · 最长 5 分钟" : "正在交接声音");
    lv_obj_update_flag(s_notice.retry, LV_OBJ_FLAG_HIDDEN, snapshot.audio_error == D_OK);
    product_ui_alarm_notice_enable(s_notice.stop, snapshot.ring_mask && !snapshot.stop_mask && !snapshot.quiescing);
    product_ui_alarm_notice_text(lv_obj_get_child(s_notice.stop, 0), snapshot.stop_mask || !snapshot.ring_mask ? "停止中" : "停止");
    snooze_in_range = snapshot.clock.valid &&
        snapshot.clock.utc_seconds <= PRODUCT_UI_CLOCK_END_UTC - PRODUCT_UI_ALARM_SNOOZE_SECONDS;
    product_ui_alarm_notice_enable(s_notice.snooze, snapshot.ring_mask && !snapshot.stop_mask &&
        can_snooze && snooze_in_range && !snapshot.quiescing);
    product_ui_alarm_notice_text(lv_obj_get_child(s_notice.snooze, 0), !can_snooze ? "稍后已达上限" :
        !snapshot.clock.valid ? "请先校时" : !snooze_in_range ? "稍后超出范围" : "5 分钟后提醒");
}
#endif
