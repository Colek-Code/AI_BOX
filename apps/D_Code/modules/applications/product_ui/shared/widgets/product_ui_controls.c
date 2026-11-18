/** @file product_ui_controls.c @brief 共用列表行、有限文字缓存及输入重置辅助，不拥有业务服务状态。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include <string.h>

#define PRODUCT_UI_LIST_ROW_W          (PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD))
#define PRODUCT_UI_LIST_ROW_H          PRODUCT_UI_ROW_DOUBLE_H
#define PRODUCT_UI_LIST_ICON_BG_SIZE   23
#define PRODUCT_UI_LIST_GLYPH_SIZE     14
#define PRODUCT_UI_LIST_RADIUS          7
#define PRODUCT_UI_LIST_PAD_H           8
#define PRODUCT_UI_LIST_PAD_V           4
#define PRODUCT_UI_LIST_GAP             5
#define PRODUCT_UI_ICON_SCALE_BASE    256U

/**
 * @brief 在真实 RESET 回调中，仅对仍按下的指针输入设置等待物理松手。
 */
void product_ui_control_wait_release(lv_event_t *event)
{
    lv_indev_t *input;

    if (event == NULL || lv_event_get_code(event) != LV_EVENT_INDEV_RESET) {
        return;
    }
    /* RESET 的输入设备位于 param，lv_event_get_indev 不处理 RESET。 */
    input = lv_event_get_param(event);
    if (input != NULL && lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER &&
        lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) {
        lv_indev_wait_release(input);
    }
}

/**
 * @brief 复位输入并按复位前的真实按压状态设置松手门禁。
 */
void product_ui_control_reset_inputs(void)
{
    lv_indev_t *input = NULL;
    while ((input = lv_indev_get_next(input)) != NULL) {
        uint8_t pressed = lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED;
        lv_indev_reset(input, NULL);
        if (pressed) lv_indev_wait_release(input);
    }
}

/**
 * @brief 应用统一表单滑条皮肤；完整合同见公开组件头文件。
 */
int product_ui_slider_style(lv_obj_t *slider, product_ui_slider_role_e role)
{
    int32_t knob_pad;
    if (slider == NULL ||
        (role != PRODUCT_UI_SLIDER_COMPACT && role != PRODUCT_UI_SLIDER_REGULAR)) {
        return -1;
    }
    knob_pad = role == PRODUCT_UI_SLIDER_COMPACT ?
        PRODUCT_UI_SLIDER_KNOB_COMPACT_PAD : PRODUCT_UI_SLIDER_KNOB_REGULAR_PAD;
    lv_obj_set_style_bg_color(slider, product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, product_ui_color(PRODUCT_UI_COLOR_SURFACE), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, PRODUCT_UI_SLIDER_KNOB_BORDER_W, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, knob_pad, LV_PART_KNOB);
    return 0;
}

/**
 * @brief 使用公开文本及自尺寸检查判断当前 DOT 格式化是否已可用。
 * @param[in] label 有效标签对象。
 * @param[in] text 期望呈现的完整文字。
 * @return 已有可用文字且格式满足检查为 1，文本为空或仍异常溢出为 0。
 * @note 沿用 LVGL 对三字符以内不缩略的规则，不读取其私有字段。
 */
static uint8_t product_ui_label_format_ready(lv_obj_t *label, const char *text)
{
    const char *shown = lv_label_get_text(label);
    const unsigned char *cursor = (const unsigned char *)text;
    unsigned characters = 0U;
    int32_t height;
    if (shown == NULL) return 0U;
    if (lv_label_get_long_mode(label) != LV_LABEL_LONG_DOT || strcmp(shown, text) != 0) return 1U;
    /* LVGL 对不超过三个字符的文字即使区域很窄也保持原样。 */
    while (*cursor != 0U && characters <= 3U) {
        if ((*cursor & 0xc0U) != 0x80U) ++characters;
        ++cursor;
    }
    if (characters <= 3U) return 1U;
    /* 公开自尺寸使用 LVGL 自身布局／缓存；全文仍多行溢出时，
     * 即使文字非空也可能是 DOT 存储失败，保留轮询恢复，不读取私有字段。 */
    height = lv_obj_get_self_height(label);
    return height <= lv_obj_get_content_height(label) ||
        height <= lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
}

/**
 * @brief 以请求全文比较的缓存更新标签，并为文字或 DOT 格式化失败保留重试。
 */
int product_ui_label_sync(lv_obj_t *label, const char *text,
                          product_ui_label_cache_t *cache)
{
    size_t length;
    const char *shown;
    if (label == NULL || text == NULL) return -1;
    if (cache == NULL) {
        shown = lv_label_get_text(label);
        if (shown != NULL && strcmp(shown, text) == 0 &&
            product_ui_label_format_ready(label, text)) return 0;
        lv_label_set_text(label, text);
        return product_ui_label_format_ready(label, text) ? 1 : -1;
    }
    if (cache->label != label) {
        cache->label = label;
        cache->valid = 0U;
    }
    if (cache->valid && strcmp(cache->requested, text) == 0 && product_ui_label_format_ready(label, text)) return 0;
    lv_label_set_text(label, text);
    cache->valid = 0U;
    if (!product_ui_label_format_ready(label, text)) return -1;
    length = strlen(text);
    if (length < sizeof(cache->requested)) {
        memcpy(cache->requested, text, length + 1U);
        cache->valid = 1U;
    }
    return 1;
}

/* 复用行共享不可变几何；各条目颜色、文字及高度仍局部保存，
 * 重绑维持相同交互而不重复拷贝几何常量。 */
enum {
    GEOMETRY_WIDTH, GEOMETRY_HEIGHT, GEOMETRY_RADIUS, GEOMETRY_BG_OPA,
    GEOMETRY_PAD_LEFT, GEOMETRY_PAD_RIGHT, GEOMETRY_PAD_TOP, GEOMETRY_PAD_BOTTOM,
    GEOMETRY_PAD_ROW, GEOMETRY_PAD_COLUMN, GEOMETRY_BORDER_WIDTH,
    GEOMETRY_LAYOUT, GEOMETRY_FLEX_FLOW, GEOMETRY_FLEX_GROW,
    GEOMETRY_FLEX_MAIN, GEOMETRY_FLEX_CROSS, GEOMETRY_FLEX_TRACK, GEOMETRY_ALIGN,
};
static const lv_style_prop_t s_geometry_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_RADIUS, LV_STYLE_BG_OPA,
    LV_STYLE_PAD_LEFT, LV_STYLE_PAD_RIGHT, LV_STYLE_PAD_TOP, LV_STYLE_PAD_BOTTOM,
    LV_STYLE_PAD_ROW, LV_STYLE_PAD_COLUMN, LV_STYLE_BORDER_WIDTH,
    LV_STYLE_LAYOUT, LV_STYLE_FLEX_FLOW, LV_STYLE_FLEX_GROW,
    LV_STYLE_FLEX_MAIN_PLACE, LV_STYLE_FLEX_CROSS_PLACE, LV_STYLE_FLEX_TRACK_PLACE, LV_STYLE_ALIGN,
};
#define GEOMETRY_VALUE(property, number) {&s_geometry_properties[GEOMETRY_##property], {.num = (number)}}
static const lv_style_const_prop_t s_list_row_geometry[] = {
    GEOMETRY_VALUE(WIDTH, PRODUCT_UI_LIST_ROW_W),
    GEOMETRY_VALUE(RADIUS, PRODUCT_UI_LIST_RADIUS),
    GEOMETRY_VALUE(PAD_LEFT, PRODUCT_UI_LIST_PAD_H), GEOMETRY_VALUE(PAD_RIGHT, PRODUCT_UI_LIST_PAD_H),
    GEOMETRY_VALUE(PAD_TOP, PRODUCT_UI_LIST_PAD_V), GEOMETRY_VALUE(PAD_BOTTOM, PRODUCT_UI_LIST_PAD_V),
    GEOMETRY_VALUE(PAD_COLUMN, PRODUCT_UI_LIST_GAP), GEOMETRY_VALUE(LAYOUT, LV_LAYOUT_FLEX),
    GEOMETRY_VALUE(FLEX_FLOW, LV_FLEX_FLOW_ROW), GEOMETRY_VALUE(FLEX_MAIN, LV_FLEX_ALIGN_START),
    GEOMETRY_VALUE(FLEX_CROSS, LV_FLEX_ALIGN_CENTER), GEOMETRY_VALUE(FLEX_TRACK, LV_FLEX_ALIGN_CENTER),
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_list_badge_geometry[] = {
    GEOMETRY_VALUE(WIDTH, PRODUCT_UI_LIST_ICON_BG_SIZE), GEOMETRY_VALUE(HEIGHT, PRODUCT_UI_LIST_ICON_BG_SIZE),
    GEOMETRY_VALUE(BG_OPA, LV_OPA_COVER), GEOMETRY_VALUE(RADIUS, PRODUCT_UI_RADIUS_MD),
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_list_text_geometry[] = {
    GEOMETRY_VALUE(FLEX_GROW, 1), GEOMETRY_VALUE(LAYOUT, LV_LAYOUT_FLEX),
    GEOMETRY_VALUE(FLEX_FLOW, LV_FLEX_FLOW_COLUMN), GEOMETRY_VALUE(PAD_ROW, 2),
    GEOMETRY_VALUE(FLEX_MAIN, LV_FLEX_ALIGN_CENTER), GEOMETRY_VALUE(FLEX_CROSS, LV_FLEX_ALIGN_START),
    GEOMETRY_VALUE(FLEX_TRACK, LV_FLEX_ALIGN_START), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_list_label_geometry[] = {
    GEOMETRY_VALUE(WIDTH, LV_PCT(100)), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_image_padding_geometry[] = {
    GEOMETRY_VALUE(PAD_LEFT, 0), GEOMETRY_VALUE(PAD_RIGHT, 0),
    GEOMETRY_VALUE(PAD_TOP, 0), GEOMETRY_VALUE(PAD_BOTTOM, 0),
    GEOMETRY_VALUE(BORDER_WIDTH, 0), LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_list_row_style, (void *)s_list_row_geometry);
static LV_STYLE_CONST_INIT(s_list_badge_style, (void *)s_list_badge_geometry);
static LV_STYLE_CONST_INIT(s_list_text_style, (void *)s_list_text_geometry);
static LV_STYLE_CONST_INIT(s_list_label_style, (void *)s_list_label_geometry);
static LV_STYLE_CONST_INIT(s_image_padding_style, (void *)s_image_padding_geometry);
static const lv_style_const_prop_t s_inset_row_geometry[] = {
    GEOMETRY_VALUE(RADIUS, PRODUCT_UI_RADIUS_LG),
    GEOMETRY_VALUE(PAD_LEFT, PRODUCT_UI_SPACE_MD), GEOMETRY_VALUE(PAD_RIGHT, PRODUCT_UI_SPACE_MD),
    GEOMETRY_VALUE(PAD_COLUMN, PRODUCT_UI_SPACE_SM), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_inset_badge_geometry[] = {
    GEOMETRY_VALUE(WIDTH, 28), GEOMETRY_VALUE(HEIGHT, 28), LV_STYLE_CONST_PROPS_END
};
static const lv_style_prop_t s_inset_disabled_property = LV_STYLE_OPA;
static const lv_style_const_prop_t s_inset_disabled_geometry[] = {
    {&s_inset_disabled_property, {.num = LV_OPA_50}}, LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_inset_row_style, (void *)s_inset_row_geometry);
static LV_STYLE_CONST_INIT(s_inset_badge_style, (void *)s_inset_badge_geometry);
static LV_STYLE_CONST_INIT(s_inset_disabled_style, (void *)s_inset_disabled_geometry);
#undef GEOMETRY_VALUE

/**
 * @brief 清除默认样式及滚动标志，空对象忽略。
 * @param[in,out] obj 借用对象。
 */
static void product_ui_component_clean(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 创建主题标签并复制本次文字。
 * @param[in,out] parent 标签父对象。
 * @param[in] text 本次复制的文字。
 * @param[in] style 主题样式标记。
 * @return 成功返回父树拥有的标签，失败返回 NULL。
 */
static lv_obj_t *product_ui_component_label(lv_obj_t *parent, const char *text,
                                            product_ui_style_e style)
{
    lv_obj_t *label;

    label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_add_style(label, product_ui_style(style), 0);
    return label;
}

/**
 * @brief 绑定不可变生成图像，固定画布尺寸并清除缩放及旋转。
 * @param[in,out] img 借用图像对象。
 * @param[in] source 覆盖对象寿命的只读生成图像描述及像素。
 * @param[in] box_w 正画布宽度。
 * @param[in] box_h 正画布高度。
 * @note 只在描述指针改变时设置 source；不能用于描述指针不变但像素动态变化的图像刷新。
 */
static void product_ui_image_bind(lv_obj_t *img, const lv_image_dsc_t *source,
                                  int32_t box_w, int32_t box_h)
{
    if ((img == NULL) || (source == NULL) || (box_w <= 0) || (box_h <= 0)) {
        return;
    }
    lv_obj_set_size(img, box_w, box_h);
    if (lv_image_get_src(img) != source) {
        lv_image_set_src(img, source);
    }
    lv_image_set_scale(img, LV_SCALE_NONE);
    lv_image_set_rotation(img, 0);
    lv_image_set_align(img, LV_IMAGE_ALIGN_CENTER);
}

/**
 * @brief 以生成的原生光学尺寸绑定方形 A4 图标。
 * @param[in,out] icon 借用图标对象。
 * @param[in] source 静态生成描述。
 * @param[in] optical_size 原生方形尺寸，零不操作。
 */
static void product_ui_component_fit_icon(lv_obj_t *icon,
                                          const lv_image_dsc_t *source,
                                          uint16_t optical_size)
{
    if ((icon == NULL) || (source == NULL) || (optical_size == 0U)) {
        return;
    }
    product_ui_image_bind(icon, source, (int32_t)optical_size,
                          (int32_t)optical_size);
}

/**
 * @brief 查询指定尺寸生成图标并创建可重着色的图像子对象。
 * @param[in,out] parent 图像父对象。
 * @param[in] item 有效条目规格，提供图标编号。
 * @param[in] optical_size 已生成的原生尺寸。
 * @return 成功返回父树拥有的图像，资源缺失或创建失败为 NULL。
 */
static lv_obj_t *product_ui_component_icon(lv_obj_t *parent,
                                           const product_ui_list_item_t *item,
                                           uint16_t optical_size)
{
    const lv_image_dsc_t *source;
    lv_obj_t *icon;

    source = product_ui_icon_get_sized(item->icon_id, optical_size);
    if (source == NULL) {
        return NULL;
    }
    icon = lv_image_create(parent);
    if (icon == NULL) {
        return NULL;
    }
    product_ui_component_fit_icon(icon, source, optical_size);
    lv_obj_add_style(icon, &s_image_padding_style, 0);
    lv_obj_set_style_image_recolor(icon, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_center(icon);
    return icon;
}

/* 与列表行创建的固定子对象顺序一致，重绑必须同步遵守。 */
enum {
    PRODUCT_UI_LIST_ROW_CHILD_BADGE = 0,
    PRODUCT_UI_LIST_ROW_CHILD_TEXT = 1,
    PRODUCT_UI_LIST_ROW_CHILD_TRAILING = 2,
};

/**
 * @brief 创建主题色徽标与白色 A4 图标，图标失败时删除局部徽标。
 * @param[in,out] row 列表行父对象。
 * @param[in] item 有效条目图标与强调色。
 * @return 成功返回行拥有的徽标容器，失败为 NULL。
 */
static lv_obj_t *product_ui_list_icon_create(lv_obj_t *row,
                                             const product_ui_list_item_t *item)
{
    lv_obj_t *badge;
    lv_obj_t *icon;

    badge = lv_obj_create(row);
    if (badge == NULL) {
        return NULL;
    }
    product_ui_component_clean(badge);
    lv_obj_add_style(badge, &s_list_badge_style, 0);
    lv_obj_set_style_bg_color(badge,
                              product_ui_color(item->accent), 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    icon = product_ui_component_icon(badge, item,
                                     PRODUCT_UI_LIST_GLYPH_SIZE);
    if (icon == NULL) {
        lv_obj_delete(badge);
        return NULL;
    }
    lv_obj_center(icon);
    return badge;
}

/**
 * @brief 隐藏、不可点击或禁用时终止点击事件继续分发。
 * @param[in] event 行或按钮的 CLICKED 事件。
 */
static void product_ui_component_action_guard(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) ||
        lv_obj_has_state(object, LV_STATE_DISABLED)) {
        lv_event_stop_processing(event);
    }
}

/**
 * @brief 先附着点击门禁再附着业务回调，并按使能／回调存在性设置点击能力。
 * @param[in,out] obj 有效交互对象。
 * @param[in] is_enabled 非零允许当前操作。
 * @param[in] event_cb 可为空的持久动作回调。
 * @param[in] user_data 业务回调借用上下文，须覆盖对象寿命。
 */
static void product_ui_component_bind_action(lv_obj_t *obj, uint8_t is_enabled,
                                             product_ui_action_cb_t event_cb,
                                             void *user_data)
{
    if (event_cb != NULL) {
        lv_obj_add_event_cb(obj, product_ui_component_action_guard, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(obj, event_cb, LV_EVENT_CLICKED, user_data);
    }
    if (!is_enabled || (event_cb == NULL)) {
        if (!is_enabled) {
            lv_obj_add_style(obj,
                             product_ui_style(PRODUCT_UI_STYLE_DISABLED), 0);
            lv_obj_add_state(obj, LV_STATE_DISABLED);
        }
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * @brief 创建随行伸展的标题及可隐藏副标题，采用固定单行字体高度和 DOT 模式。
 * @param[in,out] row 列表行父对象。
 * @param[in] item 有效条目标题。
 * @param[in] subtitle 可为空的副标题文字。
 * @param[in] height 行总高度，用于扣除文字区上下间距。
 * @return 成功返回行拥有的文字容器，任一子标签失败删除局部容器并返回 NULL。
 */
static lv_obj_t *product_ui_list_text_create(lv_obj_t *row,
                                             const product_ui_list_item_t *item,
                                             const char *subtitle, uint16_t height)
{
    lv_obj_t *text_box;
    lv_obj_t *title;
    lv_obj_t *caption;

    text_box = lv_obj_create(row);
    if (text_box == NULL) {
        return NULL;
    }
    product_ui_component_clean(text_box);
    lv_obj_clear_flag(text_box, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_CLICK_FOCUSABLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_height(text_box,
                      height - (2 * PRODUCT_UI_LIST_PAD_V));
    lv_obj_add_style(text_box, &s_list_text_style, 0);

    title = product_ui_component_label(text_box, item->title, PRODUCT_UI_STYLE_BODY);
    caption = product_ui_component_label(text_box, subtitle != NULL ? subtitle : "",
                                         PRODUCT_UI_STYLE_CAPTION);
    if ((title == NULL) || (caption == NULL)) {
        lv_obj_delete(text_box);
        return NULL;
    }
    lv_obj_add_style(title, &s_list_label_style, 0);
    lv_obj_add_style(caption, &s_list_label_style, 0);
    lv_obj_set_height(title, product_ui_font(PRODUCT_UI_FONT_BODY)->line_height);
    lv_obj_set_height(caption, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    if ((subtitle == NULL) || (subtitle[0] == '\0')) {
        lv_obj_add_flag(caption, LV_OBJ_FLAG_HIDDEN);
    }
    return text_box;
}

/**
 * @brief 检查必要条目／标题及当前行交互、尾部、回调和状态文字组合。
 * @param[in] spec 候选行规格。
 * @return 符合共用语义规则为非零，否则为零。
 */
static int product_ui_list_spec_valid(const product_ui_row_spec_t *spec)
{
    if ((spec == NULL) || (spec->item == NULL) ||
        (spec->item->title == NULL)) {
        return 0;
    }
    return product_ui_row_semantics_valid(
        spec->interaction, spec->trailing,
        spec->action_cb != NULL, spec->status_text != NULL);
}

/**
 * @brief 创建行尾空状态标签并应用次要文字色。
 * @param[in,out] parent 行尾父容器。
 * @return 成功返回父树拥有的标签，失败为 NULL。
 */
static lv_obj_t *product_ui_list_trailing_text_create(lv_obj_t *parent)
{
    lv_obj_t *label = product_ui_component_label(parent, "", PRODUCT_UI_STYLE_CAPTION);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    }
    return label;
}

/**
 * @brief 创建可在文字／开关角色间复用的行尾容器，开关不单独接管输入。
 * @param[in,out] row 列表行父对象。
 * @return 成功返回行拥有的容器，子对象失败删除容器并返回 NULL。
 */
static lv_obj_t *product_ui_list_switch_holder_create(lv_obj_t *row)
{
    lv_obj_t *holder = lv_obj_create(row);
    lv_obj_t *label;
    lv_obj_t *toggle;
    if (holder == NULL) return NULL;
    product_ui_component_clean(holder);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    label = product_ui_list_trailing_text_create(holder);
    toggle = lv_switch_create(holder);
    if (label == NULL || toggle == NULL) {
        lv_obj_delete(holder);
        return NULL;
    }
    lv_obj_set_size(toggle, 32, 18);
    lv_obj_set_style_bg_color(toggle, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_clear_flag(toggle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_center(label);
    lv_obj_center(toggle);
    return holder;
}

/**
 * @brief 按尾部角色绑定隐藏、开关、箭头或有界状态文字，必要时事务式扩展为开关容器。
 * @param[in,out] trailing 当前行尾对象；成功扩展时旧文字对象会被替换删除。
 * @param[in] spec 有效行尾角色、值及可选缓存。
 * @return 成功为 0，必要子对象、扩展分配或文字格式化失败为 -1。
 * @note 扩展分配失败保留旧文字对象；测量请求全文而不是 DOT 已缩略缓冲。
 */
static int product_ui_list_trailing_bind(
    lv_obj_t *trailing, const product_ui_row_spec_t *spec)
{
    lv_obj_t *label;
    lv_obj_t *toggle = NULL;
    lv_point_t text_size;
    const char *text;
    uint16_t row_height = spec->is_single_line ? PRODUCT_UI_ROW_SINGLE_H : PRODUCT_UI_ROW_DOUBLE_H;

    /* 普通文字不分配额外容器或隐藏开关；角色后来需要开关时才扩展，
     * 扩展分配失败保留旧文字对象。 */
    if (lv_obj_check_type(trailing, &lv_label_class) && spec->trailing == PRODUCT_UI_ROW_TRAILING_SWITCH) {
        lv_obj_t *holder = product_ui_list_switch_holder_create(lv_obj_get_parent(trailing));
        if (holder == NULL) return -1;
        lv_obj_move_to_index(holder, lv_obj_get_index(trailing));
        lv_obj_delete(trailing);
        trailing = holder;
        if (spec->text_cache != NULL) memset(&spec->text_cache->trailing, 0,
                                             sizeof(spec->text_cache->trailing));
    }
    if (lv_obj_check_type(trailing, &lv_label_class)) {
        label = trailing;
    } else {
        label = lv_obj_get_child(trailing, 0);
        toggle = lv_obj_get_child(trailing, 1);
    }
    if (label == NULL) {
        return -1;
    }
    if (spec->trailing == PRODUCT_UI_ROW_TRAILING_NONE) {
        lv_obj_add_flag(trailing, LV_OBJ_FLAG_HIDDEN);
        return 0;
    }
    if (spec->trailing == PRODUCT_UI_ROW_TRAILING_SWITCH) {
        if (toggle == NULL) return -1;
        lv_obj_clear_flag(trailing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(trailing, 40, row_height - 2 * PRODUCT_UI_LIST_PAD_V);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(toggle, LV_OBJ_FLAG_HIDDEN);
        if (spec->is_checked) {
            lv_obj_add_state(toggle, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(toggle, LV_STATE_CHECKED);
        }
        return 0;
    }
    lv_obj_clear_flag(trailing, LV_OBJ_FLAG_HIDDEN);
    if (label != trailing) lv_obj_set_height(trailing, row_height - 2 * PRODUCT_UI_LIST_PAD_V);
    if (toggle != NULL) {
        lv_obj_add_flag(toggle, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    text = spec->trailing == PRODUCT_UI_ROW_TRAILING_CHEVRON ? LV_SYMBOL_RIGHT : spec->status_text;
    /* DOT 可能缩短标签内部缓冲，因此测量请求全文。 */
    lv_text_get_size(&text_size, text,
                     product_ui_font(PRODUCT_UI_FONT_CAPTION), 0, 0,
                     PRODUCT_UI_LIST_ROW_W, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(trailing, LV_CLAMP(20, text_size.x, 84));
    if (label != trailing) lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    if (lv_label_get_long_mode(label) != LV_LABEL_LONG_DOT)
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return product_ui_label_sync(label, text, spec->text_cache != NULL ?
        &spec->text_cache->trailing : NULL) < 0 ? -1 : 0;
}

/**
 * @brief 按规格创建行尾文字或开关容器并完成初次绑定。
 * @param[in,out] row 列表行父对象。
 * @param[in] spec 有效行规格。
 * @return 成功为 0，创建／绑定失败删除局部对象并返回 -1。
 */
static int product_ui_list_trailing_create(
    lv_obj_t *row, const product_ui_row_spec_t *spec)
{
    lv_obj_t *trailing = spec->trailing == PRODUCT_UI_ROW_TRAILING_SWITCH ?
        product_ui_list_switch_holder_create(row) : product_ui_list_trailing_text_create(row);
    if (trailing == NULL) return -1;
    if (product_ui_list_trailing_bind(trailing, spec) != 0) {
        lv_obj_delete(trailing);
        return -1;
    }
    return 0;
}

/**
 * @brief 只把现有行尾更新为状态文字，不重建整行及动作绑定。
 */
int product_ui_list_row_status_update(lv_obj_t *row, const char *text)
{
    lv_obj_t *holder;
    product_ui_row_spec_t spec = {
        .status_text = text,
        .trailing = PRODUCT_UI_ROW_TRAILING_STATUS,
    };

    if (row == NULL || text == NULL) {
        return -1;
    }
    holder = lv_obj_get_child(row, PRODUCT_UI_LIST_ROW_CHILD_TRAILING);
    if (holder == NULL) {
        return -1;
    }
    spec.is_single_line = lv_obj_get_style_height(row, 0) == PRODUCT_UI_ROW_SINGLE_H;
    return product_ui_list_trailing_bind(holder, &spec);
}

/**
 * @brief 比较精确本地样式值，只有颜色变化时才改本地属性。
 * @param[in,out] object 有效对象。
 * @param[in] property 颜色类样式属性。
 * @param[in] color 期望颜色。
 * @note 不比较按压或过渡解析后的颜色，避免无变化重复写入。
 */
static void product_ui_row_color_sync(lv_obj_t *object, lv_style_prop_t property,
                                      lv_color_t color)
{
    lv_style_value_t previous;
    lv_style_value_t next = {.color = color};
    /* 比较本地精确值，不使用按压／过渡后的解析颜色。 */
    if (lv_obj_get_local_style_prop(object, property, &previous, 0) == LV_STYLE_RES_FOUND &&
        lv_color_eq(previous.color, color)) return;
    lv_obj_set_local_style_prop(object, property, next, 0);
}

/**
 * @brief 按普通／内嵌行外观更新共享几何、徽标底色及图标色。
 * @param[in,out] row 本模块布局的列表行。
 * @param[in] spec 含条目强调色及外观角色的规格。
 */
static void product_ui_list_row_appearance(lv_obj_t *row, const product_ui_row_spec_t *spec)
{
    lv_obj_t *badge = lv_obj_get_child(row, PRODUCT_UI_LIST_ROW_CHILD_BADGE);
    lv_obj_t *icon = badge != NULL ? lv_obj_get_child(badge, 0) : NULL;
    lv_color_t accent = product_ui_color(spec->item->accent);
    lv_color_t badge_color = accent;
    lv_color_t icon_color = lv_color_white();
    if (badge == NULL || icon == NULL) return;
    lv_obj_remove_style(row, &s_inset_row_style, 0);
    lv_obj_remove_style(row, &s_inset_disabled_style, LV_STATE_DISABLED);
    lv_obj_remove_style(badge, &s_inset_badge_style, 0);
    if (spec->appearance == PRODUCT_UI_ROW_APPEARANCE_INSET) {
        lv_obj_add_style(row, &s_inset_row_style, 0);
        lv_obj_add_style(row, &s_inset_disabled_style, LV_STATE_DISABLED);
        lv_obj_add_style(badge, &s_inset_badge_style, 0);
        badge_color = lv_color_mix(accent, product_ui_color(PRODUCT_UI_COLOR_SURFACE), LV_OPA_10);
        icon_color = accent;
    }
    product_ui_row_color_sync(badge, LV_STYLE_BG_COLOR, badge_color);
    product_ui_row_color_sync(icon, LV_STYLE_IMAGE_RECOLOR, icon_color);
}

/**
 * @brief 复用当前行对象，更新图标、文字、尾部及可操作性。
 */
int product_ui_list_row_rebind(lv_obj_t *row,
                               const product_ui_row_spec_t *spec)
{
    lv_obj_t *badge;
    lv_obj_t *icon;
    lv_obj_t *text_box;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *trailing;
    const lv_image_dsc_t *source;

    if ((row == NULL) || !product_ui_list_spec_valid(spec)) {
        return -1;
    }
    source = product_ui_icon_get_sized(spec->item->icon_id, PRODUCT_UI_LIST_GLYPH_SIZE);
    badge = lv_obj_get_child(row, PRODUCT_UI_LIST_ROW_CHILD_BADGE);
    text_box = lv_obj_get_child(row, PRODUCT_UI_LIST_ROW_CHILD_TEXT);
    trailing = lv_obj_get_child(row, PRODUCT_UI_LIST_ROW_CHILD_TRAILING);
    if ((source == NULL) || (badge == NULL) || (text_box == NULL) || (trailing == NULL)) {
        return -1;
    }
    icon = lv_obj_get_child(badge, 0);
    title = lv_obj_get_child(text_box, 0);
    subtitle = lv_obj_get_child(text_box, 1);
    if ((icon == NULL) || (title == NULL) || (subtitle == NULL)) {
        return -1;
    }
    /* 保留后续图标绑定调用，使已有的尺寸／源绑定流程一致。 */
    if (lv_image_get_src(icon) != source) {
        lv_image_set_src(icon, source);
    }
    product_ui_component_fit_icon(icon, source, PRODUCT_UI_LIST_GLYPH_SIZE);
    lv_obj_set_height(row, spec->is_single_line ? PRODUCT_UI_ROW_SINGLE_H : PRODUCT_UI_ROW_DOUBLE_H);
    lv_obj_set_height(text_box, lv_obj_get_style_height(row, 0) - 2 * PRODUCT_UI_LIST_PAD_V);
    if (spec->is_single_line || (spec->subtitle == NULL) || (spec->subtitle[0] == '\0')) {
        lv_obj_add_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
    }
    product_ui_list_row_appearance(row, spec);
    if (product_ui_label_sync(title, spec->item->title, spec->text_cache != NULL ?
        &spec->text_cache->title : NULL) < 0 ||
        product_ui_label_sync(subtitle, spec->subtitle != NULL ? spec->subtitle : "",
        spec->text_cache != NULL ? &spec->text_cache->subtitle : NULL) < 0 ||
        product_ui_list_trailing_bind(trailing, spec) != 0) return -1;

    lv_obj_remove_style(row,
                        product_ui_style(PRODUCT_UI_STYLE_PRESSED),
                        LV_STATE_PRESSED);
    if (spec->interaction != PRODUCT_UI_ROW_INTERACTION_NONE) {
        lv_obj_add_style(row,
                         product_ui_style(PRODUCT_UI_STYLE_PRESSED),
                         LV_STATE_PRESSED);
    }
    lv_obj_remove_style(row,
                        product_ui_style(PRODUCT_UI_STYLE_DISABLED), 0);
    lv_obj_clear_state(row, LV_STATE_DISABLED);
    if (!spec->is_enabled) {
        lv_obj_add_style(row,
                         product_ui_style(PRODUCT_UI_STYLE_DISABLED), 0);
        lv_obj_add_state(row, LV_STATE_DISABLED);
    }
    if (spec->is_enabled &&
        (spec->interaction != PRODUCT_UI_ROW_INTERACTION_NONE)) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    }
    return 0;
}

/**
 * @brief 验证规格后创建图标、文字及尾部，绑定共用动作门禁。
 */
lv_obj_t *product_ui_list_row_create(lv_obj_t *parent,
                                     const product_ui_row_spec_t *spec)
{
    lv_obj_t *row;
    lv_obj_t *icon;
    lv_obj_t *text_box;
    uint16_t height;

    if ((parent == NULL) || !product_ui_list_spec_valid(spec) ||
        (product_ui_icon_get(spec->item->icon_id) == NULL)) {
        return NULL;
    }

    height = spec->is_single_line ? PRODUCT_UI_ROW_SINGLE_H : PRODUCT_UI_ROW_DOUBLE_H;
    row = lv_button_create(parent);
    if (row == NULL) {
        return NULL;
    }
    product_ui_component_clean(row);
    lv_obj_add_style(row, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_style(row, product_ui_style(PRODUCT_UI_STYLE_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_style(row, &s_list_row_style, 0);
    lv_obj_set_height(row, height);

    icon = product_ui_list_icon_create(row, spec->item);
    text_box = product_ui_list_text_create(row, spec->item,
                                          spec->is_single_line ? NULL : spec->subtitle,
                                          height);
    if ((icon == NULL) || (text_box == NULL) ||
        (product_ui_list_trailing_create(row, spec) != 0)) {
        /* 不把半成品行留在父级 flex 列表。 */
        lv_obj_delete(row);
        return NULL;
    }
    if (spec->interaction == PRODUCT_UI_ROW_INTERACTION_NONE) {
        lv_obj_remove_style(row,
                            product_ui_style(PRODUCT_UI_STYLE_PRESSED),
                            LV_STATE_PRESSED);
    }
    product_ui_component_bind_action(
        row, spec->is_enabled,
        spec->interaction == PRODUCT_UI_ROW_INTERACTION_NONE ?
        NULL : spec->action_cb,
        spec->user_data);
    product_ui_list_row_appearance(row, spec);
    return row;
}

/**
 * @brief 创建指定宽度的主题分组标题。
 */
lv_obj_t *product_ui_section_create(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label;

    if ((parent == NULL) || (title == NULL)) {
        return NULL;
    }
    label = product_ui_component_label(parent, title, PRODUCT_UI_STYLE_SECTION);
    if (label != NULL) {
        lv_obj_set_width(label, PRODUCT_UI_LIST_ROW_W);
    }
    return label;
}

/**
 * @brief 创建居中的主题空状态说明标签。
 */
lv_obj_t *product_ui_empty_state_create(lv_obj_t *parent, const char *message)
{
    lv_obj_t *label;

    if ((parent == NULL) || (message == NULL)) {
        return NULL;
    }
    label = product_ui_component_label(parent, message, PRODUCT_UI_STYLE_BODY);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
        lv_obj_center(label);
    }
    return label;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
