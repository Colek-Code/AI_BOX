/** @file product_ui_pomodoro_page.c @brief Commands and deferred view changes; runtime outlives the page. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_pomodoro_page.h"
#include "product_ui_pomodoro_view.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

static struct {
    product_ui_page_view_t page;
    product_ui_pomodoro_snapshot_t snapshot;
    product_ui_pomodoro_settings_t draft;
    product_ui_pomo_mode_e mode;
    product_ui_pomo_action_e pending;
    product_ui_pomo_action_e confirmed;
    uint32_t command_generation;
    uint32_t confirmation_generation;
    uint8_t field;
    uint8_t value;
    uint8_t holds_pull;
} s_page;

static void product_ui_pomodoro_queue(product_ui_pomo_action_e action, uint8_t field,
    uint8_t value, uint32_t generation)
{
    if (s_page.pending != PRODUCT_UI_POMO_NONE) return;
    s_page.pending = action; s_page.field = field; s_page.command_generation = generation;
    s_page.value = value;
}

static void product_ui_pomodoro_back(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        product_ui_pomodoro_queue(PRODUCT_UI_POMO_BACK, 0U, 0U, s_page.snapshot.model.generation);
}

static int product_ui_pomodoro_show(product_ui_pomo_mode_e mode)
{
    if (product_ui_pomodoro_view_show(s_page.page.content, mode, &s_page.snapshot,
                                     product_ui_pomodoro_queue) != 0) return -1;
    if (mode == PRODUCT_UI_POMO_SETTINGS && s_page.mode != mode) s_page.draft = s_page.snapshot.model.saved.settings;
    s_page.mode = mode;
    if (mode == PRODUCT_UI_POMO_SETTINGS) product_ui_control_center_hold_pull(&s_page.holds_pull);
    else product_ui_control_center_release_pull(&s_page.holds_pull);
    lv_label_set_text(s_page.page.title, mode == PRODUCT_UI_POMO_SETTINGS ? "番茄设置" :
        mode == PRODUCT_UI_POMO_STATS ? "专注统计" : "番茄钟");
    product_ui_pomodoro_view_refresh(&s_page.snapshot, &s_page.draft);
    return 0;
}

static void product_ui_pomodoro_confirmed(uint8_t confirmed, void *context)
{
    (void)context;
    if (confirmed && s_page.page.root != NULL)
        product_ui_pomodoro_queue(s_page.confirmed, 1U, 0U, s_page.confirmation_generation);
}

static void product_ui_pomodoro_edit(product_ui_pomo_action_e action, uint8_t field, uint8_t value)
{
    uint8_t *values[] = {&s_page.draft.focus_minutes, &s_page.draft.short_minutes,
        &s_page.draft.long_minutes, &s_page.draft.interval, &s_page.draft.auto_focus,
        &s_page.draft.auto_break, &s_page.draft.sound};
    static const uint8_t maxima[] = {120,30,60,8};
    if (s_page.mode != PRODUCT_UI_POMO_SETTINGS || field >= 7U) return;
    if (action == PRODUCT_UI_POMO_SET_DURATION && field < 3U && value >= 1U && value <= maxima[field])
        *values[field] = value;
    else if (action == PRODUCT_UI_POMO_TOGGLE && field >= 4U) *values[field] ^= 1U;
    else if (field < 4U && action == PRODUCT_UI_POMO_DECREASE && *values[field] > 1U) (*values[field])--;
    else if (field < 4U && action == PRODUCT_UI_POMO_INCREASE && *values[field] < maxima[field]) (*values[field])++;
}

static void product_ui_pomodoro_apply(product_ui_pomo_action_e action)
{
    d_err_t result = D_OK;
    uint32_t request;
    if ((action == PRODUCT_UI_POMO_END || action == PRODUCT_UI_POMO_CLEAR) && !s_page.field) {
        s_page.confirmed = action;
        s_page.confirmation_generation = s_page.command_generation;
        if (product_ui_confirm_dialog_show(action == PRODUCT_UI_POMO_END ? "结束本轮" : "清空统计",
            action == PRODUCT_UI_POMO_END ? "未完成专注不计入统计，确定结束？" : "清空完成记录，当前计时继续。确定清空？",
            product_ui_pomodoro_confirmed, NULL) == NULL) product_ui_toast_show("无法打开确认，请重试", 1500U);
        return;
    }
    switch (action) {
    case PRODUCT_UI_POMO_PRIMARY:
        result = product_ui_pomodoro_execute(s_page.snapshot.model.state == PRODUCT_UI_POMODORO_RUNNING ?
            PRODUCT_UI_POMODORO_PAUSE : s_page.snapshot.model.state == PRODUCT_UI_POMODORO_PAUSED ?
            PRODUCT_UI_POMODORO_RESUME : PRODUCT_UI_POMODORO_START, s_page.command_generation);
        break;
    case PRODUCT_UI_POMO_SKIP:
    case PRODUCT_UI_POMO_END:
        result = product_ui_pomodoro_execute(action == PRODUCT_UI_POMO_SKIP ? PRODUCT_UI_POMODORO_SKIP :
            PRODUCT_UI_POMODORO_END, s_page.command_generation);
        break;
    case PRODUCT_UI_POMO_SETTINGS_OPEN: result = product_ui_pomodoro_show(PRODUCT_UI_POMO_SETTINGS); break;
    case PRODUCT_UI_POMO_STATS_OPEN: result = product_ui_pomodoro_show(PRODUCT_UI_POMO_STATS); break;
    case PRODUCT_UI_POMO_BACK:
        if (s_page.mode == PRODUCT_UI_POMO_TIMER) (void)product_ui_navigation_back();
        else result = product_ui_pomodoro_show(PRODUCT_UI_POMO_TIMER);
        break;
    case PRODUCT_UI_POMO_SAVE: result = product_ui_pomodoro_save_settings(&s_page.draft, &request); break;
    case PRODUCT_UI_POMO_RETRY: result = product_ui_pomodoro_retry_save(&request); break;
    case PRODUCT_UI_POMO_CLEAR: result = product_ui_pomodoro_clear_stats(&request); break;
    case PRODUCT_UI_POMO_DECREASE:
    case PRODUCT_UI_POMO_INCREASE:
    case PRODUCT_UI_POMO_TOGGLE:
    case PRODUCT_UI_POMO_SET_DURATION:
        if (s_page.command_generation != s_page.snapshot.model.generation) result = D_ERR_BUSY;
        else product_ui_pomodoro_edit(action, s_page.field, s_page.value);
        break;
    default: break;
    }
    if (result != D_OK) product_ui_toast_show(result == D_ERR_BUSY ? "状态已变化，请重试" : "操作未完成，请重试", 1500U);
}

int product_ui_pomodoro_page_create(lv_obj_t *parent)
{
    product_ui_page_spec_t spec = {.title = "番茄钟", .back_title = "桌面", .show_back = 1U,
        .nav_back_cb = product_ui_pomodoro_back};
    lv_obj_t *settings;
    lv_obj_t *stats;
    memset(&s_page, 0, sizeof(s_page));
    product_ui_pomodoro_get_snapshot(&s_page.snapshot);
    s_page.draft = s_page.snapshot.model.saved.settings;
    if (product_ui_page_frame_create_ex(parent, &spec, &s_page.page) != 0) return -1;
    lv_obj_set_width(s_page.page.title, 140);
    if (product_ui_pomodoro_show(PRODUCT_UI_POMO_TIMER) != 0) return -1;
    settings = product_ui_pomodoro_view_nav_button(s_page.page.nav, "设置", PRODUCT_UI_POMO_SETTINGS_OPEN);
    stats = product_ui_pomodoro_view_nav_button(s_page.page.nav, "统计", PRODUCT_UI_POMO_STATS_OPEN);
    if (!settings || !stats) return -1;
    lv_obj_align(settings, LV_ALIGN_RIGHT_MID, -64, 0);
    lv_obj_align(stats, LV_ALIGN_RIGHT_MID, -12, 0);
    return 0;
}

void product_ui_pomodoro_page_show_timer(void)
{
    if (s_page.page.root != NULL && s_page.mode != PRODUCT_UI_POMO_TIMER)
        product_ui_pomodoro_queue(PRODUCT_UI_POMO_BACK, 0U, 0U, s_page.snapshot.model.generation);
}

static void product_ui_pomodoro_refresh_snapshot(void)
{
    uint8_t follows_saved = memcmp(&s_page.draft, &s_page.snapshot.model.saved.settings,
                                  sizeof(s_page.draft)) == 0;
    product_ui_pomodoro_get_snapshot(&s_page.snapshot);
    /* A delayed load can restore settings while this page is open. Follow it
     * only while the user has no unsaved draft of their own. */
    if (follows_saved) s_page.draft = s_page.snapshot.model.saved.settings;
}

static void product_ui_pomodoro_page_enter(void)
{
    product_ui_pomodoro_refresh_snapshot();
    product_ui_pomodoro_acknowledge(s_page.snapshot.model.event_id);
    product_ui_pomodoro_view_refresh(&s_page.snapshot, &s_page.draft);
}

static void product_ui_pomodoro_page_destroy(void)
{
    product_ui_control_center_release_pull(&s_page.holds_pull);
    product_ui_pomodoro_view_destroy();
    product_ui_page_view_reset(&s_page.page);
    memset(&s_page, 0, sizeof(s_page));
}

static void product_ui_pomodoro_page_update(uint32_t elapsed_ms)
{
    product_ui_pomo_action_e action;
    product_ui_process(elapsed_ms);
    if (!s_page.page.root) return;
    product_ui_pomodoro_refresh_snapshot();
    action = s_page.pending;
    s_page.pending = PRODUCT_UI_POMO_NONE;
    if (action != PRODUCT_UI_POMO_NONE) product_ui_pomodoro_apply(action);
    if (!s_page.page.root) return;
    product_ui_pomodoro_refresh_snapshot();
    product_ui_pomodoro_view_refresh(&s_page.snapshot, &s_page.draft);
}

static const page_ops_t s_pomodoro_ops = {.create = product_ui_pomodoro_page_create,
    .enter = product_ui_pomodoro_page_enter, .destroy = product_ui_pomodoro_page_destroy,
    .update = product_ui_pomodoro_page_update, .flags = PAGE_FLAG_NO_GESTURE};
static page_desc_t s_pomodoro_page = {.name = "ProductPomodoro", .ops = &s_pomodoro_ops};
int product_ui_pomodoro_register_page(int page_type)
{
    s_pomodoro_page.page_type = page_type;
    return page_manager_register(&s_pomodoro_page);
}
static d_err_t product_ui_pomodoro_module_init(void)
{
    d_err_t result = product_ui_pomodoro_init();
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_POMODORO,
        result == D_OK ? PRODUCT_UI_SERVICE_READY : PRODUCT_UI_SERVICE_ERROR, 0U, result);
    /* Optional capabilities expose their failures inside the page, preserving the desktop. */
    return D_OK;
}
D_UI_MODULE_DEFINE(pomodoro, "番茄钟", LV_SYMBOL_LOOP,
    product_ui_pomodoro_register_page, product_ui_pomodoro_module_init);
#endif
