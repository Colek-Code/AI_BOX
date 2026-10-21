/** @file product_ui_alarm_page.c @brief Draft ownership and deferred alarm commands. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_alarm_page.h"
#include "product_ui_alarm_view.h"
#include "product_ui_time_domain_internal.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

typedef struct {
    product_ui_alarm_draft_t draft;
    product_ui_alarm_ui_event_t pending;
    product_ui_alarm_mode_e mode;
    int32_t scroll[PRODUCT_UI_ALARM_MODE_COUNT];
    uint32_t reference_monday, reference_revision;
    uint8_t initialized, created, confirmed;
    uint8_t preview_waiting;
    d_err_t preview_error;
} product_ui_alarm_controller_t;
static product_ui_alarm_controller_t s_controller;
static product_ui_page_view_t s_page;
static lv_obj_t *s_primary, *s_settings;
static product_ui_alarm_snapshot_t s_status;

static void product_ui_alarm_queue(const product_ui_alarm_ui_event_t *event)
{
    if (event && s_controller.pending.action == PRODUCT_UI_ALARM_UI_NONE) s_controller.pending = *event;
}

static void product_ui_alarm_back(lv_event_t *event)
{
    product_ui_alarm_ui_event_t request = {.action = PRODUCT_UI_ALARM_UI_BACK,
        .view_generation = product_ui_alarm_view_generation()};
    (void)event;
    product_ui_alarm_queue(&request);
}

static void product_ui_alarm_feedback(d_err_t error)
{
    const char *message = error == D_ERR_NOT_READY ? (s_status.clock.valid ?
        "请先校时或等待闹钟服务就绪" : "未校时，请到设置中校时") :
        error == D_ERR_FULL ? "最多保存 16 条闹钟" : error == D_ERR_BUSY ? "记录已变化，请重新打开后操作" :
        error == D_ERR_NOT_FOUND ? "这条闹钟已不存在" : error == D_ERR_INVALID_ARG ? "请检查时间与重复星期" :
        error == PRODUCT_UI_ALARM_ERR_VERSION ? "存储版本不兼容，无法保存" : "操作失败，请重试";
    product_ui_toast_show(message, 2200U);
}

static int product_ui_alarm_present(void)
{
    product_ui_alarm_mode_e mode = s_controller.mode;
    lv_obj_t *settings = s_settings;
    if (mode == PRODUCT_UI_ALARM_LIST && !settings) {
        settings = product_ui_alarm_view_nav_button(s_page.nav, "设置", PRODUCT_UI_ALARM_UI_SETTINGS);
        if (!settings) return -1;
        lv_obj_align(settings, LV_ALIGN_RIGHT_MID, -56, 0);
    }
    if (product_ui_alarm_view_show(s_page.content, mode, &s_controller.draft, product_ui_alarm_queue) != 0) {
        if (settings != s_settings) lv_obj_delete(settings);
        return -1;
    }
    /* The list-only navigation action need not occupy heap in an editor. */
    if (mode != PRODUCT_UI_ALARM_LIST && settings) { lv_obj_delete(settings); settings = NULL; }
    s_settings = settings;
    lv_label_set_text(s_page.title, mode == PRODUCT_UI_ALARM_LIST ? "闹钟" : mode == PRODUCT_UI_ALARM_EDITOR ? "编辑闹钟" :
        mode == PRODUCT_UI_ALARM_ROUTINE ? "重复与作息" : "闹钟设置");
    lv_label_set_text(lv_obj_get_child(s_primary, 0), mode == PRODUCT_UI_ALARM_LIST ? "新建" :
        mode == PRODUCT_UI_ALARM_ROUTINE ? "确定" : "保存");
    product_ui_alarm_view_refresh(&s_status, &s_controller.draft);
    return 0;
}

static void product_ui_alarm_new_draft(void)
{
    product_ui_datetime_t now;
    memset(&s_controller.draft, 0, sizeof(s_controller.draft));
    s_controller.draft.record.rule.enabled = 1U;
    s_controller.draft.record.rule.schedule = PRODUCT_UI_ALARM_SCHEDULE_ONCE;
    s_controller.draft.record.rule.hour = 7U;
    if (s_status.clock.valid && product_ui_time_utc_to_beijing(s_status.clock.utc_seconds + 60, &now) == PRODUCT_UI_TIME_OK) {
        s_controller.draft.record.rule.hour = now.time.hour;
        s_controller.draft.record.rule.minute = now.time.minute;
    }
    strcpy(s_controller.draft.record.rule.label, product_ui_alarm_label(0U));
    s_controller.draft.dirty = 1U;
}

static void product_ui_alarm_confirmed(uint8_t confirmed, void *context)
{
    product_ui_alarm_ui_event_t event = {.action = (product_ui_alarm_ui_action_e)(uintptr_t)context,
        .view_generation = product_ui_alarm_view_generation()};
    if (confirmed) { s_controller.confirmed = 1U; product_ui_alarm_queue(&event); }
}

static uint8_t product_ui_alarm_confirm(product_ui_alarm_ui_action_e action)
{
    if (s_controller.confirmed) { s_controller.confirmed = 0U; return 1U; }
    product_ui_alarm_view_cancel_input();
    if (!product_ui_confirm_dialog_show(action == PRODUCT_UI_ALARM_UI_DELETE ? "删除闹钟" : "放弃修改",
        action == PRODUCT_UI_ALARM_UI_DELETE ? "删除后，该闹钟的稍后提醒也会取消。" : "未保存的修改将被放弃。",
        product_ui_alarm_confirmed, (void *)(uintptr_t)action)) product_ui_toast_show("无法显示确认，请重试", 1600U);
    return 0U;
}

static void product_ui_alarm_save(void)
{
    product_ui_alarm_command_t command = {0};
    uint32_t id = 0U;
    d_err_t result;
    if (s_controller.mode == PRODUCT_UI_ALARM_SETTINGS) {
        command.action = PRODUCT_UI_ALARM_VOLUME; command.volume = s_controller.draft.volume;
    } else {
        command.action = PRODUCT_UI_ALARM_EDIT;
        command.rule = s_controller.draft.record.rule;
        command.expected_revision = s_controller.draft.record.rule_revision;
        command.label_kind = s_controller.draft.record.label_kind;
        command.resolve_once = command.rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE;
    }
    result = product_ui_alarm_submit(&command, NULL, &id);
    if (result != D_OK) {
        product_ui_alarm_record_t latest;
        if (result == D_ERR_BUSY && s_controller.mode == PRODUCT_UI_ALARM_EDITOR &&
            product_ui_alarm_get_record(command.rule.id, &latest) == D_OK) {
            /* Keep editable fields. A new Save click must explicitly accept the
             * draft against the refreshed record revision; this click did not. */
            s_controller.draft.record.rule_revision = latest.rule_revision;
            s_controller.draft.has_conflict = 1U;
            product_ui_toast_show("记录已变化，请再次保存确认", 2200U);
        } else product_ui_alarm_feedback(result);
        return;
    }
    s_controller.draft.dirty = 0U;
    s_controller.draft.has_conflict = 0U;
    if (s_controller.mode == PRODUCT_UI_ALARM_EDITOR) {
        (void)product_ui_alarm_get_record(id, &s_controller.draft.record);
        s_controller.mode = PRODUCT_UI_ALARM_LIST;
        if (product_ui_alarm_present() != 0) s_controller.mode = PRODUCT_UI_ALARM_EDITOR;
        else product_ui_alarm_view_restore_scroll(s_controller.scroll[PRODUCT_UI_ALARM_LIST]);
    }
    product_ui_toast_show("已应用，正在确认保存", 1600U);
}

static product_ui_alarm_repeat_e product_ui_alarm_repeat_kind(const product_ui_alarm_t *rule)
{
    if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE) return PRODUCT_UI_ALARM_REPEAT_ONCE;
    if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT) return PRODUCT_UI_ALARM_REPEAT_FORTNIGHT;
    if (rule->routine_template == PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST) return PRODUCT_UI_ALARM_REPEAT_DOUBLE;
    if (rule->routine_template == PRODUCT_UI_ALARM_ROUTINE_SINGLE_REST) return PRODUCT_UI_ALARM_REPEAT_SINGLE;
    return rule->repeat_mask == 127U ? PRODUCT_UI_ALARM_REPEAT_DAILY : PRODUCT_UI_ALARM_REPEAT_CUSTOM;
}

static void product_ui_alarm_default_days(product_ui_alarm_routine_draft_t *routine)
{
    routine->rule.repeat_mask = routine->kind == PRODUCT_UI_ALARM_REPEAT_ONCE ? 0U :
        routine->kind == PRODUCT_UI_ALARM_REPEAT_DOUBLE ? 62U :
        routine->kind == PRODUCT_UI_ALARM_REPEAT_SINGLE || routine->kind == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT ? 126U : 127U;
    routine->rule.alternate_mask = routine->kind == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT ? 62U : 0U;
}

static void product_ui_alarm_edit_routine(const product_ui_alarm_ui_event_t *event)
{
    product_ui_alarm_routine_draft_t *routine = &s_controller.draft.routine;
    product_ui_datetime_t now;
    uint8_t value = (uint8_t)event->value;
    if (event->action == PRODUCT_UI_ALARM_UI_REPEAT) {
        if (value >= PRODUCT_UI_ALARM_REPEAT_COUNT || value == routine->kind) return;
        routine->kind = (product_ui_alarm_repeat_e)value;
        routine->rule.schedule = value == PRODUCT_UI_ALARM_REPEAT_ONCE ? PRODUCT_UI_ALARM_SCHEDULE_ONCE :
            value == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT ? PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT : PRODUCT_UI_ALARM_SCHEDULE_WEEKLY;
        routine->rule.routine_template = value == PRODUCT_UI_ALARM_REPEAT_DOUBLE ? PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST :
            value == PRODUCT_UI_ALARM_REPEAT_SINGLE ? PRODUCT_UI_ALARM_ROUTINE_SINGLE_REST :
            value == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT ? PRODUCT_UI_ALARM_ROUTINE_ALTERNATING : PRODUCT_UI_ALARM_ROUTINE_BASIC;
        if (value == PRODUCT_UI_ALARM_REPEAT_FORTNIGHT) {
            memset(&routine->rule.date, 0, sizeof(routine->rule.date));
            routine->phase_confirmed = 0U;
        }
        product_ui_alarm_default_days(routine);
        product_ui_toast_show("已载入所选模式的默认安排", 1600U);
    } else if (event->action == PRODUCT_UI_ALARM_UI_WEEKDAY && value < 7U) {
        routine->rule.repeat_mask ^= (uint8_t)(1U << value);
    } else if (event->action == PRODUCT_UI_ALARM_UI_ALTERNATE_DAY && value < 7U) {
        routine->rule.alternate_mask ^= (uint8_t)(1U << value);
    } else if (event->action == PRODUCT_UI_ALARM_UI_DEFAULT_DAYS) {
        product_ui_alarm_default_days(routine); /* Do not move an established anchor. */
    } else if (event->action == PRODUCT_UI_ALARM_UI_PHASE) {
        if (!s_status.clock.valid || product_ui_time_utc_to_beijing(s_status.clock.utc_seconds, &now) != PRODUCT_UI_TIME_OK) {
            product_ui_toast_show("请先校时，再确认本周周期", 2200U); return;
        }
        if (product_ui_alarm_anchor_for_week(&now.date, value, &routine->rule.date) == D_OK) routine->phase_confirmed = 1U;
    }
}

static uint8_t product_ui_alarm_merge_routine(void)
{
    const product_ui_alarm_routine_draft_t *routine = &s_controller.draft.routine;
    product_ui_alarm_t *rule = &s_controller.draft.record.rule;
    if (routine->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT && !routine->phase_confirmed) {
        product_ui_toast_show("请先校时，并选择本周属于哪一周", 2200U); return 0U;
    }
    if (routine->rule.schedule != PRODUCT_UI_ALARM_SCHEDULE_ONCE &&
        product_ui_time_domain_validate_alarm(&routine->rule) != PRODUCT_UI_TIME_OK) {
        product_ui_toast_show("至少选择一天响铃；暂停使用请关闭闹钟", 2400U); return 0U;
    }
    rule->schedule = routine->rule.schedule; rule->date = routine->rule.date;
    rule->repeat_mask = routine->rule.repeat_mask; rule->alternate_mask = routine->rule.alternate_mask;
    rule->routine_template = routine->rule.routine_template;
    s_controller.draft.dirty = 1U;
    return 1U;
}

static void product_ui_alarm_edit_draft(const product_ui_alarm_ui_event_t *event)
{
    product_ui_alarm_draft_t *draft = &s_controller.draft;
    uint8_t value = (uint8_t)event->value;
    switch (event->action) {
    case PRODUCT_UI_ALARM_UI_HOUR: draft->record.rule.hour = value; break;
    case PRODUCT_UI_ALARM_UI_MINUTE: draft->record.rule.minute = value; break;
    case PRODUCT_UI_ALARM_UI_LABEL:
        draft->record.label_kind = value; strcpy(draft->record.rule.label, product_ui_alarm_label(value)); break;
    case PRODUCT_UI_ALARM_UI_ENABLED: draft->record.rule.enabled = !draft->record.rule.enabled; break;
    case PRODUCT_UI_ALARM_UI_VOLUME: draft->volume = value; break;
    case PRODUCT_UI_ALARM_UI_VOLUME_DOWN: if (draft->volume > 1U) draft->volume--; break;
    case PRODUCT_UI_ALARM_UI_VOLUME_UP: if (draft->volume < 100U) draft->volume++; break;
    default: return;
    }
    draft->dirty = 1U;
}

static void product_ui_alarm_apply(product_ui_alarm_ui_event_t event)
{
    product_ui_alarm_controller_t before = s_controller;
    product_ui_alarm_command_t command = {0};
    d_err_t result = D_OK;
    uint8_t active;
    before.confirmed = 0U; /* A failed view change never reuses prior discard consent. */
    if (event.view_generation != product_ui_alarm_view_generation()) { s_controller.confirmed = 0U; return; }
    if (event.action == PRODUCT_UI_ALARM_UI_SAVE && s_controller.mode == PRODUCT_UI_ALARM_LIST) event.action = PRODUCT_UI_ALARM_UI_NEW;
    switch (event.action) {
    case PRODUCT_UI_ALARM_UI_NEW:
        if (!s_status.loaded || s_status.count >= PRODUCT_UI_ALARM_COUNT) {
            product_ui_alarm_feedback(!s_status.loaded ? D_ERR_NOT_READY : D_ERR_FULL); return;
        }
        product_ui_alarm_new_draft(); s_controller.mode = PRODUCT_UI_ALARM_EDITOR;
        s_controller.scroll[PRODUCT_UI_ALARM_EDITOR] = 0; break;
    case PRODUCT_UI_ALARM_UI_EDIT:
        result = product_ui_alarm_get_record(event.id, &s_controller.draft.record);
        if (result != D_OK || s_controller.draft.record.rule_revision != event.revision) {
            s_controller = before; product_ui_alarm_feedback(result ? result : D_ERR_BUSY); return;
        }
        s_controller.draft.dirty = 0U; s_controller.draft.has_conflict = 0U; s_controller.mode = PRODUCT_UI_ALARM_EDITOR;
        s_controller.scroll[PRODUCT_UI_ALARM_EDITOR] = 0; break;
    case PRODUCT_UI_ALARM_UI_TOGGLE:
        command.action = PRODUCT_UI_ALARM_ENABLE; command.id = event.id;
        command.expected_revision = event.revision; command.enabled = (uint8_t)event.value; command.resolve_once = 1U;
        result = product_ui_alarm_submit(&command, NULL, NULL); break;
    case PRODUCT_UI_ALARM_UI_SETTINGS:
        s_controller.draft.volume = s_status.volume; s_controller.draft.dirty = 0U;
        s_controller.mode = PRODUCT_UI_ALARM_SETTINGS; s_controller.scroll[PRODUCT_UI_ALARM_SETTINGS] = 0; break;
    case PRODUCT_UI_ALARM_UI_ROUTINE:
        s_controller.draft.routine.rule = s_controller.draft.record.rule;
        s_controller.draft.routine.kind = product_ui_alarm_repeat_kind(&s_controller.draft.record.rule);
        s_controller.draft.routine.phase_confirmed = s_controller.draft.record.rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT;
        s_controller.mode = PRODUCT_UI_ALARM_ROUTINE;
        s_controller.scroll[PRODUCT_UI_ALARM_ROUTINE] = 0; break;
    case PRODUCT_UI_ALARM_UI_SAVE:
        if (s_controller.mode != PRODUCT_UI_ALARM_ROUTINE) { product_ui_alarm_save(); return; }
        if (!product_ui_alarm_merge_routine()) return;
        s_controller.mode = PRODUCT_UI_ALARM_EDITOR; break;
    case PRODUCT_UI_ALARM_UI_BACK:
        if (s_controller.mode == PRODUCT_UI_ALARM_LIST) { (void)product_ui_navigation_back(); return; }
        if (s_controller.mode == PRODUCT_UI_ALARM_ROUTINE) { s_controller.mode = PRODUCT_UI_ALARM_EDITOR; break; }
        if (s_controller.draft.dirty && !product_ui_alarm_confirm(event.action)) return;
        product_ui_alarm_stop_preview(); s_controller.draft.dirty = 0U;
        s_controller.mode = PRODUCT_UI_ALARM_LIST; break;
    case PRODUCT_UI_ALARM_UI_DELETE:
        if (!product_ui_alarm_confirm(event.action)) return;
        command.action = PRODUCT_UI_ALARM_DELETE; command.id = s_controller.draft.record.rule.id;
        command.expected_revision = s_controller.draft.record.rule_revision;
        result = product_ui_alarm_submit(&command, NULL, NULL);
        if (result == D_OK) { s_controller.draft.dirty = 0U; s_controller.mode = PRODUCT_UI_ALARM_LIST; }
        break;
    case PRODUCT_UI_ALARM_UI_PREVIEW:
        (void)product_ui_alarm_preview_status(&active);
        if (active) product_ui_alarm_stop_preview();
        else {
            result = product_ui_alarm_preview(s_controller.draft.volume);
            if (result == D_ERR_BUSY) { product_ui_toast_show("对话或闹钟正在使用声音，暂不能试听", 2200U); return; }
            if (result == D_OK) { s_controller.preview_waiting = 1U; s_controller.preview_error = D_OK; }
        }
        break;
    case PRODUCT_UI_ALARM_UI_RETRY_SAVE: result = product_ui_alarm_retry_save(NULL); break;
    case PRODUCT_UI_ALARM_UI_TIME:
        product_ui_alarm_stop_preview();
        if (product_ui_open_module("product_settings_time") != 0) product_ui_toast_show("日期与时间打开失败", 1600U);
        return;
    default:
        if (s_controller.mode == PRODUCT_UI_ALARM_ROUTINE) product_ui_alarm_edit_routine(&event);
        else product_ui_alarm_edit_draft(&event);
        break;
    }
    if (result != D_OK) product_ui_alarm_feedback(result);
    if (s_controller.mode != before.mode ||
        (s_controller.mode == PRODUCT_UI_ALARM_ROUTINE && s_controller.draft.routine.kind != before.draft.routine.kind)) {
        s_controller.scroll[before.mode] = product_ui_alarm_view_scroll();
        if (product_ui_alarm_present() != 0) {
            s_controller = before;
            product_ui_toast_show("页面创建失败，请重试", 1600U);
        } else product_ui_alarm_view_restore_scroll(s_controller.scroll[s_controller.mode]);
    }
}

static void product_ui_alarm_check_reference(void)
{
    product_ui_datetime_t now;
    product_ui_date_t monday;
    uint32_t reference = 0U;
    if (s_controller.mode != PRODUCT_UI_ALARM_ROUTINE) return;
    if (s_status.clock.valid && product_ui_time_utc_to_beijing(s_status.clock.utc_seconds, &now) == PRODUCT_UI_TIME_OK &&
        product_ui_alarm_anchor_for_week(&now.date, 0U, &monday) == D_OK)
        reference = monday.year * 10000U + monday.month * 100U + monday.day;
    if (reference != s_controller.reference_monday || s_status.clock.revision != s_controller.reference_revision) {
        product_ui_alarm_view_cancel_input();
        s_controller.reference_monday = reference;
        s_controller.reference_revision = s_status.clock.revision;
    }
}

int product_ui_alarm_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {.title = "闹钟", .back_title = "返回",
        .show_back = 1U, .nav_back_cb = product_ui_alarm_back};
    if (!parent) return -1;
    s_controller.created = 0U;
    if (!s_controller.initialized || page_manager_get_current() == PAGE_TYPE_MAIN) {
        memset(&s_controller, 0, sizeof(s_controller)); s_controller.initialized = 1U;
    }
    s_controller.pending.action = PRODUCT_UI_ALARM_UI_NONE;
    product_ui_alarm_get_snapshot(&s_status);
    if (product_ui_page_frame_create_ex(parent, &spec, &s_page) != 0) goto fail;
    lv_obj_set_width(s_page.title, 112);
    s_primary = product_ui_alarm_view_nav_button(s_page.nav, "新建", PRODUCT_UI_ALARM_UI_SAVE);
    s_settings = NULL;
    if (!s_primary) goto fail;
    lv_obj_align(s_primary, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_pad_ver(s_page.content, 4, 0);
    lv_obj_remove_flag(s_page.content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (product_ui_alarm_present() != 0) goto fail;
    product_ui_alarm_view_restore_scroll(s_controller.scroll[s_controller.mode]);
    s_controller.created = 1U;
    return 0;
fail:
    product_ui_alarm_view_destroy(); product_ui_page_view_reset(&s_page);
    return -1;
}

static void product_ui_alarm_page_destroy(void)
{
    if (s_controller.created) s_controller.scroll[s_controller.mode] = product_ui_alarm_view_scroll();
    product_ui_alarm_stop_preview();
    s_controller.preview_waiting = 0U;
    product_ui_alarm_view_destroy(); product_ui_page_view_reset(&s_page);
    s_primary = s_settings = NULL; s_controller.created = 0U;
    s_controller.pending.action = PRODUCT_UI_ALARM_UI_NONE;
}

static void product_ui_alarm_page_update(uint32_t elapsed_ms)
{
    product_ui_alarm_ui_event_t event;
    d_err_t preview_result;
    product_ui_process(elapsed_ms);
    if (!s_controller.created) return;
    product_ui_alarm_get_snapshot(&s_status);
    product_ui_alarm_check_reference();
    event = s_controller.pending; s_controller.pending.action = PRODUCT_UI_ALARM_UI_NONE;
    if (event.action != PRODUCT_UI_ALARM_UI_NONE) product_ui_alarm_apply(event);
    if (!s_controller.created) return;
    product_ui_alarm_get_snapshot(&s_status);
    preview_result = product_ui_alarm_preview_status(&s_controller.draft.preview_active);
    if (s_controller.preview_waiting && preview_result != D_OK && preview_result != s_controller.preview_error)
        product_ui_toast_show("试听失败，声音暂不可用，可重试", 2000U);
    s_controller.preview_error = preview_result;
    if (!s_controller.draft.preview_active) s_controller.preview_waiting = 0U;
    product_ui_alarm_view_refresh(&s_status, &s_controller.draft);
    product_ui_alarm_view_update(elapsed_ms);
}

static const page_ops_t s_alarm_ops = {.create = product_ui_alarm_page_create,
    .destroy = product_ui_alarm_page_destroy, .update = product_ui_alarm_page_update, .flags = PAGE_FLAG_NO_GESTURE};
static page_desc_t s_alarm_desc = {.name = "ProductAlarm", .ops = &s_alarm_ops};
int product_ui_alarm_register_page(int page_type)
{ s_alarm_desc.page_type = page_type; return page_manager_register(&s_alarm_desc); }

static d_err_t product_ui_alarm_module_init(void)
{
    d_err_t result = product_ui_alarm_init();
    product_ui_alarm_snapshot_t snapshot;
    product_ui_alarm_get_snapshot(&snapshot);
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_ALARM,
        result != D_OK ? PRODUCT_UI_SERVICE_ERROR : !snapshot.loaded ? PRODUCT_UI_SERVICE_UNKNOWN :
        snapshot.storage_ready ? PRODUCT_UI_SERVICE_READY : PRODUCT_UI_SERVICE_DEGRADED, 0U, result);
    return D_OK; /* Optional alarm capabilities never prevent desktop startup. */
}
D_UI_MODULE_DEFINE(alarm, "闹钟", LV_SYMBOL_BELL, product_ui_alarm_register_page, product_ui_alarm_module_init);
#endif
