/**
 * @file product_ui_feedback.c
 * @brief Bounded Product UI feedback, toast, and confirmation components.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"

typedef struct {
    lv_obj_t *scrim;
    product_ui_confirm_cb_t callback;
    void *user_data;
    uint8_t is_pull_held;
} product_ui_confirm_state_t;

static product_ui_confirm_state_t s_confirm;
uint8_t product_ui_feedback_is_modal(void) { return s_confirm.scrim != NULL; }
static product_ui_motion_t s_confirm_motion;
static product_ui_motion_t s_toast_motion;

static void product_ui_feedback_opacity(void *context, int32_t opacity)
{
    lv_obj_set_style_opa(context, (lv_opa_t)opacity, 0);
}

static void product_ui_feedback_reveal(lv_obj_t *owner, product_ui_motion_t *motion)
{
    const product_ui_motion_binding_t binding = {
        .owner = owner, .context = owner, .apply = product_ui_feedback_opacity,
    };
    product_ui_motion_bind(motion, &binding);
    product_ui_motion_set(motion, product_ui_motion_is_reduced() ? LV_OPA_COVER : LV_OPA_70);
    product_ui_motion_start(motion, LV_OPA_COVER, PRODUCT_UI_MOTION_RELEASE_MS);
    product_ui_motion_apply_tree(owner);
}

static void product_ui_feedback_clean(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *product_ui_feedback_label(lv_obj_t *parent, const char *text,
                                           product_ui_style_e style)
{
    lv_obj_t *label;

    label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_add_style(label, product_ui_style(style), 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t *product_ui_feedback_action_create(
    lv_obj_t *parent, const product_ui_feedback_spec_t *spec)
{
    lv_obj_t *action;

    if (spec->action_label == NULL) {
        return parent;
    }
    if (spec->action_cb == NULL) {
        return NULL;
    }
    action = lv_button_create(parent);
    if (action == NULL) {
        return NULL;
    }
    lv_obj_set_size(action, 96, PRODUCT_UI_TOUCH_MIN);
    lv_obj_add_style(action, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_style(action, product_ui_style(PRODUCT_UI_STYLE_PRESSED),
                     LV_STATE_PRESSED);
    lv_obj_add_event_cb(action, spec->action_cb, LV_EVENT_CLICKED,
                        spec->user_data);
    if (product_ui_feedback_label(action, spec->action_label,
                                  PRODUCT_UI_STYLE_BODY) == NULL) {
        lv_obj_delete(action);
        return NULL;
    }
    lv_obj_center(lv_obj_get_child(action, 0));
    return action;
}

lv_obj_t *product_ui_feedback_create_ex(
    lv_obj_t *parent, const product_ui_feedback_spec_t *spec)
{
    static const char *symbols[] = {
        LV_SYMBOL_DIRECTORY,
        LV_SYMBOL_REFRESH,
        LV_SYMBOL_WARNING,
    };
    lv_obj_t *root;
    lv_obj_t *symbol;

    if ((parent == NULL) || (spec == NULL) || (spec->title == NULL) ||
        (spec->message == NULL) ||
        (spec->state > PRODUCT_UI_FEEDBACK_ERROR) ||
        ((spec->action_label == NULL) != (spec->action_cb == NULL))) {
        return NULL;
    }
    root = lv_obj_create(parent);
    if (root == NULL) {
        return NULL;
    }
    product_ui_feedback_clean(root);
    lv_obj_set_size(root, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, PRODUCT_UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(root, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    symbol = product_ui_feedback_label(root, symbols[spec->state],
                                       PRODUCT_UI_STYLE_TITLE);
    if ((symbol == NULL) ||
        (product_ui_feedback_label(root, spec->title,
                                   PRODUCT_UI_STYLE_BODY) == NULL) ||
        (product_ui_feedback_label(root, spec->message,
                                   PRODUCT_UI_STYLE_CAPTION) == NULL) ||
        (product_ui_feedback_action_create(root, spec) == NULL)) {
        lv_obj_delete(root);
        return NULL;
    }
    lv_obj_set_style_text_color(symbol,
        product_ui_color(spec->state == PRODUCT_UI_FEEDBACK_ERROR ?
                         PRODUCT_UI_COLOR_DANGER : PRODUCT_UI_COLOR_ACCENT), 0);
    return root;
}

lv_obj_t *product_ui_feedback_create(lv_obj_t *parent,
                                     product_ui_feedback_state_e state,
                                     const char *title,
                                     const char *message)
{
    product_ui_feedback_spec_t spec;

    spec = (product_ui_feedback_spec_t) {
        .state = state,
        .title = title,
        .message = message,
    };
    return product_ui_feedback_create_ex(parent, &spec);
}

enum {
    TOAST_WIDTH, TOAST_HEIGHT, TOAST_MIN_WIDTH, TOAST_MAX_WIDTH, TOAST_MIN_HEIGHT,
    TOAST_BG_OPA, TOAST_RADIUS, TOAST_PAD_LEFT, TOAST_PAD_RIGHT, TOAST_PAD_TOP,
    TOAST_PAD_BOTTOM, TOAST_ALIGN, TOAST_X, TOAST_Y,
};
static const lv_style_prop_t s_toast_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_MIN_WIDTH, LV_STYLE_MAX_WIDTH, LV_STYLE_MIN_HEIGHT,
    LV_STYLE_BG_OPA, LV_STYLE_RADIUS, LV_STYLE_PAD_LEFT, LV_STYLE_PAD_RIGHT, LV_STYLE_PAD_TOP,
    LV_STYLE_PAD_BOTTOM, LV_STYLE_ALIGN, LV_STYLE_X, LV_STYLE_Y,
};
#define TOAST_VALUE(property, number) {&s_toast_properties[TOAST_##property], {.num = (number)}}
static const lv_style_const_prop_t s_toast_geometry[] = {
    TOAST_VALUE(WIDTH, LV_SIZE_CONTENT), TOAST_VALUE(HEIGHT, LV_SIZE_CONTENT),
    TOAST_VALUE(MIN_WIDTH, 96), TOAST_VALUE(MAX_WIDTH, 248 + 2 * PRODUCT_UI_SPACE_MD),
    TOAST_VALUE(MIN_HEIGHT, 34), TOAST_VALUE(BG_OPA, LV_OPA_COVER), TOAST_VALUE(RADIUS, PRODUCT_UI_RADIUS_LG),
    TOAST_VALUE(PAD_LEFT, PRODUCT_UI_SPACE_MD), TOAST_VALUE(PAD_RIGHT, PRODUCT_UI_SPACE_MD),
    TOAST_VALUE(PAD_TOP, PRODUCT_UI_SPACE_SM), TOAST_VALUE(PAD_BOTTOM, PRODUCT_UI_SPACE_SM),
    TOAST_VALUE(ALIGN, LV_ALIGN_BOTTOM_MID), TOAST_VALUE(X, 0), TOAST_VALUE(Y, -PRODUCT_UI_SPACE_MD),
    LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_toast_geometry_style, (void *)s_toast_geometry);
#undef TOAST_VALUE

static lv_obj_t *s_toast_obj;
static lv_timer_t *s_toast_timer;

static void product_ui_toast_clear(void)
{
    product_ui_motion_dispose(&s_toast_motion);
    if (s_toast_timer != NULL) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast_obj != NULL) {
        lv_obj_delete(s_toast_obj);
        s_toast_obj = NULL;
    }
}

void product_ui_feedback_clear(void)
{
    lv_obj_t *scrim = s_confirm.scrim;

    product_ui_toast_clear();
    product_ui_motion_dispose(&s_confirm_motion);
    s_confirm.scrim = NULL;
    s_confirm.callback = NULL;
    s_confirm.user_data = NULL;
    product_ui_control_center_release_pull(&s_confirm.is_pull_held);
    if (scrim != NULL) {
        lv_obj_delete(scrim);
    }
}

static void product_ui_toast_delete_cb(lv_timer_t *timer)
{
    (void)timer;
    s_toast_timer = NULL;
    if (s_toast_obj != NULL) {
        lv_obj_delete(s_toast_obj);
        s_toast_obj = NULL;
    }
}

void product_ui_toast_show(const char *message, uint16_t duration_ms)
{
    lv_point_t text_size;
    if ((message == NULL) || (message[0] == '\0') || (duration_ms == 0U)) {
        return;
    }
    /* Single active toast: replace previous to avoid stacked blank pills. */
    product_ui_toast_clear();

    /* A label can own its background, padding and motion directly. Avoid a
     * wrapper whose only child is this text, especially while a page is retained. */
    s_toast_obj = lv_label_create(lv_layer_top());
    if (s_toast_obj == NULL) {
        return;
    }
    product_ui_feedback_clean(s_toast_obj);
    lv_obj_add_style(s_toast_obj, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_add_style(s_toast_obj, &s_toast_geometry_style, 0);
    lv_label_set_text(s_toast_obj, message);
    lv_label_set_long_mode(s_toast_obj, LV_LABEL_LONG_WRAP);
    lv_text_get_size(&text_size, message, product_ui_font(PRODUCT_UI_FONT_CAPTION),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    /* SIZE_CONTENT does not wrap just because max_width clips the object.
     * Give genuinely long messages a concrete content width before layout. */
    if (text_size.x > 248) {
        lv_obj_set_width(s_toast_obj, 248 + 2 * PRODUCT_UI_SPACE_MD);
    }
    /* A toast is feedback, not an input owner. It may overlap a bottom action
     * while the user continues editing immediately after a save. */
    lv_obj_remove_flag(s_toast_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    /*
     * ON_SURFACE is a light "ink on surface" token (≈白), PAGE 也是浅色。
     * 深色条 + 浅色字才是 toast 对比度；之前浅底浅字会显示成空白小胶囊。
     */
    lv_obj_set_style_bg_color(s_toast_obj,
                              product_ui_color(PRODUCT_UI_COLOR_STATUS_BG), 0);
    lv_obj_set_style_text_color(s_toast_obj,
                                product_ui_color(PRODUCT_UI_COLOR_ON_STATUS), 0);
    s_toast_timer = lv_timer_create(product_ui_toast_delete_cb, duration_ms,
                                    NULL);
    if (s_toast_timer == NULL) {
        product_ui_toast_clear();
        return;
    }
    lv_timer_set_repeat_count(s_toast_timer, 1);
    product_ui_feedback_reveal(s_toast_obj, &s_toast_motion);
}

static void product_ui_confirm_event_cb(lv_event_t *event)
{
    uint8_t confirmed;
    product_ui_confirm_cb_t callback;
    void *user_data;
    lv_obj_t *scrim;

    confirmed = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    callback = s_confirm.callback;
    user_data = s_confirm.user_data;
    scrim = s_confirm.scrim;
    product_ui_motion_dispose(&s_confirm_motion);
    s_confirm.scrim = NULL;
    s_confirm.callback = NULL;
    s_confirm.user_data = NULL;
    product_ui_control_center_release_pull(&s_confirm.is_pull_held);
    if (scrim != NULL) lv_obj_delete(scrim);
    if (callback != NULL) {
        callback(confirmed, user_data);
    }
}

static void product_ui_confirm_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_confirm.scrim) return;
    s_confirm.scrim = NULL;
    s_confirm.callback = NULL;
    s_confirm.user_data = NULL;
    product_ui_control_center_release_pull(&s_confirm.is_pull_held);
}

static lv_obj_t *product_ui_confirm_button(lv_obj_t *parent, const char *text,
                                           uint8_t confirmed)
{
    lv_obj_t *button;

    button = lv_button_create(parent);
    if (button == NULL) {
        return NULL;
    }
    lv_obj_set_size(button, 88, PRODUCT_UI_TOUCH_MIN);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_PRESSED),
                     LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, product_ui_confirm_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)confirmed);
    if (product_ui_feedback_label(button, text,
                                  PRODUCT_UI_STYLE_BODY) == NULL) {
        lv_obj_delete(button);
        return NULL;
    }
    lv_obj_center(lv_obj_get_child(button, 0));
    return button;
}

lv_obj_t *product_ui_confirm_dialog_show(const char *title,
                                         const char *message,
                                         product_ui_confirm_cb_t callback,
                                         void *user_data)
{
    lv_obj_t *dialog;
    lv_obj_t *actions;
    lv_obj_t *message_area;

    if ((title == NULL) || (message == NULL) || (s_confirm.scrim != NULL)) {
        return NULL;
    }
    s_confirm.scrim = lv_obj_create(lv_layer_top());
    if (s_confirm.scrim == NULL) {
        return NULL;
    }
    product_ui_control_center_hold_pull(&s_confirm.is_pull_held);
    lv_obj_add_event_cb(s_confirm.scrim, product_ui_confirm_deleted, LV_EVENT_DELETE, NULL);
    s_confirm.callback = callback;
    s_confirm.user_data = user_data;
    product_ui_feedback_clean(s_confirm.scrim);
    lv_obj_set_size(s_confirm.scrim, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_style_bg_color(s_confirm.scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_confirm.scrim, LV_OPA_50, 0);
    lv_obj_add_flag(s_confirm.scrim, LV_OBJ_FLAG_CLICKABLE);

    dialog = lv_obj_create(s_confirm.scrim);
    if (dialog == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    product_ui_feedback_clean(dialog);
    lv_obj_set_size(dialog, 272, 208);
    lv_obj_add_style(dialog, product_ui_style(PRODUCT_UI_STYLE_SURFACE), 0);
    lv_obj_set_style_pad_all(dialog, PRODUCT_UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(dialog, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(dialog);
    if (product_ui_feedback_label(dialog, title, PRODUCT_UI_STYLE_TITLE) == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    message_area = lv_obj_create(dialog);
    if (message_area == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    product_ui_feedback_clean(message_area);
    lv_obj_set_width(message_area, LV_PCT(100));
    lv_obj_set_flex_grow(message_area, 1);
    lv_obj_add_flag(message_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(message_area, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(message_area, LV_DIR_VER);
    if (product_ui_feedback_label(message_area, message, PRODUCT_UI_STYLE_BODY) == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    actions = lv_obj_create(dialog);
    if (actions == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    product_ui_feedback_clean(actions);
    lv_obj_set_size(actions, LV_PCT(100), PRODUCT_UI_TOUCH_MIN);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if ((product_ui_confirm_button(actions, "取消", 0) == NULL) ||
        (product_ui_confirm_button(actions, "确定", 1) == NULL)) {
        product_ui_feedback_clear();
        return NULL;
    }
    product_ui_feedback_reveal(s_confirm.scrim, &s_confirm_motion);
    return s_confirm.scrim;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
