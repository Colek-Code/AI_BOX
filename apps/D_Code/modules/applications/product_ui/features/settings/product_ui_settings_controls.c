/** @file product_ui_settings_controls.c @brief 设置详情页的本地开关及滑条行，服务提交由外部消费者绑定。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"

#define PRODUCT_UI_SETTING_ROW_W       (PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD))
#define PRODUCT_UI_SETTING_ROW_H       PRODUCT_UI_ROW_DOUBLE_H
#define PRODUCT_UI_SETTING_ROW_GAP     4
#define PRODUCT_UI_SETTING_PAD_HOR     PRODUCT_UI_SPACE_MD
#define PRODUCT_UI_SETTING_PAD_VER     4
#define PRODUCT_UI_SETTING_TEXT_GAP    2
#define PRODUCT_UI_SETTING_RADIUS      PRODUCT_UI_RADIUS_LG
#define PRODUCT_UI_SETTING_SWITCH_W    32
#define PRODUCT_UI_SETTING_SWITCH_H    18
#define PRODUCT_UI_SETTING_SLIDER_W    96
#define PRODUCT_UI_SETTING_SLIDER_H    6
#define PRODUCT_UI_SETTING_SLIDER_HIT_PAD \
    ((PRODUCT_UI_TOUCH_MIN - PRODUCT_UI_SETTING_SLIDER_H + 1) / 2)
#define PRODUCT_UI_SETTING_VALUE_LEN   8

/**
 * @brief 清除对象默认样式及滚动能力，保留显式交互配置。
 * @param[in,out] obj 调用链保证有效的 LVGL 对象。
 */
static void product_ui_setting_clean(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 行点击时翻转子开关并发送 VALUE_CHANGED，不直接提交业务命令。
 * @param[in] event 点击事件，用户数据借用行内开关对象。
 */
static void product_ui_toggle_row_clicked(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_user_data(event);

    if (toggle == NULL) {
        return;
    }
    if (lv_obj_has_state(toggle, LV_STATE_CHECKED)) {
        lv_obj_clear_state(toggle, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    PRODUCT_UI_TRACE("settings toggle checked=%u\n",
                     lv_obj_has_state(toggle, LV_STATE_CHECKED) ? 1U : 0U);
    lv_obj_send_event(toggle, LV_EVENT_VALUE_CHANGED, NULL);
}

/**
 * @brief 创建不可点击的标题及副标题文本区。
 * @param[in,out] row 文本区父行。
 * @param[in] title 本次由标签复制的标题。
 * @param[in] subtitle 本次由标签复制的副标题。
 * @return 成功返回文本区，任一步创建失败返回 NULL，部分对象仍由行拥有。
 */
static lv_obj_t *product_ui_setting_text(lv_obj_t *row, const char *title,
                                         const char *subtitle)
{
    lv_obj_t *box;
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;

    box = lv_obj_create(row);
    if (box == NULL) {
        return NULL;
    }
    product_ui_setting_clean(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_CLICK_FOCUSABLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_width(box, 1);
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_height(box, PRODUCT_UI_SETTING_ROW_H -
                      (2 * PRODUCT_UI_SETTING_PAD_VER));
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(box, PRODUCT_UI_SETTING_TEXT_GAP, 0);

    title_label = lv_label_create(box);
    if (title_label == NULL) {
        return NULL;
    }
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_height(title_label, product_ui_font(PRODUCT_UI_FONT_BODY)->line_height);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(title_label, title);
    lv_obj_add_style(title_label, product_ui_style(PRODUCT_UI_STYLE_BODY), 0);

    subtitle_label = lv_label_create(box);
    if (subtitle_label == NULL) {
        return NULL;
    }
    lv_obj_set_width(subtitle_label, LV_PCT(100));
    lv_obj_set_height(subtitle_label, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(subtitle_label, subtitle);
    lv_obj_add_style(subtitle_label,
                     product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    return box;
}

/**
 * @brief 创建共用设置行及文本区，失败删除已创建行。
 * @param[in,out] parent 新行的父容器。
 * @param[in] title 借用标题。
 * @param[in] subtitle 借用副标题。
 * @return 成功返回父对象拥有的行，失败返回 NULL。
 */
static lv_obj_t *product_ui_setting_row(lv_obj_t *parent, const char *title,
                                        const char *subtitle)
{
    lv_obj_t *row;

    row = lv_obj_create(parent);
    if (row == NULL) {
        return NULL;
    }
    product_ui_setting_clean(row);
    lv_obj_add_style(row, product_ui_style(PRODUCT_UI_STYLE_SURFACE), 0);
    lv_obj_set_size(row, PRODUCT_UI_SETTING_ROW_W, PRODUCT_UI_SETTING_ROW_H);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, PRODUCT_UI_SETTING_RADIUS, 0);
    lv_obj_set_style_pad_hor(row, PRODUCT_UI_SETTING_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(row, PRODUCT_UI_SETTING_PAD_VER, 0);
    lv_obj_set_style_pad_column(row, PRODUCT_UI_SETTING_PAD_HOR, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(parent, PRODUCT_UI_SETTING_ROW_GAP, 0);
    if (product_ui_setting_text(row, title, subtitle) == NULL) {
        lv_obj_delete(row);
        return NULL;
    }
    return row;
}

/**
 * @brief 按本模块固定子对象布局借用副标题标签。
 * @param[in] row 本模块创建的设置行，可为空。
 * @return 结构符合预期返回标签，否则为 NULL。
 */
static lv_obj_t *product_ui_setting_subtitle(lv_obj_t *row)
{
    lv_obj_t *text_box;

    if ((row == NULL) || (lv_obj_get_child_count(row) == 0U)) {
        return NULL;
    }
    text_box = lv_obj_get_child(row, 0);
    if ((text_box == NULL) || (lv_obj_get_child_count(text_box) < 2U)) {
        return NULL;
    }
    return lv_obj_get_child(text_box, 1);
}

/**
 * @brief 在滑条值变化时只更新副标题的百分比显示。
 * @param[in] event 滑条事件，用户数据借用副标题标签。
 */
static void product_ui_slider_value_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    lv_obj_t *value_label = lv_event_get_user_data(event);

    if ((slider == NULL) || (value_label == NULL)) {
        return;
    }
    lv_label_set_text_fmt(value_label, "%ld%%",
                          (long)lv_slider_get_value(slider));
}

/**
 * @brief 创建本地开关行并设定初始选中状态。
 */
lv_obj_t *product_ui_toggle_row_create(lv_obj_t *parent, const char *title,
                                       const char *subtitle, uint8_t is_checked)
{
    lv_obj_t *row;
    lv_obj_t *toggle;

    if ((parent == NULL) || (title == NULL) || (subtitle == NULL)) {
        return NULL;
    }
    row = product_ui_setting_row(parent, title, subtitle);
    if (row == NULL) {
        return NULL;
    }
    toggle = lv_switch_create(row);
    if (toggle == NULL) {
        return NULL;
    }
    lv_obj_set_size(toggle, PRODUCT_UI_SETTING_SWITCH_W,
                    PRODUCT_UI_SETTING_SWITCH_H);
    lv_obj_set_style_bg_color(toggle,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(toggle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(toggle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(toggle, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(toggle,
                              product_ui_color(PRODUCT_UI_COLOR_ACCENT),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(toggle,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE),
                              LV_PART_KNOB);
    lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(toggle, 0, LV_PART_KNOB);
    lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(toggle, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(toggle, -2, LV_PART_KNOB);
    lv_obj_set_style_bg_color(toggle,
                              product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED),
                              LV_PART_MAIN | LV_STATE_DISABLED);
    if (is_checked) {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    lv_obj_clear_flag(toggle, LV_OBJ_FLAG_EVENT_BUBBLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(row, product_ui_toggle_row_clicked,
                        LV_EVENT_CLICKED, toggle);
    return row;
}

/**
 * @brief 创建零至一百的本地滑条行并绑定百分比文字更新。
 */
lv_obj_t *product_ui_slider_row_create(lv_obj_t *parent, const char *title,
                                       int32_t value)
{
    char value_text[PRODUCT_UI_SETTING_VALUE_LEN];
    int32_t clamped_value;
    lv_obj_t *row;
    lv_obj_t *slider;
    lv_obj_t *value_label;

    if ((parent == NULL) || (title == NULL)) {
        return NULL;
    }
    clamped_value = LV_CLAMP(0, value, 100);
    lv_snprintf(value_text, sizeof(value_text), "%ld%%", (long)clamped_value);
    row = product_ui_setting_row(parent, title, value_text);
    if (row == NULL) {
        return NULL;
    }
    slider = lv_slider_create(row);
    if (slider == NULL) {
        return NULL;
    }
    lv_obj_set_size(slider, PRODUCT_UI_SETTING_SLIDER_W,
                    PRODUCT_UI_SETTING_SLIDER_H);
    lv_obj_set_style_bg_color(slider,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider,
                              product_ui_color(PRODUCT_UI_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,
                              product_ui_color(PRODUCT_UI_COLOR_ACCENT),
                              LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider,
                                  product_ui_color(PRODUCT_UI_COLOR_SURFACE),
                                  LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 3, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, PRODUCT_UI_SETTING_SLIDER_HIT_PAD);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, clamped_value, LV_ANIM_OFF);
    value_label = product_ui_setting_subtitle(row);
    if (value_label != NULL) {
        lv_obj_add_event_cb(slider, product_ui_slider_value_changed,
                            LV_EVENT_VALUE_CHANGED, value_label);
    }
    return row;
}

/**
 * @brief 按设置行固定布局借用第二个子对象作为实际控件。
 */
lv_obj_t *product_ui_setting_row_get_control(lv_obj_t *row)
{
    if ((row == NULL) || (lv_obj_get_child_count(row) < 2U)) {
        return NULL;
    }
    return lv_obj_get_child(row, 1);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
