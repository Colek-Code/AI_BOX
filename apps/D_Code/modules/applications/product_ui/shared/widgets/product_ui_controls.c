/**
 * @file product_ui_controls.c
 * @brief Reusable app, list, and state components.
 */

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

/* Repeated rows share immutable geometry. Per-item colors, text and heights
 * stay local so rebinding keeps the same behavior without copying constants. */
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
#undef GEOMETRY_VALUE

static void product_ui_component_clean(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

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

/* A4 glyphs are generated at their display size; no runtime transform. */
static void product_ui_image_bind(lv_obj_t *img, const lv_image_dsc_t *source,
                                  int32_t box_w, int32_t box_h)
{
    if ((img == NULL) || (source == NULL) || (box_w <= 0) || (box_h <= 0)) {
        return;
    }
    lv_obj_set_size(img, box_w, box_h);
    lv_image_set_src(img, source);
    lv_image_set_scale(img, LV_SCALE_NONE);
    lv_image_set_rotation(img, 0);
    lv_image_set_align(img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_add_style(img, &s_image_padding_style, 0);
}

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
    lv_obj_set_style_image_recolor(icon, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_center(icon);
    return icon;
}

/* Child order of product_ui_list_row_create — keep rebind in lockstep. */
enum {
    PRODUCT_UI_LIST_ROW_CHILD_BADGE = 0,
    PRODUCT_UI_LIST_ROW_CHILD_TEXT = 1,
    PRODUCT_UI_LIST_ROW_CHILD_TRAILING = 2,
};

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

static void product_ui_component_action_guard(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) ||
        !lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) ||
        lv_obj_has_state(object, LV_STATE_DISABLED)) {
        lv_event_stop_processing(event);
    }
}

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

static lv_obj_t *product_ui_list_trailing_text_create(lv_obj_t *parent)
{
    lv_obj_t *label = product_ui_component_label(parent, "", PRODUCT_UI_STYLE_CAPTION);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    }
    return label;
}

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
    lv_obj_clear_flag(toggle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_center(label);
    lv_obj_center(toggle);
    return holder;
}

static int product_ui_list_trailing_bind(
    lv_obj_t *trailing, const product_ui_row_spec_t *spec)
{
    lv_obj_t *label;
    lv_obj_t *toggle = NULL;
    lv_point_t text_size;
    const char *text;
    uint16_t row_height = spec->is_single_line ? PRODUCT_UI_ROW_SINGLE_H : PRODUCT_UI_ROW_DOUBLE_H;

    /* Plain text needs no wrapper or hidden switch. Promote transactionally
     * only when the row later changes role, retaining text on allocation failure. */
    if (lv_obj_check_type(trailing, &lv_label_class) && spec->trailing == PRODUCT_UI_ROW_TRAILING_SWITCH) {
        lv_obj_t *holder = product_ui_list_switch_holder_create(lv_obj_get_parent(trailing));
        if (holder == NULL) return -1;
        lv_obj_move_to_index(holder, lv_obj_get_index(trailing));
        lv_obj_delete(trailing);
        trailing = holder;
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
    /* DOT may shorten the label buffer, so measure the requested text. */
    lv_text_get_size(&text_size, text,
                     product_ui_font(PRODUCT_UI_FONT_CAPTION), 0, 0,
                     PRODUCT_UI_LIST_ROW_W, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(trailing, LV_CLAMP(20, text_size.x, 84));
    if (label != trailing) lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
    return 0;
}

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
    lv_obj_set_style_bg_color(badge, product_ui_color(spec->item->accent), 0);
    lv_image_set_src(icon, source);
    product_ui_component_fit_icon(icon, source, PRODUCT_UI_LIST_GLYPH_SIZE);
    if (strcmp(lv_label_get_text(title), spec->item->title) != 0) {
        lv_label_set_text(title, spec->item->title);
    }
    if (strcmp(lv_label_get_text(subtitle), spec->subtitle != NULL ? spec->subtitle : "") != 0) {
        lv_label_set_text(subtitle, spec->subtitle != NULL ? spec->subtitle : "");
    }
    lv_obj_set_height(row, spec->is_single_line ? PRODUCT_UI_ROW_SINGLE_H : PRODUCT_UI_ROW_DOUBLE_H);
    lv_obj_set_height(text_box, lv_obj_get_style_height(row, 0) - 2 * PRODUCT_UI_LIST_PAD_V);
    if (spec->is_single_line || (spec->subtitle == NULL) || (spec->subtitle[0] == '\0')) {
        lv_obj_add_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
    }
    if (product_ui_list_trailing_bind(trailing, spec) != 0) {
        return -1;
    }

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
        /* Avoid half-built rows lingering in the parent flex list. */
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
    return row;
}

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
