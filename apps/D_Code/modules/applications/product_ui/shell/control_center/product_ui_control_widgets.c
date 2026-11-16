/** @file product_ui_control_widgets.c @brief 控制中心深色实体控件构造与值呈现，不直接调用业务服务。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shell/control_center/product_ui_control_widgets.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/assets/product_ui_icons.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include <string.h>

/* 媒体内容局部偏移；卡片壳体尺寸来自主题 PRODUCT_UI_CC_MEDIA_*。 */
#define PRODUCT_UI_MEDIA_ART_X               10
#define PRODUCT_UI_MEDIA_ART_Y               10
#define PRODUCT_UI_MEDIA_ART_SIZE            24
#define PRODUCT_UI_MEDIA_ART_RADIUS           5
#define PRODUCT_UI_MEDIA_TITLE_X             44
#define PRODUCT_UI_MEDIA_TITLE_Y              8
#define PRODUCT_UI_MEDIA_SUB_Y               28
#define PRODUCT_UI_INFO_ICON_SIZE            16
#define PRODUCT_UI_INFO_ICON_X                8
#define PRODUCT_UI_INFO_TEXT_X               31
#define PRODUCT_UI_INFO_TITLE_Y              15
#define PRODUCT_UI_INFO_SUB_Y                15
#define PRODUCT_UI_SLIDER_ICON_THRESHOLD     18

/* 控件均使用绝对放置，共用固定重置属性，
 * 避免每个长期控件重复保存相同零间距样式。 */
static const lv_style_prop_t s_clean_properties[] = {
    LV_STYLE_LAYOUT, LV_STYLE_PAD_LEFT, LV_STYLE_PAD_RIGHT, LV_STYLE_PAD_TOP,
    LV_STYLE_PAD_BOTTOM, LV_STYLE_PAD_ROW, LV_STYLE_PAD_COLUMN, LV_STYLE_BORDER_WIDTH,
    LV_STYLE_BG_OPA, LV_STYLE_RADIUS,
};
static const lv_style_const_prop_t s_clean_geometry[] = {
    {&s_clean_properties[0], {.num = LV_LAYOUT_NONE}},
    {&s_clean_properties[1], {.num = 0}}, {&s_clean_properties[2], {.num = 0}},
    {&s_clean_properties[3], {.num = 0}}, {&s_clean_properties[4], {.num = 0}},
    {&s_clean_properties[5], {.num = 0}}, {&s_clean_properties[6], {.num = 0}},
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_image_geometry[] = {
    {&s_clean_properties[1], {.num = 0}}, {&s_clean_properties[2], {.num = 0}},
    {&s_clean_properties[3], {.num = 0}}, {&s_clean_properties[4], {.num = 0}},
    {&s_clean_properties[7], {.num = 0}}, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_slider_indicator_geometry[] = {
    {&s_clean_properties[8], {.num = LV_OPA_TRANSP}},
    {&s_clean_properties[9], {.num = 0}}, {&s_clean_properties[7], {.num = 0}},
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_slider_knob_geometry[] = {
    {&s_clean_properties[8], {.num = LV_OPA_TRANSP}}, {&s_clean_properties[7], {.num = 0}},
    {&s_clean_properties[1], {.num = 0}}, {&s_clean_properties[2], {.num = 0}},
    {&s_clean_properties[3], {.num = 0}}, {&s_clean_properties[4], {.num = 0}},
    LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_clean_style, (void *)s_clean_geometry);
static LV_STYLE_CONST_INIT(s_image_style, (void *)s_image_geometry);
static LV_STYLE_CONST_INIT(s_slider_indicator_style, (void *)s_slider_indicator_geometry);
static LV_STYLE_CONST_INIT(s_slider_knob_style, (void *)s_slider_knob_geometry);

/**
 * @brief 清除默认样式及滚动／冒泡，安装共用绝对布局及零间距。
 * @param[in,out] obj 有效对象。
 */
static void product_ui_widget_clean(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    /* 绝对子坐标不使用 flex／grid 布局。 */
    lv_obj_add_style(obj, &s_clean_style, 0);
}

/**
 * @brief 固定左上对齐后设置绝对坐标，避免原居中对齐把坐标当作偏移。
 * @param[in,out] obj 借用对象，NULL 时忽略。
 * @param[in] x 父坐标系横坐标。
 * @param[in] y 父坐标系纵坐标。
 */
static void product_ui_widget_place(lv_obj_t *obj, int32_t x, int32_t y)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_align(obj, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(obj, x, y);
}

/**
 * @brief 借用原生静态图像，清除缩放、旋转及额外图像间距。
 * @param[in,out] img 借用图像对象。
 * @param[in] src 持续有效的生成描述及像素。
 * @param[in] box_w 正画布宽度。
 * @param[in] box_h 正画布高度。
 */
static void product_ui_ctrl_image_bind(lv_obj_t *img, const lv_image_dsc_t *src,
                                       int32_t box_w, int32_t box_h)
{
    if ((img == NULL) || (src == NULL) || (box_w <= 0) || (box_h <= 0)) {
        return;
    }
    lv_obj_set_size(img, box_w, box_h);
    lv_image_set_src(img, src);
    lv_image_set_scale(img, LV_SCALE_NONE);
    lv_image_set_rotation(img, 0);
    lv_image_set_align(img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_add_style(img, &s_image_style, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_TRANSP, 0);
}

/**
 * @brief 创建控制中心主题标签并复制本次文字，关闭输入及冒泡。
 * @param[in,out] parent 标签父容器。
 * @param[in] text 本次复制的文字。
 * @param[in] style 主题字体样式。
 * @return 成功返回父树拥有的标签，失败为 NULL。
 */
static lv_obj_t *product_ui_widget_label(lv_obj_t *parent, const char *text,
                                         product_ui_style_e style)
{
    lv_obj_t *label;

    label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_add_style(label, product_ui_style(style), 0);
    lv_obj_set_style_text_color(
        label, product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    return label;
}

/**
 * @brief 查询十六像素原生图标并创建可重着色图像。
 * @param[in,out] parent 图像父容器。
 * @param[in] icon_id 生成图标编号。
 * @param[in] color 目标重着色颜色。
 * @return 成功返回父树拥有的图像，资源或创建失败为 NULL。
 */
static lv_obj_t *product_ui_widget_icon(lv_obj_t *parent,
                                        product_ui_icon_id_t icon_id,
                                        lv_color_t color)
{
    const lv_image_dsc_t *source;
    lv_obj_t *icon;

    source = product_ui_icon_get_sized(icon_id, 16);
    if (source == NULL) {
        return NULL;
    }
    icon = lv_image_create(parent);
    if (icon == NULL) {
        return NULL;
    }
    product_ui_ctrl_image_bind(icon, source, 16, 16);
    lv_obj_set_style_image_recolor(icon, color, 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    return icon;
}

/**
 * @brief 创建指定实体表面色的绝对布局卡片并安装按压样式。
 * @param[in,out] parent 卡片父容器。
 * @param[in] width 主题给定宽度。
 * @param[in] height 主题给定高度。
 * @param[in] surface_color 主题表面色标记。
 * @return 成功返回父树拥有的卡片，失败为 NULL。
 */
static lv_obj_t *product_ui_widget_card(lv_obj_t *parent, int16_t width,
                                        int16_t height,
                                        product_ui_color_e surface_color)
{
    lv_obj_t *card;

    card = lv_button_create(parent);
    if (card == NULL) {
        return NULL;
    }
    product_ui_widget_clean(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_style(card, product_ui_style(PRODUCT_UI_STYLE_CONTROL_CARD), 0);
    lv_obj_add_style(card, product_ui_style(PRODUCT_UI_STYLE_CONTROL_PRESSED),
                     LV_STATE_PRESSED);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, product_ui_color(surface_color), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, PRODUCT_UI_CONTROL_RADIUS, 0);
    return card;
}

/**
 * @brief 按指定原生尺寸创建主题重着色图像，并显式选择居中或左上对齐。
 * @param[in,out] parent 图像父容器。
 * @param[in] icon_id 生成图标编号。
 * @param[in] box 已生成原生方形尺寸。
 * @param[in] color 主题图标色标记。
 * @param[in] center_in_parent 非零居中，零左上对齐供后续绝对放置。
 * @return 成功返回父树拥有的图像，资源或创建失败为 NULL。
 */
static lv_obj_t *product_ui_cc_glyph(lv_obj_t *parent,
                                     product_ui_icon_id_t icon_id,
                                     int16_t box, product_ui_color_e color,
                                     uint8_t center_in_parent)
{
    lv_obj_t *icon;
    const lv_image_dsc_t *source;

    source = product_ui_icon_get_sized(icon_id, (uint16_t)box);
    if (source == NULL) {
        return NULL;
    }
    icon = lv_image_create(parent);
    if (icon == NULL) {
        return NULL;
    }
    product_ui_ctrl_image_bind(icon, source, box, box);
    lv_obj_set_style_image_recolor(icon, product_ui_color(color), 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    if (center_in_parent != 0U) {
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_set_align(icon, LV_ALIGN_TOP_LEFT);
    }
    return icon;
}

/**
 * @brief 创建卡片内单个完整命中列，内含居中圆盘和静态图标。
 * @param[in,out] parent 卡内传输条父容器。
 * @param[in] x 局部横坐标。
 * @param[in] y 局部纵坐标。
 * @param[in] hit_w 命中宽度，不小于圆盘视觉尺寸。
 * @param[in] hit_h 命中高度，不小于圆盘视觉尺寸。
 * @param[in] command 存入对象用户数据供控制器解释的媒体命令。
 * @param[in] icon_id 原生传输图标编号。
 * @param[out] out_glyph 可为空，预清零，成功接收图标借用指针。
 * @return 成功返回按钮，命中区太小或分配／图标失败返回 NULL，失败删除局部按钮。
 * @note 不设置外扩点击区，控件本身不执行服务命令。
 */
static lv_obj_t *product_ui_media_hit_button_create(
    lv_obj_t *parent, int16_t x, int16_t y, int16_t hit_w, int16_t hit_h,
    uint16_t command, product_ui_icon_id_t icon_id, lv_obj_t **out_glyph)
{
    lv_obj_t *button;
    lv_obj_t *disc;
    lv_obj_t *glyph_obj;
    int16_t disc_x;
    int16_t disc_y;

    if (out_glyph != NULL) {
        *out_glyph = NULL;
    }
    if ((hit_w < PRODUCT_UI_CC_MEDIA_BTN_VIS) ||
        (hit_h < PRODUCT_UI_CC_MEDIA_BTN_VIS)) {
        return NULL;
    }
    button = lv_button_create(parent);
    if (button == NULL) {
        return NULL;
    }
    product_ui_widget_clean(button);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_size(button, hit_w, hit_h);
    product_ui_widget_place(button, x, y);
    lv_obj_set_style_radius(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        button, product_ui_color(PRODUCT_UI_COLOR_CONTROL_PLAY_PRESSED),
        LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    /* 命令编号交控制器解释，控件不调用业务服务。 */
    lv_obj_set_user_data(button, (void *)(uintptr_t)command);

    disc = lv_obj_create(button);
    if (disc == NULL) {
        lv_obj_delete(button);
        return NULL;
    }
    product_ui_widget_clean(disc);
    lv_obj_set_size(disc, PRODUCT_UI_CC_MEDIA_BTN_VIS,
                    PRODUCT_UI_CC_MEDIA_BTN_VIS);
    disc_x = (int16_t)((hit_w - PRODUCT_UI_CC_MEDIA_BTN_VIS) / 2);
    disc_y = (int16_t)((hit_h - PRODUCT_UI_CC_MEDIA_BTN_VIS) / 2);
    product_ui_widget_place(disc, disc_x, disc_y);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(
        disc, product_ui_color(command == PRODUCT_UI_MEDIA_CMD_TOGGLE_PLAY_PAUSE ?
            PRODUCT_UI_COLOR_CONTROL_ACTIVE_TINT : PRODUCT_UI_COLOR_SURFACE_MEDIA), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 0, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    glyph_obj = product_ui_cc_glyph(disc, icon_id, PRODUCT_UI_CC_MEDIA_GLYPH_SIZE,
                                    PRODUCT_UI_COLOR_CONTROL_GLYPH, 1U);
    if (glyph_obj == NULL) {
        lv_obj_delete(button);
        return NULL;
    }
    if (out_glyph != NULL) {
        *out_glyph = glyph_obj;
    }
    return button;
}

/**
 * @brief 创建封面、标题及副标题的媒体信息卡，传输控件由独立构造器提供。
 */
lv_obj_t *product_ui_media_card_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_media_controls_t *controls)
{
    lv_obj_t *card;
    lv_obj_t *cover;
    lv_obj_t *glyph;
    lv_obj_t *title;
    lv_obj_t *subtitle;

    if ((controls == NULL) || (width < 80) || (height < 80)) {
        return NULL;
    }
    /* 若传输条已先创建，保留其引用及播放投影字段。 */
    {
        lv_obj_t *transport = controls->transport;
        lv_obj_t *previous = controls->previous;
        lv_obj_t *play = controls->play;
        lv_obj_t *next = controls->next;
        lv_obj_t *play_glyph = controls->play_glyph;
        uint8_t is_playing = controls->is_playing;

        memset(controls, 0, sizeof(*controls));
        controls->transport = transport;
        controls->previous = previous;
        controls->play = play;
        controls->next = next;
        controls->play_glyph = play_glyph;
        controls->is_playing = is_playing;
    }

    card = product_ui_widget_card(parent, width, height,
                                  PRODUCT_UI_COLOR_SURFACE_MEDIA);
    if (card == NULL) {
        return NULL;
    }
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_layout(card, LV_LAYOUT_NONE, 0);
    /* 子对象按圆角卡片裁剪，传输控件必须留在壳体内。 */
    lv_obj_set_style_clip_corner(card, true, 0);

    cover = lv_obj_create(card);
    if (cover == NULL) {
        lv_obj_delete(card);
        return NULL;
    }
    product_ui_widget_clean(cover);
    lv_obj_set_size(cover, PRODUCT_UI_MEDIA_ART_SIZE, PRODUCT_UI_MEDIA_ART_SIZE);
    product_ui_widget_place(cover, PRODUCT_UI_MEDIA_ART_X, PRODUCT_UI_MEDIA_ART_Y);
    lv_obj_set_style_bg_color(
        cover, product_ui_color(PRODUCT_UI_COLOR_MEDIA_ART_BG), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cover, PRODUCT_UI_MEDIA_ART_RADIUS, 0);
    lv_obj_set_style_clip_corner(cover, true, 0);
    lv_obj_set_style_border_width(cover, 0, 0);
    lv_obj_set_style_pad_all(cover, 0, 0);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    glyph = product_ui_cc_glyph(cover, PRODUCT_UI_ICON_CC_MEDIA, 14,
                                PRODUCT_UI_COLOR_CONTROL_GLYPH, 1U);
    if (glyph == NULL) {
        lv_obj_delete(card);
        return NULL;
    }

    title = product_ui_widget_label(card, "音乐", PRODUCT_UI_STYLE_BODY);
    if (title == NULL) {
        lv_obj_delete(card);
        return NULL;
    }
    lv_obj_set_width(title, width - PRODUCT_UI_MEDIA_TITLE_X - 10);
    lv_obj_set_height(title, product_ui_font(PRODUCT_UI_FONT_BODY)->line_height);
    product_ui_widget_place(title, PRODUCT_UI_MEDIA_TITLE_X,
                            PRODUCT_UI_MEDIA_TITLE_Y);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    controls->title = title;

    subtitle = product_ui_widget_label(card, "未接入", PRODUCT_UI_STYLE_CAPTION);
    if (subtitle == NULL) {
        lv_obj_delete(card);
        controls->title = NULL;
        return NULL;
    }
    lv_obj_set_width(subtitle, width - PRODUCT_UI_MEDIA_TITLE_X - 10);
    lv_obj_set_height(subtitle, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    product_ui_widget_place(subtitle, PRODUCT_UI_MEDIA_TITLE_X,
                            PRODUCT_UI_MEDIA_SUB_Y);
    lv_obj_set_style_text_color(
        subtitle, product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    controls->subtitle = subtitle;
    return card;
}

/**
 * @brief 在媒体卡内部创建三个不重叠传输命中区及居中图标。
 */
lv_obj_t *product_ui_media_transport_create(
    lv_obj_t *parent, product_ui_media_controls_t *controls)
{
    lv_obj_t *root;
    lv_obj_t *previous;
    lv_obj_t *play;
    lv_obj_t *next;
    int16_t x1;
    int16_t x2;

    if ((parent == NULL) || (controls == NULL)) {
        return NULL;
    }
    controls->transport = NULL;
    controls->previous = NULL;
    controls->play = NULL;
    controls->next = NULL;
    controls->play_glyph = NULL;
    controls->is_playing = 0U;

    /* 父对象必须是媒体卡，三个独立命中区均使用卡内传输条的局部坐标。 */
    root = lv_obj_create(parent);
    if (root == NULL) {
        return NULL;
    }
    product_ui_widget_clean(root);
    lv_obj_set_size(root, PRODUCT_UI_CC_MEDIA_TRANSPORT_W,
                    PRODUCT_UI_CC_MEDIA_TRANSPORT_H);
    product_ui_widget_place(root, 0, PRODUCT_UI_CC_MEDIA_TRANSPORT_Y);
    lv_obj_set_style_layout(root, LV_LAYOUT_NONE, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    x1 = PRODUCT_UI_CC_MEDIA_HIT_W0;
    x2 = (int16_t)(PRODUCT_UI_CC_MEDIA_HIT_W0 + PRODUCT_UI_CC_MEDIA_HIT_W1);
    previous = product_ui_media_hit_button_create(
        root, 0, 0, PRODUCT_UI_CC_MEDIA_HIT_W0, PRODUCT_UI_CC_MEDIA_HIT_H,
        (uint16_t)PRODUCT_UI_MEDIA_CMD_PREVIOUS, PRODUCT_UI_ICON_CC_PREVIOUS,
        NULL);
    play = product_ui_media_hit_button_create(
        root, x1, 0, PRODUCT_UI_CC_MEDIA_HIT_W1, PRODUCT_UI_CC_MEDIA_HIT_H,
        (uint16_t)PRODUCT_UI_MEDIA_CMD_TOGGLE_PLAY_PAUSE,
        PRODUCT_UI_ICON_CC_PLAY, &controls->play_glyph);
    next = product_ui_media_hit_button_create(
        root, x2, 0, PRODUCT_UI_CC_MEDIA_HIT_W2, PRODUCT_UI_CC_MEDIA_HIT_H,
        (uint16_t)PRODUCT_UI_MEDIA_CMD_NEXT, PRODUCT_UI_ICON_CC_NEXT, NULL);
    if ((previous == NULL) || (play == NULL) || (next == NULL) ||
        (controls->play_glyph == NULL)) {
        lv_obj_delete(root);
        controls->transport = NULL;
        controls->previous = NULL;
        controls->play = NULL;
        controls->next = NULL;
        controls->play_glyph = NULL;
        return NULL;
    }
    controls->transport = root;
    controls->previous = previous;
    controls->play = play;
    controls->next = next;
    return root;
}

/**
 * @brief 向已有的上一首、播放及下一首按钮追加 CLICKED 回调。
 */
void product_ui_media_transport_bind(
    product_ui_media_controls_t *controls, lv_event_cb_t cb, void *user_data)
{
    if ((controls == NULL) || (cb == NULL)) {
        return;
    }
    if (controls->previous != NULL) {
        lv_obj_add_event_cb(controls->previous, cb, LV_EVENT_CLICKED, user_data);
    }
    if (controls->play != NULL) {
        lv_obj_add_event_cb(controls->play, cb, LV_EVENT_CLICKED, user_data);
    }
    if (controls->next != NULL) {
        lv_obj_add_event_cb(controls->next, cb, LV_EVENT_CLICKED, user_data);
    }
}

/**
 * @brief 复制更新已有媒体标题与副标题，空字段保持原文字。
 */
void product_ui_media_card_set_text(product_ui_media_controls_t *controls,
                                    const char *title,
                                    const char *subtitle)
{
    if (controls == NULL) {
        return;
    }
    if ((controls->title != NULL) && (title != NULL)) {
        lv_label_set_text(controls->title, title);
    }
    if ((controls->subtitle != NULL) && (subtitle != NULL)) {
        lv_label_set_text(controls->subtitle, subtitle);
    }
}

/**
 * @brief 播放投影变化时重绑播放／暂停静态图标。
 */
void product_ui_media_card_set_playing(product_ui_media_controls_t *controls,
                                       uint8_t is_playing)
{
    const lv_image_dsc_t *source;
    product_ui_icon_id_t icon_id;

    if ((controls == NULL) || (controls->play_glyph == NULL)) {
        return;
    }
    is_playing = is_playing ? 1U : 0U;
    if (controls->is_playing == is_playing) {
        return;
    }
    icon_id = is_playing ? PRODUCT_UI_ICON_CC_PAUSE : PRODUCT_UI_ICON_CC_PLAY;
    source = product_ui_icon_get_sized(icon_id, PRODUCT_UI_CC_MEDIA_GLYPH_SIZE);
    if (source == NULL) {
        return;
    }
    product_ui_ctrl_image_bind(controls->play_glyph, source,
                               PRODUCT_UI_CC_MEDIA_GLYPH_SIZE,
                               PRODUCT_UI_CC_MEDIA_GLYPH_SIZE);
    lv_obj_set_style_image_recolor(
        controls->play_glyph,
        product_ui_color(PRODUCT_UI_COLOR_CONTROL_GLYPH), 0);
    lv_obj_set_style_image_recolor_opa(controls->play_glyph, LV_OPA_COVER, 0);
    controls->is_playing = is_playing;
}

/**
 * @brief 创建图标、标题及右侧状态说明的信息卡，初始不绑定点击。
 */
lv_obj_t *product_ui_info_card_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_icon_id_t icon_id, const char *title,
    product_ui_info_card_view_t *view)
{
    lv_obj_t *card;
    lv_obj_t *icon;
    lv_obj_t *title_lbl;
    lv_obj_t *sub_lbl;

    if ((view == NULL) || (title == NULL) || (width < 144) || (height < 32)) {
        return NULL;
    }
    memset(view, 0, sizeof(*view));
    card = lv_obj_create(parent);
    if (card == NULL) {
        return NULL;
    }
    product_ui_widget_clean(card);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_layout(card, LV_LAYOUT_NONE, 0);
    lv_obj_set_style_radius(card, PRODUCT_UI_CONTROL_RADIUS, 0);
    lv_obj_set_style_bg_color(card,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE_QUICK),
                              0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 紧凑信息行左侧为图标／标题，右侧为连接状态；
     * 使用左上对齐绝对放置，避免居中对齐把 set_pos 当偏移而使图标压入文字。 */
    icon = product_ui_cc_glyph(card, icon_id, PRODUCT_UI_INFO_ICON_SIZE,
                               PRODUCT_UI_COLOR_CONTROL_GLYPH, 0U);
    if (icon == NULL) {
        lv_obj_delete(card);
        return NULL;
    }
    product_ui_widget_place(
        icon, PRODUCT_UI_INFO_ICON_X,
        (int32_t)((height - PRODUCT_UI_INFO_ICON_SIZE) / 2));
    title_lbl = product_ui_widget_label(card, title, PRODUCT_UI_STYLE_CAPTION);
    if (title_lbl == NULL) {
        lv_obj_delete(card);
        return NULL;
    }
    lv_obj_set_width(title_lbl, 49);
    lv_obj_set_height(title_lbl, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    product_ui_widget_place(title_lbl, PRODUCT_UI_INFO_TEXT_X,
                            PRODUCT_UI_INFO_TITLE_Y);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);

    sub_lbl = product_ui_widget_label(card, "不可用", PRODUCT_UI_STYLE_CAPTION);
    if (sub_lbl == NULL) {
        lv_obj_delete(card);
        return NULL;
    }
    lv_obj_set_width(sub_lbl, width - 98);
    lv_obj_set_height(sub_lbl, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    lv_obj_set_style_text_align(sub_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    product_ui_widget_place(sub_lbl, 88,
                            PRODUCT_UI_INFO_SUB_Y);
    lv_obj_set_style_text_color(
        sub_lbl, product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
    lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_DOT);

    view->root = card;
    view->icon = icon;
    view->title = title_lbl;
    view->subtitle = sub_lbl;
    view->state = PRODUCT_UI_CONTROL_UNAVAILABLE;
    return card;
}

/**
 * @brief 保持实体壳不透明，按激活／未激活／不可用更新内容色及可选副标题。
 */
void product_ui_info_card_set_state(product_ui_info_card_view_t *view,
                                    product_ui_control_state_e state,
                                    const char *subtitle)
{
    if ((view == NULL) || (view->root == NULL)) {
        return;
    }
    /* 壳体始终不透明，不可用只弱化内容颜色。 */
    lv_obj_set_style_opa(view->root, LV_OPA_COVER, 0);
    if (state == PRODUCT_UI_CONTROL_ACTIVE) {
        lv_obj_set_style_bg_color(
            view->root, product_ui_color(PRODUCT_UI_COLOR_CONTROL_ACTIVE_TINT),
            0);
        if (view->title != NULL) {
            lv_obj_set_style_text_color(
                view->title, product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT),
                0);
        }
        if (view->subtitle != NULL) {
            lv_obj_set_style_text_color(
                view->subtitle, product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT),
                0);
        }
        if (view->icon != NULL) {
            lv_obj_set_style_image_recolor(
                view->icon, product_ui_color(PRODUCT_UI_COLOR_CONTROL_GLYPH),
                0);
            lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
        }
    } else {
        lv_obj_set_style_bg_color(
            view->root, product_ui_color(PRODUCT_UI_COLOR_SURFACE_QUICK), 0);
        if (view->title != NULL) {
            lv_obj_set_style_text_color(
                view->title,
                state == PRODUCT_UI_CONTROL_UNAVAILABLE ?
                    product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED) :
                    product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT),
                0);
        }
        if (view->subtitle != NULL) {
            lv_obj_set_style_text_color(
                view->subtitle,
                product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
        }
        if (view->icon != NULL) {
            lv_obj_set_style_image_recolor(
                view->icon,
                product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
            lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
        }
    }
    if ((view->subtitle != NULL) && (subtitle != NULL)) {
        lv_label_set_text(view->subtitle, subtitle);
    }
    view->state = state;
}

/**
 * @brief 根据控件当前范围、值及高度计算底部填充像素。
 * @param[in,out] view 持久滑条引用，部分对象缺失时忽略。
 */
static void product_ui_slider_fill_sync(product_ui_slider_view_t *view)
{
    int32_t value;
    int32_t min_value;
    int32_t max_value;
    int32_t range;
    int32_t track_h;
    int32_t track_w;
    int32_t fill_h;

    if ((view == NULL) || (view->root == NULL) || (view->fill == NULL)) {
        return;
    }
    track_w = lv_obj_get_width(view->root);
    track_h = lv_obj_get_height(view->root);
    if ((track_w <= 0) || (track_h <= 0)) {
        return;
    }
    min_value = lv_slider_get_min_value(view->root);
    max_value = lv_slider_get_max_value(view->root);
    value = lv_slider_get_value(view->root);
    range = max_value - min_value;
    if (range <= 0) {
        range = 1;
    }
    if (value < min_value) {
        value = min_value;
    }
    if (value > max_value) {
        value = max_value;
    }
    fill_h = (int32_t)(((int32_t)track_h * (value - min_value)) / range);
    if (fill_h < 0) {
        fill_h = 0;
    }
    if (fill_h > track_h) {
        fill_h = track_h;
    }
    lv_obj_set_size(view->fill, track_w, fill_h);
    lv_obj_align(view->fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    if (fill_h <= 0) {
        lv_obj_add_flag(view->fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(view->fill, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 同步填充、图标对比色及只在拖动显示的数值说明。
 * @param[in,out] view 持久滑条引用及本地拖动状态。
 */
static void product_ui_slider_visual_update(product_ui_slider_view_t *view)
{
    product_ui_color_e color;
    int32_t value;

    if ((view == NULL) || (view->root == NULL) || (view->icon == NULL)) {
        return;
    }
    product_ui_slider_fill_sync(view);
    value = lv_slider_get_value(view->root);
    color = value >= PRODUCT_UI_SLIDER_ICON_THRESHOLD ?
            PRODUCT_UI_COLOR_SLIDER_ICON : PRODUCT_UI_COLOR_CONTROL_GLYPH;
    lv_obj_set_style_image_recolor(view->icon, product_ui_color(color), 0);
    lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
    /* 能力恢复时撤销不可用弱化，图标恢复完全不透明。 */
    lv_obj_set_style_opa(view->icon, LV_OPA_COVER, 0);
    if (view->value_label != NULL) {
        char text[24];
        lv_snprintf(text, sizeof(text), "%s\n%ld%%", view->name, (long)value);
        if (strcmp(lv_label_get_text(view->value_label), text) != 0)
            lv_label_set_text(view->value_label, text);
        lv_obj_set_style_opa(view->value_label, LV_OPA_COVER, 0);
        if (view->is_dragging) {
            lv_obj_clear_flag(view->value_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view->value_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 按下时置本地拖动预览标志，并在值／尺寸变化时刷新视觉。
 * @param[in] event 滑条事件，用户数据借用持久视图。
 */
static void product_ui_slider_value_cb(lv_event_t *event)
{
    product_ui_slider_view_t *view = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        view->is_dragging = 1U;
    }
    product_ui_slider_visual_update(view);
}

/**
 * @brief 保存权威确认值，非拖动时再应用到滑条及本地视觉。
 */
void product_ui_slider_apply_value(product_ui_slider_view_t *view,
                                   int32_t value)
{
    if ((view == NULL) || (view->root == NULL)) {
        return;
    }
    view->confirmed_value = value;
    if (view->is_dragging) {
        return;
    }
    lv_slider_set_value(view->root, value, LV_ANIM_OFF);
    product_ui_slider_visual_update(view);
}

/**
 * @brief 创建零至一百的竖滑条、独立填充、图标及拖动时数值说明。
 */
lv_obj_t *product_ui_vertical_slider_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_icon_id_t icon_id, int32_t value,
    product_ui_slider_view_t *view)
{
    lv_obj_t *slider;
    lv_obj_t *fill;
    lv_obj_t *value_label;
    lv_obj_t *icon;
    int16_t radius;

    if (view == NULL) {
        return NULL;
    }
    view->root = NULL;
    view->fill = NULL;
    view->value_label = NULL;
    view->icon = NULL;
    view->name = icon_id == PRODUCT_UI_ICON_CC_BRIGHTNESS ? "亮度" : "音量";
    view->is_dragging = 0U;
    view->confirmed_value = value;
    slider = lv_slider_create(parent);
    if (slider == NULL) {
        return NULL;
    }
    product_ui_widget_clean(slider);
    lv_obj_set_size(slider, width, height);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    radius = PRODUCT_UI_CONTROL_RADIUS;
    if (radius > (width / 2)) {
        radius = (int16_t)(width / 2);
    }
    lv_obj_set_style_bg_color(
        slider, product_ui_color(PRODUCT_UI_COLOR_SURFACE_SLIDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_add_style(slider, &s_slider_indicator_style, LV_PART_INDICATOR);
    lv_obj_add_style(slider, &s_slider_knob_style, LV_PART_KNOB);
    lv_obj_set_style_clip_corner(slider, true, 0);
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    fill = lv_obj_create(slider);
    if (fill == NULL) {
        lv_obj_delete(slider);
        return NULL;
    }
    product_ui_widget_clean(fill);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_color(fill,
                              product_ui_color(PRODUCT_UI_COLOR_SLIDER_FILL), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, 0, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    value_label = product_ui_widget_label(slider, "", PRODUCT_UI_STYLE_CAPTION);
    icon = product_ui_widget_icon(slider, icon_id,
                                  product_ui_color(PRODUCT_UI_COLOR_CONTROL_GLYPH));
    if ((value_label == NULL) || (icon == NULL)) {
        lv_obj_delete(slider);
        return NULL;
    }
    lv_obj_set_width(value_label, width - 8);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(value_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(value_label, LV_OPA_60, 0);
    lv_obj_set_style_radius(value_label, PRODUCT_UI_RADIUS_SM, 0);
    lv_obj_set_style_pad_ver(value_label, 2, 0);
    lv_obj_align(value_label, LV_ALIGN_TOP_MID, 0, 12);
    product_ui_ctrl_image_bind(icon, product_ui_icon_get_sized(icon_id, 16), 16, 16);
    lv_obj_set_style_image_recolor(
        icon, product_ui_color(PRODUCT_UI_COLOR_CONTROL_GLYPH), 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_align(icon, LV_ALIGN_BOTTOM_MID, 0, -10);
    view->root = slider;
    view->fill = fill;
    view->value_label = value_label;
    view->icon = icon;
    lv_obj_move_to_index(fill, 0);
    lv_obj_add_event_cb(slider, product_ui_slider_value_cb,
                        LV_EVENT_PRESSED, view);
    lv_obj_add_event_cb(slider, product_ui_slider_value_cb,
                        LV_EVENT_VALUE_CHANGED, view);
    lv_obj_add_event_cb(slider, product_ui_slider_value_cb,
                        LV_EVENT_SIZE_CHANGED, view);
    product_ui_slider_visual_update(view);
    return slider;
}

/**
 * @brief 把具名快捷项显式映射为生成图标。
 * @param[in] tile_id 快捷项语义身份。
 * @return 有效项返回对应图标，未知项为 ICON_COUNT 使创建失败，不回退设置图标。
 */
static product_ui_icon_id_t product_ui_quick_tile_icon(
    product_ui_quick_tile_e tile_id)
{
    switch (tile_id) {
    case PRODUCT_UI_QUICK_TILE_ALARM:
        return PRODUCT_UI_ICON_CC_ALARM;
    case PRODUCT_UI_QUICK_TILE_ROTATION:
        return PRODUCT_UI_ICON_CC_ROTATION;
    case PRODUCT_UI_QUICK_TILE_SETTINGS:
        return PRODUCT_UI_ICON_CC_SETTINGS;
    default:
        /* 不回退为设置图标，未知项通过缺少图标直接让创建失败。 */
        return PRODUCT_UI_ICON_COUNT;
    }
}

/**
 * @brief 按静态语义规格创建快捷卡，并投影为初始未激活。
 */
lv_obj_t *product_ui_quick_card_create(
    lv_obj_t *parent, const product_ui_quick_card_spec_t *spec,
    product_ui_quick_card_view_t *view)
{
    lv_obj_t *card;
    lv_obj_t *glyph;
    lv_obj_t *label;
    product_ui_icon_id_t icon_id;

    if ((spec == NULL) || (spec->semantic_name == NULL) || (view == NULL)) {
        return NULL;
    }
    icon_id = product_ui_quick_tile_icon(spec->tile_id);
    if (icon_id >= PRODUCT_UI_ICON_COUNT) {
        return NULL;
    }
    card = product_ui_widget_card(parent, spec->width, spec->height,
                                  PRODUCT_UI_COLOR_SURFACE_QUICK);
    if (card == NULL) {
        return NULL;
    }
    lv_obj_set_style_radius(card, PRODUCT_UI_RADIUS_MD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_ext_click_area(card, PRODUCT_UI_CONTROL_QUICK_HIT_PAD);
    glyph = product_ui_cc_glyph(card, icon_id, 18,
                                PRODUCT_UI_COLOR_CONTROL_GLYPH, 0U);
    label = product_ui_widget_label(card,
        spec->tile_id == PRODUCT_UI_QUICK_TILE_ROTATION ? "翻转" : spec->semantic_name,
        PRODUCT_UI_STYLE_CAPTION);
    if (glyph == NULL || label == NULL) {
        lv_obj_delete(card);
        return NULL;
    }
    lv_obj_align(glyph, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -3);
    view->root = card;
    view->icon = glyph;
    view->label = label;
    view->tile_id = spec->tile_id;
    view->semantic_name = spec->semantic_name;
    product_ui_quick_card_set_state(view, PRODUCT_UI_CONTROL_INACTIVE);
    return card;
}

/**
 * @brief 保持实体壳不透明，按状态更新背景及图标文字色，并记录显示状态。
 */
void product_ui_quick_card_set_state(product_ui_quick_card_view_t *view,
                                     product_ui_control_state_e state)
{
    if ((view == NULL) || (view->root == NULL)) {
        return;
    }
    lv_obj_remove_state(view->root, LV_STATE_DISABLED);
    /* 壳体始终完全不透明，仅不可用内容使用灰色。 */
    lv_obj_set_style_opa(view->root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(view->root, LV_OPA_COVER, 0);
    if (view->label != NULL) {
        lv_obj_set_style_text_color(view->label, product_ui_color(
            state == PRODUCT_UI_CONTROL_UNAVAILABLE ? PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED :
            PRODUCT_UI_COLOR_CONTROL_TEXT), 0);
    }
    if (state == PRODUCT_UI_CONTROL_ACTIVE) {
        lv_obj_set_style_bg_color(
            view->root, product_ui_color(PRODUCT_UI_COLOR_CONTROL_ACTIVE_TINT),
            0);
        if (view->icon != NULL) {
            lv_obj_set_style_image_recolor(
                view->icon, product_ui_color(PRODUCT_UI_COLOR_CONTROL_GLYPH),
                0);
            lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
        }
    } else if (state == PRODUCT_UI_CONTROL_UNAVAILABLE) {
        lv_obj_set_style_bg_color(
            view->root, product_ui_color(PRODUCT_UI_COLOR_SURFACE_QUICK), 0);
        if (view->icon != NULL) {
            lv_obj_set_style_image_recolor(
                view->icon,
                product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
            lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
        }
    } else {
        lv_obj_set_style_bg_color(
            view->root, product_ui_color(PRODUCT_UI_COLOR_SURFACE_QUICK), 0);
        if (view->icon != NULL) {
            lv_obj_set_style_image_recolor(
                view->icon, product_ui_color(PRODUCT_UI_COLOR_CONTROL_GLYPH),
                0);
            lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
        }
    }
    view->state = state;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
