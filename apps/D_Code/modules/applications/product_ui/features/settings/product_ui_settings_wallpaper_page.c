/** @file product_ui_settings_wallpaper_page.c @brief 壁纸草稿选择与持久化确认的页面投影。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_settings_details.h"
#include "product_ui_settings_details_common.h"
#include "modules/applications/product_ui/assets/product_ui_wallpapers.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

typedef struct {
    product_ui_page_view_t view;
    lv_obj_t *cards[PRODUCT_UI_WALLPAPER_COUNT];
    lv_obj_t *marks[PRODUCT_UI_WALLPAPER_COUNT];
    lv_obj_t *current;
    lv_obj_t *status;
    uint32_t revision;
    uint32_t elapsed_ms;
    uint8_t selected_id;
    uint8_t render_pending;
} product_ui_wallpaper_page_t;
static product_ui_wallpaper_page_t s_wallpaper_page;

/**
 * @brief 更新选择标记、当前记录及保存状态，所有文字成功后才提交已呈现代次。
 * @note 文本失败保留 render_pending 以重试；只有已确认记录、结果成功且选中值相同才显示已应用。
 */
static void product_ui_settings_wallpaper_render(void)
{
    product_ui_settings_snapshot_t snapshot;
    const product_ui_wallpaper_t *current;
    const char *current_text;
    const char *action_text;
    const char *status_text;
    const char *displayed_action;
    uint8_t is_applied;
    uint8_t can_save;
    uint8_t index;
    int text_result;
    char current_caption[80];
    if (s_wallpaper_page.current == NULL ||
        product_ui_settings_service_snapshot(&snapshot) != 0) return;
    if (!s_wallpaper_page.render_pending && snapshot.revision == s_wallpaper_page.revision) return;
    current = product_ui_wallpaper_get(snapshot.wallpaper_id);
    if (current == NULL) return;
    is_applied = snapshot.appearance.has_confirmed_record && snapshot.appearance.result == D_OK &&
        s_wallpaper_page.selected_id == snapshot.wallpaper_id;
    can_save = product_ui_settings_appearance_can_save(&snapshot);
    for (index = 0U; index < PRODUCT_UI_WALLPAPER_COUNT; index++) {
        uint8_t is_selected = index == s_wallpaper_page.selected_id;
        if (lv_obj_has_state(s_wallpaper_page.cards[index], LV_STATE_CHECKED) != (is_selected != 0U)) {
            if (is_selected) lv_obj_add_state(s_wallpaper_page.cards[index], LV_STATE_CHECKED);
            else lv_obj_remove_state(s_wallpaper_page.cards[index], LV_STATE_CHECKED);
            lv_obj_set_style_border_color(s_wallpaper_page.cards[index],
                product_ui_color(is_selected ? PRODUCT_UI_COLOR_ACTION : PRODUCT_UI_COLOR_BORDER), 0);
        }
        if (is_selected) lv_obj_remove_flag(s_wallpaper_page.marks[index], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_wallpaper_page.marks[index], LV_OBJ_FLAG_HIDDEN);
    }
    lv_snprintf(current_caption, sizeof(current_caption), "当前：%s", current->name);
    text_result = product_ui_label_sync(s_wallpaper_page.current, current_caption, NULL);
    status_text = product_ui_settings_appearance_status(&snapshot);
    if (can_save && !snapshot.appearance.is_timed_out && snapshot.appearance.result == D_OK &&
        s_wallpaper_page.selected_id != snapshot.wallpaper_id) {
        status_text = "待应用";
    }
    if (product_ui_label_sync(s_wallpaper_page.status, status_text, NULL) < 0) text_result = -1;
    action_text = can_save ? (is_applied ? "已应用" : "应用") :
        (snapshot.appearance.is_quiescing ? "关机中" :
         snapshot.appearance.is_service_ready ? "保存中" : "不可用");
    product_ui_page_action_update(&s_wallpaper_page.view, action_text,
        can_save && !is_applied, can_save && is_applied);
    current_text = lv_label_get_text(s_wallpaper_page.current);
    displayed_action = lv_label_get_text(s_wallpaper_page.view.nav_action_label);
    s_wallpaper_page.render_pending = text_result < 0 || current_text == NULL || strstr(current_text, current->name) == NULL ||
        displayed_action == NULL || strcmp(displayed_action, action_text) != 0;
    if (!s_wallpaper_page.render_pending) s_wallpaper_page.revision = snapshot.revision;
}

/**
 * @brief 把点击卡片编号保存为页面草稿并触发重绘。
 * @param[in] event 卡片事件，用户数据编码壁纸编号。
 * @note 选择不直接保存，越界编号忽略。
 */
static void product_ui_settings_wallpaper_select(lv_event_t *event)
{
    uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (id >= PRODUCT_UI_WALLPAPER_COUNT) return;
    s_wallpaper_page.selected_id = id;
    s_wallpaper_page.render_pending = 1U;
    product_ui_settings_wallpaper_render();
}

/**
 * @brief 提交选中壁纸的保存请求并重新读取真实状态。
 * @param[in] event 应用操作事件，当前未读取载荷。
 * @note 请求失败提示不可保存；成功只表示受理，页面继续等外观完成快照。
 */
static void product_ui_settings_wallpaper_apply(lv_event_t *event)
{
    (void)event;
    if (product_ui_settings_service_command(PRODUCT_UI_SETTINGS_SET_WALLPAPER,
                                             s_wallpaper_page.selected_id) != D_OK) {
        product_ui_toast_show("暂时无法保存壁纸", 1400U);
    }
    s_wallpaper_page.render_pending = 1U;
    product_ui_settings_wallpaper_render();
}

/**
 * @brief 创建静态缩略图、名称及选中标记的壁纸卡片。
 * @param[in,out] parent 卡片父容器。
 * @param[in] id 有效壁纸目录编号，须落在本页固定卡片数组内。
 * @return 完整创建返回 0；资源或对象失败返回 -1，已创建对象交页面树清理。
 */
static int product_ui_settings_wallpaper_card(lv_obj_t *parent, uint8_t id)
{
    const product_ui_wallpaper_t *wallpaper = product_ui_wallpaper_get(id);
    lv_obj_t *card;
    lv_obj_t *image;
    lv_obj_t *name;
    lv_obj_t *mark;
    if (wallpaper == NULL) return -1;
    card = lv_button_create(parent);
    if (card == NULL) return -1;
    s_wallpaper_page.cards[id] = card;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 88, 88);
    lv_obj_set_style_bg_color(card, product_ui_color(PRODUCT_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, PRODUCT_UI_RADIUS_LG, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, product_ui_color(PRODUCT_UI_COLOR_BORDER), 0);
    lv_obj_add_style(card, product_ui_style(PRODUCT_UI_STYLE_PRESSED), LV_STATE_PRESSED);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, product_ui_settings_wallpaper_select, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)id);
    image = lv_image_create(card);
    name = lv_label_create(card);
    mark = lv_label_create(card);
    if (image == NULL || name == NULL || mark == NULL) return -1;
    lv_image_set_src(image, wallpaper->thumbnail);
    lv_obj_set_pos(image, 2, 2);
    lv_obj_set_pos(name, 2, 64);
    lv_obj_set_size(name, 80, 16);
    lv_obj_add_style(name, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(name, wallpaper->name);
    lv_obj_set_size(mark, 18, 18);
    lv_obj_set_pos(mark, 64, 2);
    lv_obj_set_style_bg_color(mark, product_ui_color(PRODUCT_UI_COLOR_ACTION), 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_font(mark, product_ui_font(PRODUCT_UI_FONT_CAPTION), 0);
    lv_obj_set_style_text_color(mark, lv_color_white(), 0);
    lv_obj_set_style_text_align(mark, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(mark, LV_SYMBOL_OK);
    s_wallpaper_page.marks[id] = mark;
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    return 0;
}

/**
 * @brief 核对壁纸数量后创建选择卡片及状态，恢复当前或在途的目标选择。
 */
int product_ui_settings_wallpaper_page_create(lv_obj_t *parent)
{
    product_ui_page_spec_t spec = {
        .title = "壁纸", .back_title = "设置", .show_back = 1U,
        .role = PRODUCT_UI_PAGE_ROLE_SETTINGS, .nav_action_text = "应用",
        .nav_action_cb = product_ui_settings_wallpaper_apply,
    };
    product_ui_settings_snapshot_t snapshot;
    lv_obj_t *content;
    lv_obj_t *cards;
    uint8_t index;
    memset(&s_wallpaper_page, 0, sizeof(s_wallpaper_page));
    if (product_ui_wallpaper_count() != PRODUCT_UI_WALLPAPER_COUNT ||
        product_ui_page_frame_create_ex(parent, &spec, &s_wallpaper_page.view) != 0) return -1;
    content = s_wallpaper_page.view.content;
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    cards = lv_obj_create(content);
    if (cards == NULL) return -1;
    lv_obj_remove_style_all(cards);
    lv_obj_set_size(cards, 296, 92);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cards, PRODUCT_UI_SPACE_LG, 0);
    lv_obj_clear_flag(cards, LV_OBJ_FLAG_SCROLLABLE);
    for (index = 0U; index < PRODUCT_UI_WALLPAPER_COUNT; index++) {
        if (product_ui_settings_wallpaper_card(cards, index) != 0) return -1;
    }
    s_wallpaper_page.current = lv_label_create(content);
    s_wallpaper_page.status = lv_label_create(content);
    if (s_wallpaper_page.current == NULL || s_wallpaper_page.status == NULL) return -1;
    lv_obj_add_style(s_wallpaper_page.current, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_add_style(s_wallpaper_page.status, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_set_width(s_wallpaper_page.current, 296);
    lv_obj_set_width(s_wallpaper_page.status, 296);
    if (product_ui_settings_service_snapshot(&snapshot) == 0) {
        s_wallpaper_page.selected_id = snapshot.appearance.is_pending || snapshot.appearance.is_in_progress ?
            snapshot.appearance.target_wallpaper_id : snapshot.wallpaper_id;
    }
    s_wallpaper_page.render_pending = 1U;
    product_ui_settings_wallpaper_render();
    return 0;
}

/**
 * @brief 推进公共详情周期，并按待重试状态及有限节奏刷新壁纸投影。
 * @param[in] elapsed_ms 本轮毫秒数，刷新累计每次最多采用 100。
 */
static void product_ui_settings_wallpaper_update(uint32_t elapsed_ms)
{
    product_ui_settings_detail_page_update(elapsed_ms);
    s_wallpaper_page.elapsed_ms += LV_MIN(elapsed_ms, 100U);
    if (s_wallpaper_page.elapsed_ms < 100U && !s_wallpaper_page.render_pending) return;
    s_wallpaper_page.elapsed_ms = 0U;
    product_ui_settings_wallpaper_render();
}

/**
 * @brief 解除框架状态并清空选择与呈现缓存，不取消已受理保存。
 */
static void product_ui_settings_wallpaper_destroy(void)
{
    product_ui_page_view_reset(&s_wallpaper_page.view);
    memset(&s_wallpaper_page, 0, sizeof(s_wallpaper_page));
}

static const page_ops_t s_wallpaper_ops = {
    .create = product_ui_settings_wallpaper_page_create,
    .destroy = product_ui_settings_wallpaper_destroy,
    .update = product_ui_settings_wallpaper_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};
static page_desc_t s_wallpaper_desc = {.name = "壁纸详情", .ops = &s_wallpaper_ops};
/**
 * @brief 注册静态壁纸详情页面描述。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int product_ui_settings_wallpaper_register(int page_type)
{
    s_wallpaper_desc.page_type = page_type;
    return page_manager_register(&s_wallpaper_desc);
}
D_UI_MODULE_DEFINE(product_settings_wallpaper, "壁纸详情", LV_SYMBOL_SETTINGS,
                   product_ui_settings_wallpaper_register, NULL);
#endif
