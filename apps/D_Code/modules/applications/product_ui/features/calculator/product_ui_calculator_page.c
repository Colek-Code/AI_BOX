/**
 * @file product_ui_calculator_page.c
 * @brief Product UI calculator keypad and page registration.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/calculator/product_ui_calculator_page.h"
#include "modules/applications/product_ui/features/calculator/product_ui_calculator_model.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "modules/applications/product_ui/product_ui.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

#define PRODUCT_UI_CALCULATOR_DISPLAY_W    114
#define PRODUCT_UI_CALCULATOR_CONTENT_PAD    6
#define PRODUCT_UI_CALCULATOR_LAYOUT_GAP     4
#define PRODUCT_UI_CALCULATOR_KEY_GAP        2
#define PRODUCT_UI_CALCULATOR_INNER_W      \
    (PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_CALCULATOR_CONTENT_PAD))
#define PRODUCT_UI_CALCULATOR_INNER_H      \
    (PRODUCT_UI_CONTENT_H - (2 * PRODUCT_UI_CALCULATOR_CONTENT_PAD))
#define PRODUCT_UI_CALCULATOR_KEYPAD_W     \
    (PRODUCT_UI_CALCULATOR_INNER_W - PRODUCT_UI_CALCULATOR_DISPLAY_W - \
     PRODUCT_UI_CALCULATOR_LAYOUT_GAP)
#define PRODUCT_UI_CALCULATOR_KEY_W        \
    ((PRODUCT_UI_CALCULATOR_KEYPAD_W - (4 * PRODUCT_UI_CALCULATOR_KEY_GAP)) / 5)
#define PRODUCT_UI_CALCULATOR_KEY_H        \
    ((PRODUCT_UI_CALCULATOR_INNER_H - (3 * PRODUCT_UI_CALCULATOR_KEY_GAP)) / 4)

#if (PRODUCT_UI_CALCULATOR_KEY_W < PRODUCT_UI_TOUCH_MIN) || \
    (PRODUCT_UI_CALCULATOR_KEY_H < PRODUCT_UI_TOUCH_MIN)
#error "Calculator keys must meet the Product UI minimum touch target"
#endif

typedef struct {
    const char *label;
    product_ui_calculator_key_e key;
    uint8_t is_action;
    uint8_t is_danger;
} product_ui_calculator_key_spec_t;

typedef struct {
    product_ui_page_view_t page;
    product_ui_calculator_model_t model;
    lv_obj_t *display;
    lv_obj_t *expression;
} product_ui_calculator_page_t;

static const int32_t s_calculator_columns[] = {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST,
};

static const int32_t s_calculator_rows[] = {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST,
};

static const int32_t s_calculator_layout_columns[] = {
    PRODUCT_UI_CALCULATOR_DISPLAY_W,
    PRODUCT_UI_CALCULATOR_KEYPAD_W,
    LV_GRID_TEMPLATE_LAST,
};

static const int32_t s_calculator_layout_rows[] = {
    PRODUCT_UI_CALCULATOR_INNER_H,
    LV_GRID_TEMPLATE_LAST,
};

static const product_ui_calculator_key_spec_t s_calculator_keys[] = {
    {"7", PRODUCT_UI_CALCULATOR_KEY_7, 0, 0},
    {"8", PRODUCT_UI_CALCULATOR_KEY_8, 0, 0},
    {"9", PRODUCT_UI_CALCULATOR_KEY_9, 0, 0},
    {"C", PRODUCT_UI_CALCULATOR_KEY_CLEAR, 0, 1},
    {"/", PRODUCT_UI_CALCULATOR_KEY_DIVIDE, 1, 0},
    {"4", PRODUCT_UI_CALCULATOR_KEY_4, 0, 0},
    {"5", PRODUCT_UI_CALCULATOR_KEY_5, 0, 0},
    {"6", PRODUCT_UI_CALCULATOR_KEY_6, 0, 0},
    {"<-", PRODUCT_UI_CALCULATOR_KEY_BACKSPACE, 0, 0},
    {"*", PRODUCT_UI_CALCULATOR_KEY_MULTIPLY, 1, 0},
    {"1", PRODUCT_UI_CALCULATOR_KEY_1, 0, 0},
    {"2", PRODUCT_UI_CALCULATOR_KEY_2, 0, 0},
    {"3", PRODUCT_UI_CALCULATOR_KEY_3, 0, 0},
    {"+/-", PRODUCT_UI_CALCULATOR_KEY_SIGN, 0, 0},
    {"-", PRODUCT_UI_CALCULATOR_KEY_SUBTRACT, 1, 0},
    {"0", PRODUCT_UI_CALCULATOR_KEY_0, 0, 0},
    {".", PRODUCT_UI_CALCULATOR_KEY_DECIMAL, 0, 0},
    {"%", PRODUCT_UI_CALCULATOR_KEY_PERCENT, 0, 0},
    {"=", PRODUCT_UI_CALCULATOR_KEY_EQUALS, 1, 0},
    {"+", PRODUCT_UI_CALCULATOR_KEY_ADD, 1, 0},
};

static product_ui_calculator_page_t s_calculator_page;

static void product_ui_calculator_clean(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_CLICK_FOCUSABLE |
                      LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
}

static lv_obj_t *product_ui_calculator_label_create(
    lv_obj_t *parent, product_ui_style_e style)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL) {
        return NULL;
    }
    lv_obj_add_style(label, product_ui_style(style), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    return label;
}

static void product_ui_calculator_refresh(void)
{
    product_ui_calculator_model_t *model = &s_calculator_page.model;
    const char *display_text;
    product_ui_font_e display_font;

    display_text = product_ui_calculator_model_display(model);
    display_font = strlen(display_text) > 9U ?
                   PRODUCT_UI_FONT_TITLE : PRODUCT_UI_FONT_DISPLAY_NUMBER;
    lv_label_set_text(s_calculator_page.display, display_text);
    lv_label_set_text(s_calculator_page.expression,
                      product_ui_calculator_model_expression(model));
    lv_obj_set_style_text_font(s_calculator_page.display,
                               product_ui_font(display_font), 0);
    lv_obj_set_style_text_color(
        s_calculator_page.display,
        product_ui_color(model->state == PRODUCT_UI_CALCULATOR_STATE_ERROR ?
                         PRODUCT_UI_COLOR_DANGER_TEXT : PRODUCT_UI_COLOR_TEXT), 0);
}

static void product_ui_calculator_key_cb(lv_event_t *event)
{
    const product_ui_calculator_key_spec_t *spec;
    lv_obj_t *button;
    lv_area_t area;
    lv_point_t point = {0};
    lv_indev_t *indev = lv_indev_active();

    spec = lv_event_get_user_data(event);
    if (spec == NULL) {
        return;
    }
    button = lv_event_get_current_target(event);
    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
    }
    if (button != NULL) {
        lv_obj_get_coords(button, &area);
        PRODUCT_UI_TRACE("calculator key=%s point=%d,%d area=%d,%d,%d,%d\n",
                         spec->label, point.x, point.y,
                         area.x1, area.y1, area.x2, area.y2);
    }
    product_ui_calculator_model_press(&s_calculator_page.model, spec->key);
    product_ui_calculator_refresh();
}

static int product_ui_calculator_display_create(lv_obj_t *content)
{
    lv_obj_t *display_box;

    display_box = lv_obj_create(content);
    if (display_box == NULL) {
        return -1;
    }
    product_ui_calculator_clean(display_box);
    lv_obj_add_style(display_box,
                     product_ui_style(PRODUCT_UI_STYLE_SURFACE), 0);
    lv_obj_set_grid_cell(display_box, LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_border_width(display_box, 0, 0);
    lv_obj_set_style_radius(display_box, PRODUCT_UI_RADIUS_MD, 0);
    lv_obj_set_style_pad_hor(display_box, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_set_style_pad_ver(display_box, 1, 0);
    lv_obj_set_style_pad_row(display_box, 0, 0);
    lv_obj_set_flex_flow(display_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(display_box, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    s_calculator_page.expression = product_ui_calculator_label_create(
        display_box, PRODUCT_UI_STYLE_CAPTION);
    s_calculator_page.display = product_ui_calculator_label_create(
        display_box, PRODUCT_UI_STYLE_TITLE);
    if ((s_calculator_page.expression == NULL) ||
        (s_calculator_page.display == NULL)) {
        return -1;
    }
    lv_obj_set_width(s_calculator_page.expression, lv_pct(100));
    lv_obj_set_style_text_color(
        s_calculator_page.expression,
        product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(s_calculator_page.expression,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_calculator_page.expression, LV_LABEL_LONG_DOT);

    lv_obj_set_width(s_calculator_page.display, lv_pct(100));
    lv_obj_set_flex_grow(s_calculator_page.display, 1);
    lv_obj_set_style_text_align(s_calculator_page.display,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_calculator_page.display, LV_LABEL_LONG_DOT);
    return 0;
}

static lv_obj_t *product_ui_calculator_button_create(
    lv_obj_t *keypad, const product_ui_calculator_key_spec_t *spec)
{
    lv_obj_t *button;
    lv_obj_t *label;
    product_ui_color_e background = PRODUCT_UI_COLOR_SURFACE;
    product_ui_color_e foreground = PRODUCT_UI_COLOR_TEXT;

    button = lv_button_create(keypad);
    if (button == NULL) {
        return NULL;
    }
    product_ui_calculator_clean(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_PRESSED),
                     LV_STATE_PRESSED);
    if (spec->is_action) {
        background = PRODUCT_UI_COLOR_ACTION;
        foreground = PRODUCT_UI_COLOR_ACTION_TEXT;
    } else if (spec->is_danger) {
        background = PRODUCT_UI_COLOR_ACTION_DANGER;
        foreground = PRODUCT_UI_COLOR_ACTION_TEXT;
    }
    lv_obj_set_style_bg_color(button, product_ui_color(background), 0);
    if (spec->is_action || spec->is_danger) {
        lv_obj_set_style_bg_color(button, lv_color_darken(product_ui_color(background), LV_OPA_20),
                                  LV_STATE_PRESSED);
    }
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_event_cb(button, product_ui_calculator_key_cb,
                        LV_EVENT_CLICKED, (void *)spec);

    label = product_ui_calculator_label_create(button, PRODUCT_UI_STYLE_BODY);
    if (label == NULL) {
        lv_obj_delete(button);
        return NULL;
    }
    lv_label_set_text(label, spec->label);
    lv_obj_set_style_text_color(label, product_ui_color(foreground), 0);
    lv_obj_center(label);
    return button;
}

static int product_ui_calculator_keypad_create(lv_obj_t *content)
{
    lv_obj_t *keypad;
    lv_obj_t *button;
    uint8_t index;

    keypad = lv_obj_create(content);
    if (keypad == NULL) {
        return -1;
    }
    product_ui_calculator_clean(keypad);
    lv_obj_set_grid_cell(keypad, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_bg_opa(keypad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(keypad, 0, 0);
    lv_obj_set_layout(keypad, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(keypad, s_calculator_columns, s_calculator_rows);
    lv_obj_set_style_pad_column(keypad, PRODUCT_UI_CALCULATOR_KEY_GAP, 0);
    lv_obj_set_style_pad_row(keypad, PRODUCT_UI_CALCULATOR_KEY_GAP, 0);

    for (index = 0; index <
         (uint8_t)(sizeof(s_calculator_keys) / sizeof(s_calculator_keys[0]));
         index++) {
        button = product_ui_calculator_button_create(
            keypad, &s_calculator_keys[index]);
        if (button == NULL) {
            return -1;
        }
        lv_obj_set_grid_cell(button, LV_GRID_ALIGN_STRETCH, index % 5U, 1,
                             LV_GRID_ALIGN_STRETCH, index / 5U, 1);
    }
#ifdef D_PRODUCT_UI_INTERACTION_TRACE_ENABLE
    lv_obj_update_layout(keypad);
    for (index = 0; index <
         (uint8_t)(sizeof(s_calculator_keys) / sizeof(s_calculator_keys[0]));
         index++) {
        lv_area_t area;

        button = lv_obj_get_child(keypad, index);
        lv_obj_get_coords(button, &area);
        PRODUCT_UI_TRACE("calculator bounds key=%s area=%d,%d,%d,%d\n",
                         s_calculator_keys[index].label,
                         area.x1, area.y1, area.x2, area.y2);
    }
#endif
    return 0;
}

int product_ui_calculator_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t page_spec = {
        .title = "计算器",
        .back_title = "桌面",
        .show_back = 1,
        .role = PRODUCT_UI_PAGE_ROLE_CONTENT,
    };
    if (product_ui_page_frame_create_ex(parent, &page_spec,
                                        &s_calculator_page.page) != 0) {
        return -1;
    }
    lv_obj_clear_flag(s_calculator_page.page.content,
                      LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_calculator_page.page.content,
                             PRODUCT_UI_CALCULATOR_CONTENT_PAD, 0);
    lv_obj_set_layout(s_calculator_page.page.content, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(s_calculator_page.page.content,
                              s_calculator_layout_columns,
                              s_calculator_layout_rows);
    lv_obj_set_style_pad_column(s_calculator_page.page.content,
                                PRODUCT_UI_CALCULATOR_LAYOUT_GAP, 0);
    lv_obj_set_style_pad_row(s_calculator_page.page.content,
                             0, 0);
    product_ui_calculator_model_init(&s_calculator_page.model);
    if ((product_ui_calculator_display_create(
             s_calculator_page.page.content) != 0) ||
        (product_ui_calculator_keypad_create(
             s_calculator_page.page.content) != 0)) {
        return -1;
    }
    product_ui_calculator_refresh();
    return 0;
}

static void product_ui_calculator_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
}

static const page_ops_t s_product_ui_calculator_ops = {
    .create = product_ui_calculator_page_create,
    .update = product_ui_calculator_page_update,
    .flags = PAGE_FLAG_KEEP_ALIVE | PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_product_ui_calculator_desc = {
    .name = "ProductCalculator",
    .ops = &s_product_ui_calculator_ops,
};

int product_ui_calculator_register_page(int page_type)
{
    s_product_ui_calculator_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_calculator_desc);
}

D_UI_MODULE_DEFINE(calculator, "计算器", LV_SYMBOL_EDIT,
                product_ui_calculator_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
