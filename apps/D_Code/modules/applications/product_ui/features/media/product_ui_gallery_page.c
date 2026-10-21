/**
 * @file product_ui_gallery_page.c
 * @brief Bounded Product UI gallery media-list page.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/media/product_ui_gallery_page.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/features/media/product_ui_media_storage.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

#define PRODUCT_UI_GALLERY_LIST_LIMIT       10U
#define PRODUCT_UI_GALLERY_REFRESH_MS      250U

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
    uint32_t refresh_elapsed_ms;
    uint16_t list_first;
    uint16_t list_count;
    uint8_t is_rendered;
} product_ui_gallery_page_state_t;

static product_ui_gallery_page_state_t s_gallery_page;
static uint16_t s_gallery_first_visible;

static const char *product_ui_gallery_item_title(
    const product_ui_media_item_t *item)
{
    if (item->title[0] != '\0') {
        return item->title;
    }
    if (item->path[0] != '\0') {
        return item->path;
    }
    return "元数据不可用";
}

static const char *product_ui_gallery_item_subtitle(
    const product_ui_media_item_t *item)
{
    if (item->path[0] != '\0') {
        return item->path;
    }
    return "无可用文件信息";
}

static int product_ui_gallery_feedback(product_ui_feedback_state_e state,
                                       const char *title,
                                       const char *message)
{
    return product_ui_feedback_create(s_gallery_page.page.content, state, title,
                                      message) == NULL ? -1 : 0;
}

static int product_ui_gallery_storage_state_render(void)
{
    const product_ui_media_storage_summary_t *summary =
        &s_gallery_page.summary;

    switch (summary->sd_capability) {
    case PRODUCT_UI_SD_CAPABILITY_UNKNOWN:
        return product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_LOADING,
                                           "SD 状态未知",
                                           "等待存储能力确认");
    case PRODUCT_UI_SD_CAPABILITY_UNAVAILABLE:
        return product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                           "SD 不可用",
                                           "当前没有可用的图片存储");
    case PRODUCT_UI_SD_CAPABILITY_ERROR:
        return product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                           "SD 错误",
                                           "存储服务报告读取错误");
    case PRODUCT_UI_SD_CAPABILITY_READY:
        break;
    default:
        return product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                           "SD 状态无效",
                                           "无法读取图片存储状态");
    }
    if (!summary->media_present) {
        return product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                           "SD 不可用",
                                           "未检测到可用存储介质");
    }
    return 1;
}

static void product_ui_gallery_row_prepare(product_ui_list_item_t *row,
                                           const product_ui_media_item_t *item)
{
    row->item_id = "gallery_item";
    row->title = product_ui_gallery_item_title(item);
    row->icon_id = PRODUCT_UI_ICON_GALLERY;
    row->accent = PRODUCT_UI_COLOR_ACCENT;
}

static int product_ui_gallery_current_render(const product_ui_media_item_t *item)
{
    product_ui_list_item_t row;
    product_ui_row_spec_t row_spec;

    product_ui_gallery_row_prepare(&row, item);
    row_spec = (product_ui_row_spec_t) {
        .item = &row,
        .subtitle = product_ui_gallery_item_subtitle(item),
        .interaction = PRODUCT_UI_ROW_INTERACTION_NONE,
        .trailing = PRODUCT_UI_ROW_TRAILING_NONE,
        .is_enabled = 1U,
    };
    if (s_gallery_page.current_row != NULL) {
        return product_ui_list_row_rebind(s_gallery_page.current_row, &row_spec);
    }
    if (product_ui_section_create(s_gallery_page.list.header, "当前项") == NULL) {
        return -1;
    }
    s_gallery_page.current_row = product_ui_list_row_create(s_gallery_page.list.header, &row_spec);
    return s_gallery_page.current_row != NULL ? 0 : -1;
}

static int product_ui_gallery_row_bind(uint16_t item_index,
                                       uint8_t slot_index,
                                       product_ui_row_spec_t *row_spec,
                                       void *user_data);

static void product_ui_gallery_list_range(uint16_t *first, uint16_t *count)
{
    product_ui_media_page_index_t page_index;

    *first = 0U;
    *count = s_gallery_page.summary.queue_count;
    if ((s_gallery_page.summary.page_cache_count > 0U) &&
        (product_ui_media_storage_page_index_get(0U, &page_index) == D_OK)) {
        if (page_index.item_count > 0U) {
            *first = page_index.first_queue_index;
            *count = page_index.item_count;
        }
    }
    if (*count > PRODUCT_UI_GALLERY_LIST_LIMIT) {
        *count = PRODUCT_UI_GALLERY_LIST_LIMIT;
    }
}

static int product_ui_gallery_list_render(const product_ui_media_item_t *current)
{
    product_ui_virtual_list_spec_t spec = {
        .row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_gallery_row_bind,
        .user_data = &s_gallery_page,
    };

    product_ui_gallery_list_range(&s_gallery_page.list_first, &s_gallery_page.list_count);
    spec.item_count = s_gallery_page.list_count;
    if (product_ui_virtual_list_create(s_gallery_page.page.content,
                                          &s_gallery_page.list, &spec) != 0) {
        return -1;
    }
    if ((product_ui_gallery_current_render(current) != 0) ||
        (product_ui_section_create(s_gallery_page.list.header, "图片列表") == NULL) ||
        (product_ui_virtual_list_refresh(&s_gallery_page.list) != 0)) {
        return -1;
    }
    product_ui_virtual_list_scroll_to(&s_gallery_page.list, s_gallery_first_visible);
    return 0;
}

static int product_ui_gallery_row_bind(uint16_t item_index,
                                       uint8_t slot_index,
                                       product_ui_row_spec_t *row_spec,
                                       void *user_data)
{
    product_ui_gallery_page_state_t *page = user_data;
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
    product_ui_gallery_row_prepare(row, item);
    *row_spec = (product_ui_row_spec_t) {
        .item_key = item->media_id,
        .item = row,
        .subtitle = product_ui_gallery_item_subtitle(item),
        .interaction = PRODUCT_UI_ROW_INTERACTION_NONE,
        .trailing = PRODUCT_UI_ROW_TRAILING_NONE,
        .is_enabled = 1U,
    };
    return 0;
}

static int product_ui_gallery_snapshot_render(void)
{
    int state_result;
    product_ui_media_item_t current;

    state_result = product_ui_gallery_storage_state_render();
    if (state_result <= 0) {
        return state_result;
    }
    if (s_gallery_page.summary.queue_count == 0U) {
        return product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_EMPTY,
                                           "没有可显示媒体",
                                           "媒体队列为空");
    }
    if (s_gallery_page.summary.current_queue_index >= s_gallery_page.summary.queue_count ||
        product_ui_media_storage_queue_item_get(s_gallery_page.summary.current_queue_index,
                                                 &current) != D_OK) {
        return product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                           "当前项不可用", "媒体队列索引无效");
    }
    return product_ui_gallery_list_render(&current);
}

static int product_ui_gallery_refresh(void)
{
    product_ui_media_item_t current;
    product_ui_service_snapshot_t storage_service = {0};
    d_err_t result;
    uint32_t version = 0U;
    uint8_t registry_ready;

    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_STORAGE,
                                      &storage_service);
    registry_ready =
        (uint8_t)(storage_service.state == PRODUCT_UI_SERVICE_READY);
    result = registry_ready ?
             product_ui_media_storage_summary_get(&s_gallery_page.summary) :
             (d_err_t)-1;
    if (registry_ready && (result == D_OK)) {
        version = s_gallery_page.summary.version;
    }
    if (s_gallery_page.is_rendered &&
        (result == s_gallery_page.rendered_result) &&
        ((result != D_OK) || (version == s_gallery_page.rendered_version)) &&
        (storage_service.revision ==
         s_gallery_page.rendered_storage_revision)) {
        return 0;
    }

    if (registry_ready && result == D_OK && s_gallery_page.is_rendered &&
        s_gallery_page.list.root != NULL && s_gallery_page.current_row != NULL &&
        s_gallery_page.summary.sd_capability == PRODUCT_UI_SD_CAPABILITY_READY &&
        s_gallery_page.summary.media_present &&
        s_gallery_page.summary.current_queue_index < s_gallery_page.summary.queue_count &&
        product_ui_media_storage_queue_item_get(s_gallery_page.summary.current_queue_index, &current) == D_OK) {
        product_ui_gallery_list_range(&s_gallery_page.list_first, &s_gallery_page.list_count);
        if (product_ui_gallery_current_render(&current) != 0 ||
            product_ui_virtual_list_set_count(&s_gallery_page.list, s_gallery_page.list_count) != 0) {
            return -1;
        }
    } else {
        if (s_gallery_page.list.root != NULL) {
            s_gallery_first_visible = product_ui_virtual_list_first_visible(
                &s_gallery_page.list);
        }
        s_gallery_page.current_row = NULL;
        lv_obj_clean(s_gallery_page.page.content);
        memset(&s_gallery_page.list, 0, sizeof(s_gallery_page.list));
        if (!registry_ready) {
            if (product_ui_gallery_feedback(
                    storage_service.state == PRODUCT_UI_SERVICE_UNKNOWN ?
                        PRODUCT_UI_FEEDBACK_LOADING : PRODUCT_UI_FEEDBACK_ERROR,
                    "相册不可用",
                    "等待真实存储服务接入；查看器尚未启用") != 0) {
                return -1;
            }
        } else if (result != D_OK) {
            if (product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                            "媒体服务不可用",
                                            "媒体存储状态尚未就绪") != 0) {
                return -1;
            }
        } else if (product_ui_gallery_snapshot_render() != 0) {
            return -1;
        }
    }
    s_gallery_page.rendered_result = result;
    s_gallery_page.rendered_version = version;
    s_gallery_page.rendered_storage_revision = storage_service.revision;
    s_gallery_page.is_rendered = 1U;
    return 0;
}

int product_ui_gallery_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "相册",
        .back_title = "桌面",
        .show_back = 1,
    };

    memset(&s_gallery_page, 0, sizeof(s_gallery_page));
    if (product_ui_page_frame_create_ex(parent, &spec,
                                        &s_gallery_page.page) != 0) {
        return -1;
    }
    lv_obj_clear_flag(s_gallery_page.page.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_gallery_page.page.content,
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(s_gallery_page.page.content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_gallery_page.page.content,
                              LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_gallery_page.page.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_gallery_page.page.content,
                             PRODUCT_UI_SPACE_SM, 0);
    return product_ui_gallery_refresh();
}

static void product_ui_gallery_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
    product_ui_virtual_list_update(&s_gallery_page.list, elapsed_ms);
    s_gallery_page.refresh_elapsed_ms += elapsed_ms;
    if (s_gallery_page.refresh_elapsed_ms < PRODUCT_UI_GALLERY_REFRESH_MS) {
        return;
    }
    s_gallery_page.refresh_elapsed_ms = 0U;
    (void)product_ui_gallery_refresh();
}

static void product_ui_gallery_page_destroy(void)
{
    if (s_gallery_page.list.root != NULL) {
        s_gallery_first_visible = product_ui_virtual_list_first_visible(
            &s_gallery_page.list);
    }
    product_ui_page_view_reset(&s_gallery_page.page);
    memset(&s_gallery_page, 0, sizeof(s_gallery_page));
}

static const page_ops_t s_product_ui_gallery_ops = {
    .create = product_ui_gallery_page_create,
    .destroy = product_ui_gallery_page_destroy,
    .update = product_ui_gallery_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_product_ui_gallery_desc = {
    .name = "ProductGallery",
    .ops = &s_product_ui_gallery_ops,
};

int product_ui_gallery_register_page(int page_type)
{
    s_product_ui_gallery_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_gallery_desc);
}

D_UI_MODULE_DEFINE_ID(product_gallery, "gallery", "相册", LV_SYMBOL_IMAGE,
                   product_ui_gallery_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
