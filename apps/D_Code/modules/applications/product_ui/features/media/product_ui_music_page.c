/**
 * @file product_ui_music_page.c
 * @brief Bounded Product UI music queue page.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/media/product_ui_music_page.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/features/media/product_ui_media_storage.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

#define PRODUCT_UI_MUSIC_LIST_LIMIT       10U
#define PRODUCT_UI_MUSIC_REFRESH_MS      250U

typedef struct {
    product_ui_page_view_t page;
    product_ui_virtual_list_t list;
    lv_obj_t *current_row;
    product_ui_media_storage_summary_t summary;
    product_ui_media_item_t slot_items[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    product_ui_list_item_t slot_rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    d_err_t rendered_result;
    uint32_t rendered_version;
    uint32_t rendered_storage_revision;
    uint32_t rendered_media_revision;
    uint32_t refresh_elapsed_ms;
    uint16_t list_first;
    uint16_t list_count;
    uint8_t is_rendered;
} product_ui_music_page_state_t;

static product_ui_music_page_state_t s_music_page;
static uint16_t s_music_first_visible;

static uint8_t product_ui_music_audio_available(void)
{
#ifdef CONFIG_AUDIO_ENABLE
    return 1U;
#else
    return 0U;
#endif
}

static const char *product_ui_music_item_title(
    const product_ui_media_item_t *item)
{
    if (item->title[0] != '\0') {
        return item->title;
    }
    return "元数据不可用";
}

static const char *product_ui_music_item_subtitle(
    const product_ui_media_item_t *item)
{
    if (item->artist[0] != '\0') {
        return item->artist;
    }
    if (item->path[0] != '\0') {
        return item->path;
    }
    return "无可用详情";
}

static int product_ui_music_feedback(product_ui_feedback_state_e state,
                                     const char *title,
                                     const char *message)
{
    return product_ui_feedback_create(s_music_page.page.content, state, title,
                                      message) == NULL ? -1 : 0;
}

static int product_ui_music_storage_state_render(void)
{
    const product_ui_media_storage_summary_t *summary = &s_music_page.summary;

    switch (summary->sd_capability) {
    case PRODUCT_UI_SD_CAPABILITY_UNKNOWN:
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_LOADING,
                                         "SD 状态未知",
                                         "等待存储能力确认");
    case PRODUCT_UI_SD_CAPABILITY_UNAVAILABLE:
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                         "SD 不可用",
                                         "当前没有可用的媒体存储");
    case PRODUCT_UI_SD_CAPABILITY_ERROR:
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                         "SD 错误",
                                         "存储服务报告读取错误");
    case PRODUCT_UI_SD_CAPABILITY_READY:
        break;
    default:
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                         "SD 状态无效",
                                         "无法读取媒体存储状态");
    }
    if (!summary->media_present) {
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                         "SD 不可用",
                                         "未检测到可用存储介质");
    }
    return 1;
}

static void product_ui_music_row_prepare(product_ui_list_item_t *row,
                                         const product_ui_media_item_t *item)
{
    row->item_id = "music_item";
    row->title = product_ui_music_item_title(item);
    row->icon_id = PRODUCT_UI_ICON_MUSIC;
    row->accent = PRODUCT_UI_COLOR_DANGER;
}

static int product_ui_music_current_render(const product_ui_media_item_t *item)
{
    product_ui_list_item_t row;
    product_ui_row_spec_t row_spec;

    product_ui_music_row_prepare(&row, item);
    row_spec = (product_ui_row_spec_t) {
        .item = &row,
        .subtitle = product_ui_music_item_subtitle(item),
        .interaction = PRODUCT_UI_ROW_INTERACTION_NONE,
        .trailing = PRODUCT_UI_ROW_TRAILING_NONE,
        .is_enabled = 1U,
    };
    if (s_music_page.current_row != NULL) {
        return product_ui_list_row_rebind(s_music_page.current_row, &row_spec);
    }
    if (product_ui_section_create(s_music_page.list.header, "当前项") == NULL) {
        return -1;
    }
    s_music_page.current_row = product_ui_list_row_create(s_music_page.list.header, &row_spec);
    return s_music_page.current_row != NULL ? 0 : -1;
}

static int product_ui_music_row_bind(uint16_t item_index,
                                     uint8_t slot_index,
                                     product_ui_row_spec_t *row_spec,
                                     void *user_data);

static void product_ui_music_list_range(uint16_t *first, uint16_t *count)
{
    product_ui_media_page_index_t page_index;

    *first = 0U;
    *count = s_music_page.summary.queue_count;
    if ((s_music_page.summary.page_cache_count > 0U) &&
        (product_ui_media_storage_page_index_get(0U, &page_index) == D_OK)) {
        if (page_index.item_count > 0U) {
            *first = page_index.first_queue_index;
            *count = page_index.item_count;
        }
    }
    if (*count > PRODUCT_UI_MUSIC_LIST_LIMIT) {
        *count = PRODUCT_UI_MUSIC_LIST_LIMIT;
    }
}

static int product_ui_music_queue_render(const product_ui_media_item_t *current)
{
    product_ui_virtual_list_spec_t spec = {
        .row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_music_row_bind,
        .user_data = &s_music_page,
    };

    product_ui_music_list_range(&s_music_page.list_first, &s_music_page.list_count);
    spec.item_count = s_music_page.list_count;
    if (product_ui_virtual_list_create(s_music_page.page.content,
                                          &s_music_page.list, &spec) != 0) {
        return -1;
    }
    if ((product_ui_music_current_render(current) != 0) ||
        (product_ui_section_create(s_music_page.list.header, "媒体队列") == NULL) ||
        (product_ui_virtual_list_refresh(&s_music_page.list) != 0)) {
        return -1;
    }
    product_ui_virtual_list_scroll_to(&s_music_page.list, s_music_first_visible);
    return 0;
}

static int product_ui_music_row_bind(uint16_t item_index,
                                     uint8_t slot_index,
                                     product_ui_row_spec_t *row_spec,
                                     void *user_data)
{
    product_ui_music_page_state_t *page = user_data;
    product_ui_media_item_t *item;
    product_ui_list_item_t *row;

    if ((page == NULL) || (slot_index >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) ||
        (item_index >= page->list_count)) {
        return -1;
    }
    item = &page->slot_items[slot_index];
    row = &page->slot_rows[slot_index];
    if (product_ui_media_storage_queue_item_get(
            (uint16_t)(page->list_first + item_index), item) != D_OK) {
        return -1;
    }
    product_ui_music_row_prepare(row, item);
    *row_spec = (product_ui_row_spec_t) {
        .item_key = item->media_id,
        .item = row,
        .subtitle = product_ui_music_item_subtitle(item),
        .interaction = PRODUCT_UI_ROW_INTERACTION_NONE,
        .trailing = PRODUCT_UI_ROW_TRAILING_NONE,
        .is_enabled = 1U,
    };
    return 0;
}

static int product_ui_music_snapshot_render(void)
{
    int state_result;
    product_ui_media_item_t current;

    state_result = product_ui_music_storage_state_render();
    if (state_result <= 0) {
        return state_result;
    }
    if (!product_ui_music_audio_available()) {
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                         "音频不可用",
                                         "当前固件未启用音频能力");
    }
    if (s_music_page.summary.queue_count == 0U) {
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_EMPTY,
                                         "没有可播放媒体",
                                         "媒体队列为空");
    }
    if (s_music_page.summary.current_queue_index >= s_music_page.summary.queue_count ||
        product_ui_media_storage_queue_item_get(s_music_page.summary.current_queue_index,
                                                 &current) != D_OK) {
        return product_ui_music_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                           "当前项不可用", "媒体队列索引无效");
    }
    return product_ui_music_queue_render(&current);
}

static int product_ui_music_refresh(void)
{
    product_ui_media_item_t current;
    product_ui_service_snapshot_t storage_service = {0};
    product_ui_service_snapshot_t media_service = {0};
    d_err_t result;
    uint32_t version = 0U;
    uint8_t registry_ready;

    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_STORAGE,
                                      &storage_service);
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_MEDIA,
                                      &media_service);
    registry_ready =
        (uint8_t)((storage_service.state == PRODUCT_UI_SERVICE_READY) &&
                  (media_service.state == PRODUCT_UI_SERVICE_READY));
    result = registry_ready ?
             product_ui_media_storage_summary_get(&s_music_page.summary) :
             (d_err_t)-1;
    if (registry_ready && (result == D_OK)) {
        version = s_music_page.summary.version;
    }
    if (s_music_page.is_rendered &&
        (result == s_music_page.rendered_result) &&
        ((result != D_OK) || (version == s_music_page.rendered_version)) &&
        (storage_service.revision ==
         s_music_page.rendered_storage_revision) &&
        (media_service.revision == s_music_page.rendered_media_revision)) {
        return 0;
    }

    if (registry_ready && result == D_OK && s_music_page.is_rendered &&
        s_music_page.list.root != NULL && s_music_page.current_row != NULL &&
        s_music_page.summary.sd_capability == PRODUCT_UI_SD_CAPABILITY_READY &&
        s_music_page.summary.media_present && product_ui_music_audio_available() &&
        s_music_page.summary.current_queue_index < s_music_page.summary.queue_count &&
        product_ui_media_storage_queue_item_get(s_music_page.summary.current_queue_index, &current) == D_OK) {
        product_ui_music_list_range(&s_music_page.list_first, &s_music_page.list_count);
        if (product_ui_music_current_render(&current) != 0 ||
            product_ui_virtual_list_set_count(&s_music_page.list, s_music_page.list_count) != 0) {
            return -1;
        }
    } else {
        if (s_music_page.list.root != NULL) {
            s_music_first_visible = product_ui_virtual_list_first_visible(
                &s_music_page.list);
        }
        s_music_page.current_row = NULL;
        lv_obj_clean(s_music_page.page.content);
        memset(&s_music_page.list, 0, sizeof(s_music_page.list));
        if (!registry_ready) {
            if (product_ui_music_feedback(
                    ((storage_service.state == PRODUCT_UI_SERVICE_UNKNOWN) ||
                     (media_service.state == PRODUCT_UI_SERVICE_UNKNOWN)) ?
                        PRODUCT_UI_FEEDBACK_LOADING : PRODUCT_UI_FEEDBACK_ERROR,
                    "音乐不可用",
                    "等待真实存储与播放服务接入") != 0) {
                return -1;
            }
        } else if (result != D_OK) {
            if (product_ui_music_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                          "媒体服务不可用",
                                          "媒体存储状态尚未就绪") != 0) {
                return -1;
            }
        } else if (product_ui_music_snapshot_render() != 0) {
            return -1;
        }
    }
    s_music_page.rendered_result = result;
    s_music_page.rendered_version = version;
    s_music_page.rendered_storage_revision = storage_service.revision;
    s_music_page.rendered_media_revision = media_service.revision;
    s_music_page.is_rendered = 1U;
    return 0;
}

int product_ui_music_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "音乐",
        .back_title = "桌面",
        .show_back = 1,
    };

    memset(&s_music_page, 0, sizeof(s_music_page));
    if (product_ui_page_frame_create_ex(parent, &spec,
                                        &s_music_page.page) != 0) {
        return -1;
    }
    lv_obj_clear_flag(s_music_page.page.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_music_page.page.content,
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(s_music_page.page.content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_music_page.page.content,
                              LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_music_page.page.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_music_page.page.content,
                             PRODUCT_UI_SPACE_SM, 0);
    return product_ui_music_refresh();
}

static void product_ui_music_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
    product_ui_virtual_list_update(&s_music_page.list, elapsed_ms);
    s_music_page.refresh_elapsed_ms += elapsed_ms;
    if (s_music_page.refresh_elapsed_ms < PRODUCT_UI_MUSIC_REFRESH_MS) {
        return;
    }
    s_music_page.refresh_elapsed_ms = 0U;
    (void)product_ui_music_refresh();
}

static void product_ui_music_page_destroy(void)
{
    if (s_music_page.list.root != NULL) {
        s_music_first_visible = product_ui_virtual_list_first_visible(
            &s_music_page.list);
    }
    product_ui_page_view_reset(&s_music_page.page);
    memset(&s_music_page, 0, sizeof(s_music_page));
}

static const page_ops_t s_product_ui_music_ops = {
    .create = product_ui_music_page_create,
    .destroy = product_ui_music_page_destroy,
    .update = product_ui_music_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_product_ui_music_desc = {
    .name = "ProductMusic",
    .ops = &s_product_ui_music_ops,
};

int product_ui_music_register_page(int page_type)
{
    s_product_ui_music_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_music_desc);
}

D_UI_MODULE_DEFINE(music, "音乐", LV_SYMBOL_AUDIO,
                product_ui_music_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
