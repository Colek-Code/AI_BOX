/**
 * @file product_ui_schedule_page.c
 * @brief Shared event snapshot driven schedule page.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/time/product_ui_schedule_page.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <stdint.h>
#include <string.h>

#define PRODUCT_UI_SCHEDULE_REFRESH_MS 500U

typedef struct {
    product_ui_page_view_t page;
    product_ui_virtual_list_t list;
    product_ui_time_domain_summary_t summary;
    product_ui_calendar_event_t slot_events[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    product_ui_list_item_t slot_rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    char slot_subtitles[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT][28];
    uint8_t sorted_indices[PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS];
    uint32_t generation;
    uint8_t is_list_ready;
    uint32_t service_revision;
    uint32_t refresh_elapsed_ms;
    uint8_t refresh_pending;
} product_ui_schedule_view_t;

typedef struct {
    uint16_t first_visible;
} product_ui_schedule_controller_t;

static product_ui_schedule_view_t s_schedule_view;
static product_ui_schedule_controller_t s_schedule_controller;

static void product_ui_schedule_format_subtitle(
    char text[28], const product_ui_calendar_event_t *event)
{
    const product_ui_date_t *date = &event->start.date;
    const char *state = event->completed ? "已完成" : "未完成";

    text[0] = (char)('0' + ((date->year / 1000U) % 10U));
    text[1] = (char)('0' + ((date->year / 100U) % 10U));
    text[2] = (char)('0' + ((date->year / 10U) % 10U));
    text[3] = (char)('0' + (date->year % 10U));
    text[4] = '-';
    text[5] = (char)('0' + (date->month / 10U));
    text[6] = (char)('0' + (date->month % 10U));
    text[7] = '-';
    text[8] = (char)('0' + (date->day / 10U));
    text[9] = (char)('0' + (date->day % 10U));
    text[10] = ' ';
    strcpy(&text[11], state);
}

static int product_ui_schedule_datetime_compare(
    const product_ui_datetime_t *left, const product_ui_datetime_t *right)
{
    if (left->date.year != right->date.year) {
        return left->date.year < right->date.year ? -1 : 1;
    }
    if (left->date.month != right->date.month) {
        return left->date.month < right->date.month ? -1 : 1;
    }
    if (left->date.day != right->date.day) {
        return left->date.day < right->date.day ? -1 : 1;
    }
    if (left->time.hour != right->time.hour) {
        return left->time.hour < right->time.hour ? -1 : 1;
    }
    if (left->time.minute != right->time.minute) {
        return left->time.minute < right->time.minute ? -1 : 1;
    }
    if (left->time.second == right->time.second) {
        return 0;
    }
    return left->time.second < right->time.second ? -1 : 1;
}

static int product_ui_schedule_order_build(void)
{
    product_ui_calendar_event_t candidate;
    product_ui_calendar_event_t previous;
    uint8_t source_index;
    uint8_t insert_at;

    for (source_index = 0U;
         source_index < s_schedule_view.summary.event_count;
         source_index++) {
        if (product_ui_time_domain_get_event(source_index, &candidate) !=
            PRODUCT_UI_TIME_OK) {
            return -1;
        }
        insert_at = source_index;
        while (insert_at > 0U) {
            if (product_ui_time_domain_get_event(
                    s_schedule_view.sorted_indices[insert_at - 1U],
                    &previous) != PRODUCT_UI_TIME_OK) {
                return -1;
            }
            if (product_ui_schedule_datetime_compare(
                    &previous.start, &candidate.start) <= 0) {
                break;
            }
            s_schedule_view.sorted_indices[insert_at] =
                s_schedule_view.sorted_indices[insert_at - 1U];
            insert_at--;
        }
        s_schedule_view.sorted_indices[insert_at] = source_index;
    }
    return 0;
}

static int product_ui_schedule_row_bind(uint16_t item_index,
                                        uint8_t slot_index,
                                        product_ui_row_spec_t *spec,
                                        void *user_data)
{
    product_ui_schedule_view_t *view = user_data;
    product_ui_calendar_event_t *event;
    product_ui_list_item_t *row;

    if ((view == NULL) || (slot_index >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) ||
        (item_index >= view->summary.event_count)) {
        return -1;
    }
    event = &view->slot_events[slot_index];
    row = &view->slot_rows[slot_index];
    if (product_ui_time_domain_get_event(
            view->sorted_indices[item_index], event) != PRODUCT_UI_TIME_OK) {
        return -1;
    }
    *row = (product_ui_list_item_t) {
        .item_id = "schedule_item",
        .title = event->title[0] != '\0' ? event->title : "未命名日程",
        .icon_id = PRODUCT_UI_ICON_SCHEDULE,
        .accent = PRODUCT_UI_COLOR_WARNING,
    };
    product_ui_schedule_format_subtitle(view->slot_subtitles[slot_index],
                                        event);
    *spec = (product_ui_row_spec_t) {
        .item_key = event->id,
        .item = row,
        .subtitle = view->slot_subtitles[slot_index],
        /* PRODUCT_UI_VIRTUAL_ACTION_INJECTED */
        .interaction = PRODUCT_UI_ROW_INTERACTION_TOGGLE,
        .trailing = PRODUCT_UI_ROW_TRAILING_SWITCH,
        .is_enabled = 1U,
        .is_checked = event->completed,
    };
    return 0;
}

static void product_ui_schedule_row_action(uint16_t item_index,
                                           void *user_data)
{
    product_ui_schedule_view_t *view = user_data;
    product_ui_calendar_event_t item;
    product_ui_time_domain_command_t command;
    product_ui_time_domain_command_result_t result;

    if ((view == NULL) || (item_index >= view->summary.event_count) ||
        (product_ui_time_domain_get_event(
             view->sorted_indices[item_index], &item) != PRODUCT_UI_TIME_OK)) {
        return;
    }
    memset(&command, 0, sizeof(command));
    command.type = PRODUCT_UI_TIME_COMMAND_SET_EVENT_COMPLETED;
    command.data.event_completed.id = item.id;
    command.data.event_completed.completed = item.completed ? 0U : 1U;
    if (product_ui_time_domain_execute(&command, &result) == PRODUCT_UI_TIME_OK) {
        product_ui_toast_show(command.data.event_completed.completed ?
                              "日程已完成" : "日程已恢复", 1200U);
    } else {
        product_ui_toast_show("日程状态更新失败", 1200U);
    }
    s_schedule_view.refresh_pending = 1U;
}

static int product_ui_schedule_add_rows(lv_obj_t *content)
{
    product_ui_virtual_list_spec_t spec = {
        .row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_schedule_row_bind,
        .action_cb = product_ui_schedule_row_action,
        .user_data = &s_schedule_view,
    };

    if (product_ui_schedule_order_build() != 0) {
        return -1;
    }
    spec.item_count = s_schedule_view.summary.event_count;
    if (product_ui_virtual_list_create(content, &s_schedule_view.list, &spec) != 0) {
        return -1;
    }
    if (product_ui_section_create(s_schedule_view.list.header, "全部日程（本次启动有效）") == NULL) {
        return -1;
    }
    if (product_ui_virtual_list_refresh(&s_schedule_view.list) != 0) {
        return -1;
    }
    product_ui_virtual_list_scroll_to(&s_schedule_view.list,
                                      s_schedule_controller.first_visible);
    return 0;
}

static int product_ui_schedule_render_commit(int result,
                                             uint32_t service_revision)
{
    if (result == 0) {
        s_schedule_view.is_list_ready = s_schedule_view.list.root != NULL &&
            !s_schedule_view.list.render_pending;
        s_schedule_view.generation = s_schedule_view.summary.generation;
        s_schedule_view.service_revision = service_revision;
    }
    return result;
}

static int product_ui_schedule_render(uint8_t force)
{
    product_ui_time_result_e status;
    product_ui_service_snapshot_t service = {0};

    if (s_schedule_view.page.content == NULL) {
        return -1;
    }
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_EVENT, &service);
    if (service.state != PRODUCT_UI_SERVICE_READY) {
        if (!force &&
            (s_schedule_view.service_revision == service.revision)) {
            return 0;
        }
        if (s_schedule_view.list.root != NULL) {
            s_schedule_controller.first_visible =
                product_ui_virtual_list_first_visible(&s_schedule_view.list);
        }
        s_schedule_view.is_list_ready = 0U;
        lv_obj_clean(s_schedule_view.page.content);
        memset(&s_schedule_view.list, 0, sizeof(s_schedule_view.list));
        s_schedule_view.refresh_pending = 0U;
        return product_ui_schedule_render_commit(
            product_ui_feedback_create(
                s_schedule_view.page.content,
                service.state == PRODUCT_UI_SERVICE_UNKNOWN ?
                    PRODUCT_UI_FEEDBACK_LOADING : PRODUCT_UI_FEEDBACK_ERROR,
                "日程不可用",
                service.state == PRODUCT_UI_SERVICE_UNKNOWN ?
                    "等待事件服务就绪" : "当前固件未提供事件服务") !=
                NULL ? 0 : -1,
            service.revision);
    }
    status = product_ui_time_domain_get_summary(&s_schedule_view.summary);
    if ((status == PRODUCT_UI_TIME_OK) && !force &&
        (s_schedule_view.generation == s_schedule_view.summary.generation) &&
        (s_schedule_view.service_revision == service.revision)) {
        return 0;
    }

    if (status == PRODUCT_UI_TIME_OK &&
        s_schedule_view.is_list_ready && s_schedule_view.list.root != NULL &&
        s_schedule_view.summary.event_count != 0U) {
        s_schedule_view.refresh_pending = 0U;
        if (product_ui_schedule_order_build() != 0) {
            return -1;
        }
        return product_ui_schedule_render_commit(product_ui_virtual_list_set_count(
            &s_schedule_view.list, s_schedule_view.summary.event_count), service.revision);
    }
    if (s_schedule_view.list.root != NULL) {
        s_schedule_controller.first_visible =
            product_ui_virtual_list_first_visible(&s_schedule_view.list);
    }
    s_schedule_view.is_list_ready = 0U;
    lv_obj_clean(s_schedule_view.page.content);
    memset(&s_schedule_view.list, 0, sizeof(s_schedule_view.list));
    s_schedule_view.refresh_pending = 0U;
    if (status != PRODUCT_UI_TIME_OK) {
        return product_ui_feedback_create(s_schedule_view.page.content,
                                          PRODUCT_UI_FEEDBACK_ERROR,
                                          "日程读取失败",
                                          "事件快照当前不可用") != NULL ? 0 : -1;
    }
    if (s_schedule_view.summary.event_count == 0U) {
        return product_ui_schedule_render_commit(
            product_ui_empty_state_create(s_schedule_view.page.content,
                                          "还没有日程") != NULL ? 0 : -1,
            service.revision);
    }
    return product_ui_schedule_render_commit(
        product_ui_schedule_add_rows(s_schedule_view.page.content),
        service.revision);
}

int product_ui_schedule_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "日程表",
        .back_title = "桌面",
        .show_back = 1U,
    };

    if (parent == NULL) {
        return -1;
    }
    memset(&s_schedule_view, 0, sizeof(s_schedule_view));
    if (product_ui_page_frame_create_ex(parent, &spec,
                                        &s_schedule_view.page) != 0) {
        return -1;
    }
    lv_obj_set_flex_flow(s_schedule_view.page.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_schedule_view.page.content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_schedule_view.page.content,
                             PRODUCT_UI_SPACE_SM, 0);
    lv_obj_clear_flag(s_schedule_view.page.content,
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    return product_ui_schedule_render(1U);
}

static void product_ui_schedule_page_enter(void)
{
    product_ui_schedule_render(1U);
}

static void product_ui_schedule_page_destroy(void)
{
    if (s_schedule_view.list.root != NULL) {
        s_schedule_controller.first_visible =
            product_ui_virtual_list_first_visible(&s_schedule_view.list);
    }
    product_ui_page_view_reset(&s_schedule_view.page);
    memset(&s_schedule_view, 0, sizeof(s_schedule_view));
}

static void product_ui_schedule_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
    product_ui_virtual_list_update(&s_schedule_view.list, elapsed_ms);
    if (s_schedule_view.refresh_pending) {
        product_ui_schedule_render(1U);
        return;
    }
    s_schedule_view.refresh_elapsed_ms += elapsed_ms;
    if (s_schedule_view.refresh_elapsed_ms < PRODUCT_UI_SCHEDULE_REFRESH_MS) {
        return;
    }
    s_schedule_view.refresh_elapsed_ms %= PRODUCT_UI_SCHEDULE_REFRESH_MS;
    product_ui_schedule_render(0U);
}

static const page_ops_t s_product_ui_schedule_ops = {
    .create = product_ui_schedule_page_create,
    .enter = product_ui_schedule_page_enter,
    .destroy = product_ui_schedule_page_destroy,
    .update = product_ui_schedule_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_product_ui_schedule_desc = {
    .name = "ProductSchedule",
    .ops = &s_product_ui_schedule_ops,
};

int product_ui_schedule_register_page(int page_type)
{
    s_product_ui_schedule_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_schedule_desc);
}

D_UI_MODULE_DEFINE(schedule, "日程表", LV_SYMBOL_LIST,
                product_ui_schedule_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
