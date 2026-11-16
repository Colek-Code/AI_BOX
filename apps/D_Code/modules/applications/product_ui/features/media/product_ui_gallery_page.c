/** @file product_ui_gallery_page.c @brief 图库的有限媒体列表投影及可重试页面生命周期。 */

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

/* 显示窗口及其行数据为页面副本，与媒体容器的生命周期分开。 */
typedef struct {
    product_ui_media_item_t items[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    product_ui_list_item_t rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    uint16_t first;
    uint16_t count;
} product_ui_gallery_window_t;

/* 最近一次完整呈现的身份，不能证明部分创建控件已经存在。 */
typedef struct {
    d_err_t result;
    uint32_t source_version;
    uint32_t storage_revision;
    uint8_t is_valid;
} product_ui_gallery_render_stamp_t;

typedef struct {
    product_ui_page_view_t page;
    product_ui_virtual_list_t list;
    lv_obj_t *current_row;
    product_ui_media_storage_summary_t summary;
    product_ui_gallery_window_t window;
    product_ui_gallery_render_stamp_t rendered;
    uint32_t refresh_elapsed_ms;
} product_ui_gallery_page_state_t;

static product_ui_gallery_page_state_t s_gallery_page;
static product_ui_row_text_cache_t s_gallery_row_text[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
static product_ui_row_text_cache_t s_gallery_current_text;
/* 滚动目标跨页面销毁保留，控件及行数据副本不保留。 */
static uint16_t s_gallery_first_visible;

/**
 * @brief 选择条目标题，空标题时回退路径及静态缺省文字。
 * @param[in] item 调用链保证非空的条目副本。
 * @return 条目内或静态字符串的借用指针。
 */
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

/**
 * @brief 选择条目路径作为副标题，空路径时使用缺省文字。
 * @param[in] item 调用链保证非空的条目副本。
 * @return 条目内路径或静态缺省文字的借用指针。
 */
static const char *product_ui_gallery_item_subtitle(
    const product_ui_media_item_t *item)
{
    if (item->path[0] != '\0') {
        return item->path;
    }
    return "无可用文件信息";
}

/**
 * @brief 在图库内容区创建状态反馈面板。
 * @param[in] state 加载、空或错误反馈类型。
 * @param[in] title 本次传给控件的标题文字。
 * @param[in] message 本次传给控件的说明文字。
 * @return 创建成功返回 0，失败返回 -1。
 */
static int product_ui_gallery_feedback(product_ui_feedback_state_e state,
                                       const char *title,
                                       const char *message)
{
    return product_ui_feedback_create(s_gallery_page.page.content, state, title,
                                      message) == NULL ? -1 : 0;
}

/**
 * @brief 按真实 SD 能力及存在状态创建反馈，或允许继续渲染列表。
 * @return 允许继续列表返回 1；反馈创建成功返回 0，创建失败返回 -1。
 */
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

/**
 * @brief 用条目借用文字填写图库行的图标及主题描述。
 * @param[out] row 接收行规格，文字仍借用 item 或静态字符串。
 * @param[in] item 须覆盖行绑定消费期间的条目副本。
 */
static void product_ui_gallery_row_prepare(product_ui_list_item_t *row,
                                           const product_ui_media_item_t *item)
{
    row->item_id = "gallery_item";
    row->title = product_ui_gallery_item_title(item);
    row->icon_id = PRODUCT_UI_ICON_GALLERY;
    row->accent = PRODUCT_UI_COLOR_ACCENT;
}

/**
 * @brief 复用或创建只读当前条目行，使用稳定媒体编号绑定文本缓存。
 * @param[in] item 本次借用的当前条目副本。
 * @return 创建或重绑成功返回 0，任一步失败返回 -1。
 */
static int product_ui_gallery_current_render(const product_ui_media_item_t *item)
{
    product_ui_list_item_t row;
    product_ui_row_spec_t row_spec;

    product_ui_gallery_row_prepare(&row, item);
    row_spec = (product_ui_row_spec_t) {
        .item_key = item->media_id,
        .item = &row,
        .subtitle = product_ui_gallery_item_subtitle(item),
        .interaction = PRODUCT_UI_ROW_INTERACTION_NONE,
        .trailing = PRODUCT_UI_ROW_TRAILING_NONE,
        .is_enabled = 1U,
        .appearance = PRODUCT_UI_ROW_APPEARANCE_INSET,
        .text_cache = &s_gallery_current_text,
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

/**
 * @brief 从当前队列及首个有效页索引确定最多十项的显示窗口。
 * @param[out] first 调用方保证非空，接收队列起点。
 * @param[out] count 调用方保证非空，接收限制后的条目数。
 */
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

/**
 * @brief 创建有限复用列表、当前项和分组标题，恢复保存的滚动目标。
 * @param[in] current 本次借用的当前媒体条目。
 * @return 完整创建和刷新返回 0，任一步失败返回 -1，部分对象留给内容清理路径。
 */
static int product_ui_gallery_list_render(const product_ui_media_item_t *current)
{
    product_ui_virtual_list_spec_t spec = {
        .row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_gallery_row_bind,
        .user_data = &s_gallery_page,
        .text_caches = s_gallery_row_text,
    };

    product_ui_gallery_list_range(&s_gallery_page.window.first, &s_gallery_page.window.count);
    spec.item_count = s_gallery_page.window.count;
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
    memset(s_gallery_row_text, 0, sizeof(s_gallery_row_text));
    memset(&s_gallery_current_text, 0, sizeof(s_gallery_current_text));
    return 0;
}

/**
 * @brief 把逻辑窗口条目复制到固定槽，再生成只读行规格。
 * @param[in] item_index 当前窗口内的逻辑下标。
 * @param[in] slot_index 固定复用槽下标。
 * @param[out] row_spec 接收借用该槽条目及文字的行描述。
 * @param[in,out] user_data 页面状态，槽内条目及行描述会被更新。
 * @return 成功返回 0；上下文、下标或队列读取失败返回 -1。
 */
static int product_ui_gallery_row_bind(uint16_t item_index,
                                       uint8_t slot_index,
                                       product_ui_row_spec_t *row_spec,
                                       void *user_data)
{
    product_ui_gallery_page_state_t *page = user_data;
    product_ui_media_item_t *item;
    product_ui_list_item_t *row;

    if ((page == NULL) || (slot_index >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) ||
        (item_index >= page->window.count)) {
        return -1;
    }
    item = &page->window.items[slot_index];
    row = &page->window.rows[slot_index];
    if (product_ui_media_storage_queue_item_get(
            (uint16_t)(page->window.first + item_index), item) != D_OK) {
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
        .appearance = PRODUCT_UI_ROW_APPEARANCE_INSET,
    };
    return 0;
}

/**
 * @brief 根据储存状态、队列和当前下标选择反馈或实际列表。
 * @return 呈现成功返回 0，任一反馈或列表创建失败返回 -1。
 */
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

/**
 * @brief 使完成呈现标记失效，删除内容子对象并重置控件及文本缓存。
 * @note 保留独立保存的滚动目标；清空后的控件状态不能由旧呈现标记证明有效。
 */
static void product_ui_gallery_content_clear(void)
{
    s_gallery_page.rendered.is_valid = 0U;
    s_gallery_page.current_row = NULL;
    lv_obj_clean(s_gallery_page.page.content);
    memset(&s_gallery_page.list, 0, sizeof(s_gallery_page.list));
    memset(s_gallery_row_text, 0, sizeof(s_gallery_row_text));
    memset(&s_gallery_current_text, 0, sizeof(s_gallery_current_text));
}

/**
 * @brief 完整控件可复用时重绑，否则从空内容重建反馈或列表。
 * @param[in] registry_ready 存储域是否已发布 READY。
 * @param[in] source_result 读取媒体摘要的结果。
 * @param[in] registry_state 未就绪时用于选择加载或错误反馈的服务状态。
 * @return 成功返回 0；重绑或重建失败返回 -1。
 * @note 重绑失败保留完整控件供后续再试；重建失败清空部分对象，下一轮从空内容开始。
 */
static int product_ui_gallery_content_refresh(uint8_t registry_ready,
                                              d_err_t source_result,
                                              product_ui_service_state_e registry_state)
{
    product_ui_media_item_t current;
    int render_result;
    if (registry_ready && source_result == D_OK && s_gallery_page.rendered.is_valid &&
        s_gallery_page.list.root != NULL && s_gallery_page.current_row != NULL &&
        s_gallery_page.summary.sd_capability == PRODUCT_UI_SD_CAPABILITY_READY &&
        s_gallery_page.summary.media_present &&
        s_gallery_page.summary.current_queue_index < s_gallery_page.summary.queue_count &&
        product_ui_media_storage_queue_item_get(s_gallery_page.summary.current_queue_index, &current) == D_OK) {
        product_ui_gallery_list_range(&s_gallery_page.window.first, &s_gallery_page.window.count);
        if (product_ui_gallery_current_render(&current) != 0 ||
            product_ui_virtual_list_set_count(&s_gallery_page.list, s_gallery_page.window.count) != 0) {
            return -1;
        }
    } else {
        if (s_gallery_page.list.root != NULL) {
            s_gallery_first_visible = product_ui_virtual_list_first_visible(
                &s_gallery_page.list);
        }
        product_ui_gallery_content_clear();
        if (!registry_ready) {
            render_result = product_ui_gallery_feedback(
                    registry_state == PRODUCT_UI_SERVICE_UNKNOWN ?
                        PRODUCT_UI_FEEDBACK_LOADING : PRODUCT_UI_FEEDBACK_ERROR,
                    "相册不可用",
                    "等待真实存储服务接入；查看器尚未启用");
        } else if (source_result != D_OK) {
            render_result = product_ui_gallery_feedback(PRODUCT_UI_FEEDBACK_ERROR,
                                            "媒体服务不可用",
                                            "媒体存储状态尚未就绪");
        } else {
            render_result = product_ui_gallery_snapshot_render();
        }
        if (render_result != 0) {
            product_ui_gallery_content_clear();
            return -1;
        }
    }
    return 0;
}

/**
 * @brief 以来源版本及存储服务代次检查完成呈现标记，失败不提交新标记。
 * @return 已有有效相同内容或本次呈现成功返回 0，呈现失败返回 -1。
 */
static int product_ui_gallery_refresh(void)
{
    product_ui_service_snapshot_t storage_service = {0};
    d_err_t source_result;
    uint32_t version = 0U;
    uint8_t registry_ready;

    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_STORAGE,
                                      &storage_service);
    registry_ready =
        (uint8_t)(storage_service.state == PRODUCT_UI_SERVICE_READY);
    source_result = registry_ready ?
             product_ui_media_storage_summary_get(&s_gallery_page.summary) :
             (d_err_t)-1;
    if (registry_ready && (source_result == D_OK)) {
        version = s_gallery_page.summary.version;
    }
    if (s_gallery_page.rendered.is_valid &&
        (source_result == s_gallery_page.rendered.result) &&
        ((source_result != D_OK) || (version == s_gallery_page.rendered.source_version)) &&
        (storage_service.revision ==
         s_gallery_page.rendered.storage_revision)) {
        return 0;
    }

    if (product_ui_gallery_content_refresh(registry_ready, source_result, storage_service.state) != 0) {
        return -1;
    }
    s_gallery_page.rendered.result = source_result;
    s_gallery_page.rendered.source_version = version;
    s_gallery_page.rendered.storage_revision = storage_service.revision;
    s_gallery_page.rendered.is_valid = 1U;
    return 0;
}

/**
 * @brief 创建图库页面框架并根据真实存储投影呈现首屏。
 */
int product_ui_gallery_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "相册",
        .back_title = "桌面",
        .show_back = 1,
        .role = PRODUCT_UI_PAGE_ROLE_CONTENT,
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

/**
 * @brief 推进公共 UI 与复用列表，并按周期重试图库内容刷新。
 * @param[in] elapsed_ms 本轮页面经过的毫秒数。
 */
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

/**
 * @brief 保存滚动位置后解除页面状态及文本缓存，不改变媒体容器。
 * @note LVGL 对象由页面管理器删除，条目数据仅为可丢弃的页面副本。
 */
static void product_ui_gallery_page_destroy(void)
{
    if (s_gallery_page.list.root != NULL) {
        s_gallery_first_visible = product_ui_virtual_list_first_visible(
            &s_gallery_page.list);
    }
    product_ui_page_view_reset(&s_gallery_page.page);
    memset(&s_gallery_page, 0, sizeof(s_gallery_page));
    memset(s_gallery_row_text, 0, sizeof(s_gallery_row_text));
    memset(&s_gallery_current_text, 0, sizeof(s_gallery_current_text));
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

/**
 * @brief 将静态图库描述注册到页面管理器。
 */
int product_ui_gallery_register_page(int page_type)
{
    s_product_ui_gallery_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_gallery_desc);
}

D_UI_MODULE_DEFINE_ID(product_gallery, "gallery", "相册", LV_SYMBOL_IMAGE,
                   product_ui_gallery_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
