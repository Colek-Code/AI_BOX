/**
 * @file product_ui_frame.c
 * @brief Status, navigation, and page-frame components.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "framework/d_page_manager.h"
#include <string.h>

static product_ui_status_view_t *s_status_views;

typedef struct {
    product_ui_status_view_t *view;
    lv_obj_t **root_out;
    uint8_t allow_pull;
    uint8_t is_light;
    uint8_t hide_connectivity;
} product_ui_status_create_params_t;

static void product_ui_status_view_apply(
    product_ui_status_view_t *view, const product_ui_status_t *status);

static void product_ui_status_unregister(product_ui_status_view_t *view)
{
    product_ui_status_view_t **link;

    if (view == NULL) {
        return;
    }
    link = &s_status_views;
    while (*link != NULL) {
        if (*link == view) {
            *link = view->next;
            break;
        }
        link = &(*link)->next;
    }
    view->next = NULL;
    view->is_registered = 0U;
}

static void product_ui_clean_obj(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *product_ui_make_label(lv_obj_t *parent, const char *text,
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

static const lv_style_prop_t s_status_icon_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_IMAGE_RECOLOR_OPA,
};
static const lv_style_const_prop_t s_status_icon_geometry[] = {
    {&s_status_icon_properties[0], {.num = PRODUCT_UI_STATUS_BT_ICON}},
    {&s_status_icon_properties[1], {.num = PRODUCT_UI_STATUS_BT_ICON}},
    {&s_status_icon_properties[2], {.num = LV_OPA_COVER}}, LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_status_icon_style, (void *)s_status_icon_geometry);

static lv_obj_t *product_ui_make_status_icon(lv_obj_t *parent,
                                             product_ui_icon_id_t icon_id)
{
    const lv_image_dsc_t *source;
    lv_obj_t *image;

    source = product_ui_icon_get_sized(icon_id, PRODUCT_UI_STATUS_BT_ICON);
    if (source == NULL) {
        return NULL;
    }
    image = lv_image_create(parent);
    if (image == NULL) {
        return NULL;
    }
    lv_image_set_src(image, source);
    /* The generated glyph and layout box share their native dimensions. */
    lv_obj_add_style(image, &s_status_icon_style, 0);
    lv_image_set_scale(image, LV_SCALE_NONE);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return image;
}

/*
 * Status-bar WiFi: Cipher-style 4 rising bars (wifi_level 0..4).
 * Settings rows keep PRODUCT_UI_ICON_WIFI asset separately.
 */
/* Signal geometry is identical on every page, including both pages retained
 * during a transactional transition. Keep it in Flash instead of local styles. */
static const lv_style_prop_t s_signal_geometry_props[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_X, LV_STYLE_Y, LV_STYLE_RADIUS, LV_STYLE_BORDER_WIDTH
};
#define PRODUCT_UI_SIGNAL_GEOMETRY(name, index, height) \
    static const lv_style_const_prop_t name[] = { \
        {&s_signal_geometry_props[0], {.num = PRODUCT_UI_STATUS_WIFI_BAR_W}}, \
        {&s_signal_geometry_props[1], {.num = height}}, \
        {&s_signal_geometry_props[2], {.num = index * (PRODUCT_UI_STATUS_WIFI_BAR_W + PRODUCT_UI_STATUS_WIFI_BAR_GAP)}}, \
        {&s_signal_geometry_props[3], {.num = PRODUCT_UI_STATUS_ICON_H - height}}, \
        {&s_signal_geometry_props[4], {.num = 1}}, {&s_signal_geometry_props[5], {.num = 0}}, LV_STYLE_CONST_PROPS_END \
    }
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_0, 0, PRODUCT_UI_STATUS_WIFI_BAR_H0);
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_1, 1, PRODUCT_UI_STATUS_WIFI_BAR_H1);
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_2, 2, PRODUCT_UI_STATUS_WIFI_BAR_H2);
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_3, 3, PRODUCT_UI_STATUS_WIFI_BAR_H3);
#undef PRODUCT_UI_SIGNAL_GEOMETRY
static LV_STYLE_CONST_INIT(s_signal_0_style, (void *)s_signal_0);
static LV_STYLE_CONST_INIT(s_signal_1_style, (void *)s_signal_1);
static LV_STYLE_CONST_INIT(s_signal_2_style, (void *)s_signal_2);
static LV_STYLE_CONST_INIT(s_signal_3_style, (void *)s_signal_3);
static const lv_style_t *const s_signal_styles[] = {
    &s_signal_0_style, &s_signal_1_style, &s_signal_2_style, &s_signal_3_style
};

/* Status geometry is repeated on the home page, control center and both sides
 * of a page transition. Only values that vary with live status stay local. */
enum {
    STATUS_WIDTH, STATUS_HEIGHT, STATUS_X, STATUS_Y, STATUS_BG_OPA, STATUS_BORDER,
    STATUS_RADIUS, STATUS_PAD_LEFT, STATUS_PAD_RIGHT, STATUS_PAD_TOP, STATUS_PAD_BOTTOM,
    STATUS_ALIGN, STATUS_TEXT_ALIGN, STATUS_LAYOUT, STATUS_FLEX_FLOW, STATUS_FLEX_GROW,
    STATUS_FLEX_MAIN, STATUS_FLEX_CROSS, STATUS_FLEX_TRACK, STATUS_PAD_COLUMN,
};
static const lv_style_prop_t s_status_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_X, LV_STYLE_Y, LV_STYLE_BG_OPA, LV_STYLE_BORDER_WIDTH,
    LV_STYLE_RADIUS, LV_STYLE_PAD_LEFT, LV_STYLE_PAD_RIGHT, LV_STYLE_PAD_TOP, LV_STYLE_PAD_BOTTOM,
    LV_STYLE_ALIGN, LV_STYLE_TEXT_ALIGN, LV_STYLE_LAYOUT, LV_STYLE_FLEX_FLOW, LV_STYLE_FLEX_GROW,
    LV_STYLE_FLEX_MAIN_PLACE, LV_STYLE_FLEX_CROSS_PLACE, LV_STYLE_FLEX_TRACK_PLACE, LV_STYLE_PAD_COLUMN,
};
#define STATUS_VALUE(property, number) {&s_status_properties[STATUS_##property], {.num = (number)}}
#define STATUS_ZERO_PADDING \
    STATUS_VALUE(PAD_LEFT, 0), STATUS_VALUE(PAD_RIGHT, 0), STATUS_VALUE(PAD_TOP, 0), STATUS_VALUE(PAD_BOTTOM, 0)
static const lv_style_const_prop_t s_wifi_box_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_WIFI_BAR_COUNT * PRODUCT_UI_STATUS_WIFI_BAR_W +
                       (PRODUCT_UI_WIFI_BAR_COUNT - 1U) * PRODUCT_UI_STATUS_WIFI_BAR_GAP),
    STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_ICON_H), STATUS_VALUE(BG_OPA, LV_OPA_TRANSP),
    STATUS_VALUE(BORDER, 0), STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_host_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_BODY_W + PRODUCT_UI_STATUS_BAT_TIP_GAP + PRODUCT_UI_STATUS_BAT_TIP_W),
    STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_BODY_H), STATUS_VALUE(BG_OPA, LV_OPA_TRANSP),
    STATUS_VALUE(BORDER, 0), STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_body_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_BODY_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_BODY_H),
    STATUS_VALUE(X, 0), STATUS_VALUE(Y, 0), STATUS_VALUE(BG_OPA, LV_OPA_TRANSP),
    STATUS_VALUE(BORDER, 1), STATUS_VALUE(RADIUS, 2), STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_fill_geometry[] = {
    STATUS_VALUE(WIDTH, 0), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_FILL_H),
    STATUS_VALUE(X, PRODUCT_UI_STATUS_BAT_INSET), STATUS_VALUE(Y, PRODUCT_UI_STATUS_BAT_INSET),
    STATUS_VALUE(BG_OPA, LV_OPA_COVER), STATUS_VALUE(BORDER, 0), STATUS_VALUE(RADIUS, 0),
    STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_tip_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_TIP_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_TIP_H),
    STATUS_VALUE(X, PRODUCT_UI_STATUS_BAT_BODY_W + PRODUCT_UI_STATUS_BAT_TIP_GAP),
    STATUS_VALUE(Y, (PRODUCT_UI_STATUS_BAT_BODY_H - PRODUCT_UI_STATUS_BAT_TIP_H) / 2U),
    STATUS_VALUE(BG_OPA, LV_OPA_COVER), STATUS_VALUE(BORDER, 0), STATUS_VALUE(RADIUS, 1),
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_charge_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_BODY_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_BODY_H),
    STATUS_VALUE(ALIGN, LV_ALIGN_LEFT_MID), STATUS_VALUE(X, 0), STATUS_VALUE(Y, 0),
    STATUS_VALUE(TEXT_ALIGN, LV_TEXT_ALIGN_CENTER), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_status_root_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_SCREEN_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_H),
    STATUS_VALUE(X, 0), STATUS_VALUE(Y, 0), STATUS_VALUE(LAYOUT, LV_LAYOUT_FLEX),
    STATUS_VALUE(FLEX_FLOW, LV_FLEX_FLOW_ROW), STATUS_VALUE(FLEX_MAIN, LV_FLEX_ALIGN_START),
    STATUS_VALUE(FLEX_CROSS, LV_FLEX_ALIGN_CENTER), STATUS_VALUE(FLEX_TRACK, LV_FLEX_ALIGN_CENTER),
    STATUS_VALUE(PAD_COLUMN, PRODUCT_UI_STATUS_ICON_GAP), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_status_spacer_geometry[] = {
    STATUS_VALUE(WIDTH, 1), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_H),
    STATUS_VALUE(FLEX_GROW, 1), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_nav_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_SCREEN_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_NAV_H),
    STATUS_VALUE(X, 0), STATUS_VALUE(Y, PRODUCT_UI_STATUS_H), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_nav_back_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_TOUCH_MIN), STATUS_VALUE(HEIGHT, PRODUCT_UI_TOUCH_MIN),
    STATUS_VALUE(ALIGN, LV_ALIGN_LEFT_MID), STATUS_VALUE(X, PRODUCT_UI_PAGE_PAD), STATUS_VALUE(Y, 0),
    LV_STYLE_CONST_PROPS_END
};
#define PRODUCT_UI_CONTENT_GEOMETRY(name, content_height, content_y) \
    static const lv_style_const_prop_t name[] = { \
        STATUS_VALUE(WIDTH, PRODUCT_UI_SCREEN_W), STATUS_VALUE(HEIGHT, content_height), \
        STATUS_VALUE(X, 0), STATUS_VALUE(Y, content_y), \
        STATUS_VALUE(PAD_LEFT, PRODUCT_UI_PAGE_PAD), STATUS_VALUE(PAD_RIGHT, PRODUCT_UI_PAGE_PAD), \
        STATUS_VALUE(PAD_TOP, PRODUCT_UI_PAGE_PAD), STATUS_VALUE(PAD_BOTTOM, PRODUCT_UI_PAGE_PAD), \
        LV_STYLE_CONST_PROPS_END \
    }
PRODUCT_UI_CONTENT_GEOMETRY(s_page_content_geometry, PRODUCT_UI_CONTENT_H,
                            PRODUCT_UI_STATUS_H + PRODUCT_UI_NAV_H);
PRODUCT_UI_CONTENT_GEOMETRY(s_home_content_geometry, PRODUCT_UI_SCREEN_H - PRODUCT_UI_STATUS_H,
                            PRODUCT_UI_STATUS_H);
#undef PRODUCT_UI_CONTENT_GEOMETRY
static LV_STYLE_CONST_INIT(s_wifi_box_style, (void *)s_wifi_box_geometry);
static LV_STYLE_CONST_INIT(s_battery_host_style, (void *)s_battery_host_geometry);
static LV_STYLE_CONST_INIT(s_battery_body_style, (void *)s_battery_body_geometry);
static LV_STYLE_CONST_INIT(s_battery_fill_style, (void *)s_battery_fill_geometry);
static LV_STYLE_CONST_INIT(s_battery_tip_style, (void *)s_battery_tip_geometry);
static LV_STYLE_CONST_INIT(s_battery_charge_style, (void *)s_battery_charge_geometry);
static LV_STYLE_CONST_INIT(s_status_root_style, (void *)s_status_root_geometry);
static LV_STYLE_CONST_INIT(s_status_spacer_style, (void *)s_status_spacer_geometry);
static LV_STYLE_CONST_INIT(s_nav_geometry_style, (void *)s_nav_geometry);
static LV_STYLE_CONST_INIT(s_nav_back_style, (void *)s_nav_back_geometry);
static LV_STYLE_CONST_INIT(s_page_content_style, (void *)s_page_content_geometry);
static LV_STYLE_CONST_INIT(s_home_content_style, (void *)s_home_content_geometry);
#undef STATUS_ZERO_PADDING
#undef STATUS_VALUE

static int product_ui_wifi_signal_create(product_ui_status_view_t *view,
                                         lv_obj_t *parent)
{
    uint8_t i;

    if ((view == NULL) || (parent == NULL)) {
        return -1;
    }
    view->wifi = lv_obj_create(parent);
    if (view->wifi == NULL) {
        return -1;
    }
    product_ui_clean_obj(view->wifi);
    lv_obj_add_style(view->wifi, &s_wifi_box_style, 0);
    lv_obj_clear_flag(view->wifi, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++) {
        view->wifi_bar[i] = lv_obj_create(view->wifi);
        if (view->wifi_bar[i] == NULL) {
            return -1;
        }
        product_ui_clean_obj(view->wifi_bar[i]);
        lv_obj_add_style(view->wifi_bar[i], s_signal_styles[i], 0);
        lv_obj_set_style_bg_opa(view->wifi_bar[i], LV_OPA_30, 0);
        lv_obj_clear_flag(view->wifi_bar[i], LV_OBJ_FLAG_CLICKABLE |
                          LV_OBJ_FLAG_SCROLLABLE);
    }
    return 0;
}

/*
 * Portrait Cipher battery style (cipher_panel_demo.c cipher_battery_icon):
 *   body 26x14 border shell (transparent fill)
 *   liquid at +2,+2 size (22*pct/100) x 10
 *   tip 2x6 at body_w+gap
 * Host is a fixed flex unit so tip cannot collide with peers.
 */
static int product_ui_battery_create(product_ui_status_view_t *view,
                                     lv_obj_t *parent)
{
    if ((view == NULL) || (parent == NULL)) {
        return -1;
    }
    view->battery_host = lv_obj_create(parent);
    if (view->battery_host == NULL) {
        return -1;
    }
    product_ui_clean_obj(view->battery_host);
    lv_obj_add_style(view->battery_host, &s_battery_host_style, 0);
    lv_obj_clear_flag(view->battery_host, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE);

    /* Outer shell: border only (Cipher). */
    view->battery = lv_obj_create(view->battery_host);
    if (view->battery == NULL) {
        return -1;
    }
    product_ui_clean_obj(view->battery);
    lv_obj_add_style(view->battery, &s_battery_body_style, 0);
    lv_obj_clear_flag(view->battery, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Fill is sibling of body under host (not pad-based child). Matches Cipher
     * absolute inset and avoids pre-layout content_width = 0 empty shells.
     */
    view->battery_fill = lv_obj_create(view->battery_host);
    view->battery_tip = lv_obj_create(view->battery_host);
    view->battery_charge = product_ui_make_label(view->battery_host,
                                                  LV_SYMBOL_CHARGE,
                                                  PRODUCT_UI_STYLE_CAPTION);
    if ((view->battery_fill == NULL) || (view->battery_tip == NULL) ||
        (view->battery_charge == NULL)) {
        return -1;
    }
    product_ui_clean_obj(view->battery_fill);
    product_ui_clean_obj(view->battery_tip);
    lv_obj_add_style(view->battery_fill, &s_battery_fill_style, 0);
    lv_obj_clear_flag(view->battery_fill, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_style(view->battery_tip, &s_battery_tip_style, 0);
    lv_obj_clear_flag(view->battery_tip, LV_OBJ_FLAG_CLICKABLE);

    /* Center charge glyph on body rect (host is wider by tip). */
    lv_obj_add_style(view->battery_charge, &s_battery_charge_style, 0);
    lv_obj_add_flag(view->battery_charge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view->battery_charge, LV_OBJ_FLAG_CLICKABLE);
    return 0;
}

static void product_ui_status_delete_cb(lv_event_t *event)
{
    product_ui_status_view_t *view;

    view = lv_event_get_user_data(event);
    if (view != NULL) {
        product_ui_status_unregister(view);
        memset(view, 0, sizeof(*view));
    }
}

static int product_ui_status_register(product_ui_status_view_t *view)
{
    if ((view == NULL) || (view->root == NULL) || view->is_registered) {
        return -1;
    }
    view->next = s_status_views;
    view->is_registered = 1U;
    s_status_views = view;
    lv_obj_add_event_cb(view->root, product_ui_status_delete_cb,
                        LV_EVENT_DELETE, view);
    return 0;
}

static lv_obj_t *product_ui_status_root_create(lv_obj_t *parent,
                                               uint8_t is_transparent)
{
    lv_obj_t *root;

    root = lv_obj_create(parent);
    if (root == NULL) {
        return NULL;
    }
    product_ui_clean_obj(root);
    lv_obj_add_style(root, product_ui_style(PRODUCT_UI_STYLE_STATUS), 0);
    if (is_transparent) {
        lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_set_style_bg_color(root, product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    }
    /* Keep time | spacer | wifi | bt | battery from colliding. */
    lv_obj_add_style(root, &s_status_root_style, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    return root;
}

static int product_ui_status_finish(
    const product_ui_status_create_params_t *params, lv_obj_t *spacer)
{
    product_ui_status_view_t *view = params->view;

    if (params->is_light) {
        lv_obj_set_style_text_color(view->time, lv_color_white(), 0);
    }
    product_ui_clean_obj(spacer);
    lv_obj_set_width(view->time, 40);
    lv_obj_add_style(spacer, &s_status_spacer_style, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_CLICK_FOCUSABLE);
    if (params->allow_pull) {
        product_ui_control_center_bind_pull(view->root);
    }
    if (product_ui_status_register(view) != 0) {
        return -1;
    }
    if (params->root_out != NULL) {
        *params->root_out = view->root;
    }
    return 0;
}

static int product_ui_status_create(
    lv_obj_t *parent, const product_ui_status_create_params_t *params)
{
    product_ui_status_t status;
    product_ui_status_view_t *view;
    lv_obj_t *spacer;

    if ((params == NULL) || (params->view == NULL)) {
        return -1;
    }
    view = params->view;
    memset(view, 0, sizeof(*view));
    view->root = product_ui_status_root_create(
        parent, params->is_light);
    if (view->root == NULL) {
        return -1;
    }
    view->is_light = params->is_light;
    view->hide_connectivity = params->hide_connectivity;
    /*
     * Time uses the STATUS font role. Do not attach
     * PRODUCT_UI_STYLE_STATUS (it carries status-bar background).
     */
    view->time = product_ui_make_label(view->root, "--:--",
                                       PRODUCT_UI_STYLE_CAPTION);
    if (view->time != NULL) {
        lv_obj_set_style_text_font(view->time,
                                   product_ui_font(PRODUCT_UI_FONT_STATUS), 0);
    }
    spacer = lv_obj_create(view->root);
    if (product_ui_wifi_signal_create(view, view->root) != 0) {
        return -1;
    }
    view->bluetooth = product_ui_make_status_icon(view->root,
                                                   PRODUCT_UI_ICON_BLUETOOTH);
    if ((view->time == NULL) || (spacer == NULL) ||
        (view->bluetooth == NULL) || (view->wifi == NULL) ||
        (product_ui_battery_create(view, view->root) != 0)) {
        return -1;
    }
    view->battery_text = product_ui_make_label(view->root, "",
                                                PRODUCT_UI_STYLE_CAPTION);
    if (view->battery_text == NULL) {
        return -1;
    }

    product_ui_status_snapshot(&status);
    product_ui_status_view_apply(view, &status);

    return product_ui_status_finish(params, spacer);
}

static void product_ui_back_cb(lv_event_t *event)
{
    (void)event;
    PRODUCT_UI_TRACE("nav_back action=history\n");
    if (product_ui_navigation_back() != 0) {
        product_ui_toast_show("返回失败", 1200U);
    }
}

static int product_ui_nav_back_create(lv_obj_t *nav,
                                      const product_ui_page_spec_t *spec)
{
    lv_obj_t *back;
    lv_event_cb_t callback;
    void *user_data;

    back = lv_button_create(nav);
    if (back == NULL) {
        return -1;
    }
    product_ui_clean_obj(back);
    lv_obj_add_style(back, &s_nav_back_style, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_PRESS_LOCK);
    callback = spec->nav_back_cb != NULL ? spec->nav_back_cb :
                                          product_ui_back_cb;
    user_data = spec->nav_back_cb != NULL ? spec->nav_back_user_data : NULL;
    lv_obj_add_event_cb(back, callback, LV_EVENT_CLICKED, user_data);
    back = product_ui_make_label(back, LV_SYMBOL_LEFT, PRODUCT_UI_STYLE_BODY);
    if (back == NULL) {
        return -1;
    }
    lv_obj_set_style_text_color(
        back, product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
    lv_obj_center(back);
    return 0;
}

void product_ui_page_action_update(product_ui_page_view_t *view,
                                    const char *text, uint8_t is_enabled,
                                    uint8_t is_read_only)
{
    lv_obj_t *action;
    const char *displayed;
    uint8_t is_visible;

    if ((view == NULL) || (view->nav_action == NULL) || (view->nav_action_label == NULL)) {
        return;
    }
    action = view->nav_action;
    is_visible = text != NULL && text[0] != '\0';
    displayed = lv_label_get_text(view->nav_action_label);
    if (is_visible == !lv_obj_has_flag(action, LV_OBJ_FLAG_HIDDEN) &&
        (!is_visible || (displayed != NULL && strcmp(displayed, text) == 0 &&
         lv_obj_has_state(action, LV_STATE_DISABLED) == (!is_enabled && !is_read_only) &&
         lv_obj_has_flag(action, LV_OBJ_FLAG_CLICKABLE) == (is_enabled && !is_read_only) &&
         view->is_nav_action_read_only == is_read_only))) {
        return;
    }
    /* A press belongs to the operation displayed when that press began. */
    view->is_nav_action_armed = 0U;
    view->is_nav_action_read_only = is_read_only;
    lv_obj_clear_state(action, LV_STATE_PRESSED);
    lv_obj_update_flag(action, LV_OBJ_FLAG_HIDDEN, !is_visible);
    if (!is_visible) {
        return;
    }
    if (strcmp(lv_label_get_text(view->nav_action_label), text) != 0) {
        lv_label_set_text(view->nav_action_label, text);
    }
    if (!is_enabled && !is_read_only) {
        lv_obj_add_state(action, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(action, LV_STATE_DISABLED);
    }
    lv_obj_update_flag(action, LV_OBJ_FLAG_CLICKABLE, is_enabled && !is_read_only);
    lv_obj_set_style_bg_opa(action, is_read_only ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(action, is_read_only ?
        product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED) : lv_color_white(), 0);
    if (is_read_only || !is_enabled) {
        lv_obj_clear_state(action, LV_STATE_PRESSED);
    }
}

static void product_ui_nav_action_event(lv_event_t *event)
{
    product_ui_page_view_t *view = lv_event_get_user_data(event);
    lv_obj_t *action = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        view->is_nav_action_armed = 1U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        view->is_nav_action_armed = 0U;
    } else if (code == LV_EVENT_CLICKED) {
        if (!view->is_nav_action_armed || lv_obj_has_flag(action, LV_OBJ_FLAG_HIDDEN) ||
            !lv_obj_has_flag(action, LV_OBJ_FLAG_CLICKABLE) ||
            lv_obj_has_state(action, LV_STATE_DISABLED)) {
            lv_event_stop_processing(event);
        }
        view->is_nav_action_armed = 0U;
    }
}

static int product_ui_nav_action_create(lv_obj_t *nav,
                                         const product_ui_page_spec_t *spec,
                                         product_ui_page_view_t *view)
{
    lv_obj_t *action;
    lv_obj_t *visual;
    uint8_t is_text = spec->nav_action_text != NULL;

    if (!is_text && (spec->nav_action_symbol == NULL) &&
        (spec->nav_action_image == NULL)) {
        return 0;
    }
    action = lv_button_create(nav);
    if (action == NULL) {
        return -1;
    }
    view->nav_action = action;
    product_ui_clean_obj(action);
    lv_obj_clear_flag(action, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(action, is_text ? 96 : PRODUCT_UI_TOUCH_MIN, PRODUCT_UI_TOUCH_MIN);
    lv_obj_align(action, LV_ALIGN_RIGHT_MID, -PRODUCT_UI_PAGE_PAD, 0);
    lv_obj_add_event_cb(action, product_ui_nav_action_event, LV_EVENT_ALL, view);
    if (spec->nav_action_cb != NULL) {
        lv_obj_add_event_cb(action, spec->nav_action_cb, LV_EVENT_CLICKED,
                            spec->nav_action_user_data);
    }
    if (is_text) {
        lv_obj_set_style_radius(action, PRODUCT_UI_RADIUS_MD, 0);
        lv_obj_set_style_bg_color(action, product_ui_color(PRODUCT_UI_COLOR_ACTION), 0);
        lv_obj_set_style_bg_color(action, product_ui_color(PRODUCT_UI_COLOR_ACTION_PRESSED),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(action, product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT),
                                  LV_STATE_DISABLED);
        lv_obj_set_style_text_color(action, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED),
                                    LV_STATE_DISABLED);
        visual = lv_label_create(action);
        if (visual != NULL) {
            lv_obj_set_style_text_font(visual, product_ui_font(PRODUCT_UI_FONT_BODY), 0);
            lv_obj_set_width(visual, 88);
            lv_obj_set_height(visual, product_ui_font(PRODUCT_UI_FONT_BODY)->line_height);
            lv_obj_set_style_text_align(visual, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(visual, LV_LABEL_LONG_DOT);
            view->nav_action_label = visual;
            product_ui_page_action_update(view, spec->nav_action_text,
                !spec->nav_action_disabled && spec->nav_action_cb != NULL,
                spec->nav_action_read_only);
        }
    } else if (spec->nav_action_image != NULL) {
        visual = lv_image_create(action);
        if (visual != NULL) {
            lv_image_set_src(visual, spec->nav_action_image);
            lv_obj_set_style_image_recolor(visual, product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
            lv_obj_set_style_image_recolor_opa(visual, LV_OPA_COVER, 0);
        }
    } else {
        visual = product_ui_make_label(action, spec->nav_action_symbol, PRODUCT_UI_STYLE_BODY);
    }
    if (visual == NULL) {
        return -1;
    }
    lv_obj_center(visual);
    if (!is_text) {
        lv_obj_set_style_bg_color(action, product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(action, LV_OPA_COVER, LV_STATE_PRESSED);
        if (spec->nav_action_cb == NULL) {
            lv_obj_add_state(action, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(action, LV_STATE_DISABLED);
        }
        lv_obj_update_flag(action, LV_OBJ_FLAG_CLICKABLE, spec->nav_action_cb != NULL);
    }
    return 0;
}

static uint32_t s_page_enter_event;

static void product_ui_title_apply(void *context, int32_t offset)
{
    product_ui_page_view_t *view = context;
    if (view->title != NULL) lv_obj_set_style_translate_x(view->title, offset, 0);
}

static void product_ui_page_enter_cb(lv_event_t *event)
{
    product_ui_page_view_t *view = lv_event_get_user_data(event);
    const uint8_t *is_back = lv_event_get_param(event);
    if (lv_event_get_target(event) != view->root || view->title == NULL) return;
    product_ui_motion_set(&view->title_motion,
        product_ui_motion_is_reduced() ? 0 : (*is_back ? -4 : 4));
    product_ui_motion_start(&view->title_motion, 0, PRODUCT_UI_MOTION_ENTER_MS);
}

void product_ui_page_enter(lv_obj_t *root, uint8_t is_back)
{
    if (root != NULL && s_page_enter_event != 0U) {
        lv_obj_send_event(root, s_page_enter_event, &is_back);
    }
}

static void product_ui_page_deleted(lv_event_t *event)
{
    product_ui_page_view_t *view = lv_event_get_user_data(event);
    if (lv_event_get_target(event) != view->root) return;
    /* Do not remove this callback during dispatch: later page cleanup must run. */
    view->root = NULL;
    product_ui_page_view_reset(view);
}

static int product_ui_nav_create(lv_obj_t *parent,
                                 const product_ui_page_spec_t *spec,
                                 product_ui_page_view_t *view)
{
    lv_obj_t *nav;
    lv_obj_t *title;

    nav = lv_obj_create(parent);
    if (nav == NULL) {
        return -1;
    }
    product_ui_clean_obj(nav);
    lv_obj_add_style(nav, product_ui_style(PRODUCT_UI_STYLE_NAV), 0);
    lv_obj_add_style(nav, &s_nav_geometry_style, 0);
    if (spec->show_back && (product_ui_nav_back_create(nav, spec) != 0)) {
        return -1;
    }
    title = product_ui_make_label(nav, spec->title, PRODUCT_UI_STYLE_TITLE);
    if (title == NULL) {
        return -1;
    }
    lv_obj_align(title, LV_ALIGN_LEFT_MID,
                 spec->show_back ? 56 : PRODUCT_UI_PAGE_PAD, 0);
    lv_obj_set_width(title, PRODUCT_UI_SCREEN_W - 4 - (spec->show_back ? 56 : PRODUCT_UI_PAGE_PAD) -
        PRODUCT_UI_PAGE_PAD - (spec->nav_action_text != NULL ? 104 :
        ((spec->nav_action_symbol != NULL || spec->nav_action_image != NULL) ? 44 : 0)));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title, product_ui_font(PRODUCT_UI_FONT_TITLE)->line_height);
    if (product_ui_nav_action_create(nav, spec, view) != 0) {
        return -1;
    }
    view->nav = nav;
    view->title = title;
    {
        const product_ui_motion_binding_t binding = {
            .owner = title, .context = view, .apply = product_ui_title_apply,
        };
        product_ui_motion_bind(&view->title_motion, &binding);
    }
    return 0;
}

void product_ui_page_view_reset(product_ui_page_view_t *view)
{
    if (view != NULL) {
        product_ui_motion_dispose(&view->title_motion);
        if (view->root != NULL) {
            lv_obj_remove_event_cb(view->root, product_ui_page_enter_cb);
            lv_obj_remove_event_cb(view->root, product_ui_page_deleted);
        }
        product_ui_status_unregister(&view->status_view);
        memset(view, 0, sizeof(*view));
    }
}

static lv_obj_t *product_ui_page_content_create(
    lv_obj_t *parent, const product_ui_page_spec_t *spec,
    product_ui_page_role_e role)
{
    lv_obj_t *content;

    content = lv_obj_create(parent);
    if (content == NULL) {
        return NULL;
    }
    product_ui_clean_obj(content);
    lv_obj_add_style(content, product_ui_style(PRODUCT_UI_STYLE_PAGE), 0);
    lv_obj_add_style(content, spec->show_back ? &s_page_content_style : &s_home_content_style, 0);
    if ((role == PRODUCT_UI_PAGE_ROLE_HOME) ||
        (spec->theme == PRODUCT_UI_PAGE_THEME_TRANSPARENT)) {
        lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    }
    return content;
}

int product_ui_page_frame_create_ex(lv_obj_t *parent,
                                    const product_ui_page_spec_t *spec,
                                    product_ui_page_view_t *view)
{
    product_ui_status_create_params_t status_params;
    product_ui_status_t status;
    product_ui_page_role_e role;

    if ((parent == NULL) || (spec == NULL) || (spec->title == NULL) ||
        (view == NULL)) {
        return -1;
    }
    product_ui_page_view_reset(view);
    if ((product_ui_theme_init() != 0) ||
        (product_ui_control_center_init() != 0)) {
        return -1;
    }
    role = spec->role == PRODUCT_UI_PAGE_ROLE_AUTO ?
           (spec->show_back ? PRODUCT_UI_PAGE_ROLE_CONTENT :
                              PRODUCT_UI_PAGE_ROLE_HOME) : spec->role;
    product_ui_clean_obj(parent);
    lv_obj_add_style(parent, product_ui_style(PRODUCT_UI_STYLE_PAGE), 0);
    lv_obj_set_size(parent, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    view->root = parent;
    if (s_page_enter_event == 0U) s_page_enter_event = lv_event_register_id();
    lv_obj_add_event_cb(parent, product_ui_page_enter_cb, s_page_enter_event, view);
    lv_obj_add_event_cb(parent, product_ui_page_deleted, LV_EVENT_DELETE, view);

    status_params = (product_ui_status_create_params_t) {
        .view = &view->status_view,
        .root_out = &view->status,
        .allow_pull = 1U,
        .is_light = spec->show_back ? 0U : 1U,
        .hide_connectivity = 0U,
    };
    if (product_ui_status_create(parent, &status_params) != 0) {
        return -1;
    }
    if (spec->show_back &&
        (product_ui_nav_create(parent, spec, view) != 0)) {
        return -1;
    }
    view->content = product_ui_page_content_create(parent, spec, role);
    if (view->content == NULL) {
        return -1;
    }

    product_ui_status_snapshot(&status);
    product_ui_status_bar_refresh(&status);
    return 0;
}

lv_obj_t *product_ui_control_status_create(lv_obj_t *parent,
                                           product_ui_status_view_t *view)
{
    product_ui_status_create_params_t params;
    lv_obj_t *root = NULL;

    params = (product_ui_status_create_params_t) {
        .view = view,
        .root_out = &root,
        .allow_pull = 0U,
        .is_light = 1U,
        .hide_connectivity = 1U,
    };
    if ((parent == NULL) || (view == NULL) ||
        (product_ui_status_create(parent, &params) != 0)) {
        return NULL;
    }
    /* The dark control-center bar uses light foregrounds but still exposes
     * connectivity icons when a real capability publishes them. */
    view->hide_connectivity = 0U;
    lv_obj_set_style_border_color(
        root, product_ui_color(PRODUCT_UI_COLOR_CONTROL_BORDER), 0);
    lv_obj_set_style_border_opa(root, LV_OPA_30, 0);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_set_style_border_side(root, LV_BORDER_SIDE_BOTTOM, 0);
    return root;
}

static void product_ui_status_visibility_update(
    product_ui_status_view_t *view, const product_ui_status_t *status)
{
    /* Transparent objects keep their layout slots when a capability disappears. */
    lv_obj_clear_flag(view->time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view->wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view->bluetooth, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view->battery_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(view->time, status->valid_mask & PRODUCT_UI_STATUS_VALID_TIME ?
                          LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (view->hide_connectivity || !(status->valid_mask & PRODUCT_UI_STATUS_VALID_WIFI)) {
        lv_obj_set_style_opa(view->wifi, LV_OPA_TRANSP, 0);
    }
    if (view->hide_connectivity || !(status->valid_mask & PRODUCT_UI_STATUS_VALID_BLUETOOTH)) {
        lv_obj_set_style_opa(view->bluetooth, LV_OPA_TRANSP, 0);
    }
    if (!(status->valid_mask & PRODUCT_UI_STATUS_VALID_BATTERY)) {
        lv_obj_set_style_opa(view->battery_host, LV_OPA_TRANSP, 0);
    }
    lv_obj_add_flag(view->battery_text, LV_OBJ_FLAG_HIDDEN);
}

static uint8_t product_ui_status_view_ready(
    const product_ui_status_view_t *view)
{
    uint8_t i;

    if (view == NULL) {
        return 0U;
    }
    if ((view->root == NULL) || (view->time == NULL) ||
        (view->wifi == NULL) || (view->bluetooth == NULL) ||
        (view->battery_host == NULL) || (view->battery == NULL) ||
        (view->battery_fill == NULL) || (view->battery_tip == NULL) ||
        (view->battery_charge == NULL) || (view->battery_text == NULL)) {
        return 0U;
    }
    for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++) {
        if (view->wifi_bar[i] == NULL) {
            return 0U;
        }
    }
    return 1U;
}

static void product_ui_status_connectivity_update(
    product_ui_status_view_t *view, const product_ui_status_t *status,
    lv_color_t color)
{
    lv_opa_t bt_opa = LV_OPA_30;
    lv_opa_t lit_opa = LV_OPA_COVER;
    lv_opa_t dim_opa = LV_OPA_30;
    lv_opa_t wifi_root_opa = LV_OPA_COVER;
    uint8_t level = 0U;
    uint8_t i;

    lv_obj_set_style_image_recolor(view->bluetooth, color, 0);

    /*
     * WiFi signal bars (wifi_level 0..4):
     * - CONNECTED: light first N bars from status->wifi_level
     * - ON (associating): all bars dim, root mid-opa
     * - OFF/UNKNOWN: all bars dim
     */
    if (status->valid_mask & PRODUCT_UI_STATUS_VALID_WIFI) {
        if (status->wifi_state == PRODUCT_UI_LINK_CONNECTED) {
            level = status->wifi_level;
            if (level > PRODUCT_UI_WIFI_BAR_COUNT) {
                level = PRODUCT_UI_WIFI_BAR_COUNT;
            }
            lit_opa = LV_OPA_COVER;
            dim_opa = LV_OPA_30;
            wifi_root_opa = LV_OPA_COVER;
        } else if (status->wifi_state == PRODUCT_UI_LINK_ON) {
            level = 0U;
            dim_opa = LV_OPA_40;
            wifi_root_opa = LV_OPA_70;
        } else {
            level = 0U;
            dim_opa = LV_OPA_30;
            wifi_root_opa = LV_OPA_COVER;
        }
    } else {
        level = 0U;
        dim_opa = LV_OPA_30;
        wifi_root_opa = LV_OPA_COVER;
    }
    for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++) {
        lv_obj_set_style_bg_color(view->wifi_bar[i], color, 0);
        if (i < level) {
            lv_obj_set_style_bg_opa(view->wifi_bar[i], lit_opa, 0);
        } else {
            lv_obj_set_style_bg_opa(view->wifi_bar[i], dim_opa, 0);
        }
    }
    lv_obj_set_style_opa(view->wifi, wifi_root_opa, 0);

    if ((status->valid_mask & PRODUCT_UI_STATUS_VALID_BLUETOOTH) &&
        (status->bluetooth_state >= PRODUCT_UI_LINK_ON)) {
        bt_opa = LV_OPA_COVER;
    }
    lv_obj_set_style_opa(view->bluetooth, bt_opa, 0);
}

static void product_ui_status_battery_update(
    product_ui_status_view_t *view, const product_ui_status_t *status,
    lv_color_t color)
{
    uint8_t is_valid = (uint8_t)((status->valid_mask &
        PRODUCT_UI_STATUS_VALID_BATTERY) != 0U);
    /* Cipher: low battery uses warn/danger fill; shell/tip follow accent color. */
    lv_color_t fill_color = (is_valid && (status->battery_percent <= 20U)) ?
                            product_ui_color(PRODUCT_UI_COLOR_DANGER) : color;
    uint8_t percent = 0U;
    uint8_t fill_width;

    lv_obj_set_style_border_color(view->battery, color, 0);
    lv_obj_set_style_bg_opa(view->battery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(view->battery_fill, fill_color, 0);
    lv_obj_set_style_bg_color(view->battery_tip, fill_color, 0);

    if (is_valid) {
        percent = status->battery_percent;
        if (percent > 100U) {
            percent = 100U;
        }
    }
    /* Cipher: fill_w = (22 * pct) / 100; only draw when fill_w > 0. */
    fill_width = (uint8_t)(((uint32_t)percent *
                            (uint32_t)PRODUCT_UI_STATUS_BAT_FILL_MAX) / 100U);
    lv_obj_set_pos(view->battery_fill, PRODUCT_UI_STATUS_BAT_INSET,
                   PRODUCT_UI_STATUS_BAT_INSET);
    if (fill_width > 0U) {
        lv_obj_set_size(view->battery_fill, fill_width,
                        PRODUCT_UI_STATUS_BAT_FILL_H);
        lv_obj_clear_flag(view->battery_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_size(view->battery_fill, 0, PRODUCT_UI_STATUS_BAT_FILL_H);
        lv_obj_add_flag(view->battery_fill, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_opa(view->battery_host,
                         is_valid ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_text_color(view->battery_charge, color, 0);
    lv_obj_set_style_text_color(view->battery_text, color, 0);
    lv_label_set_text(view->battery_text, "");
    if (is_valid && status->is_charging) {
        lv_obj_clear_flag(view->battery_charge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(view->battery_charge);
    } else {
        lv_obj_add_flag(view->battery_charge, LV_OBJ_FLAG_HIDDEN);
    }
}

static void product_ui_status_view_apply(
    product_ui_status_view_t *view, const product_ui_status_t *status)
{
    char time_text[8];
    lv_color_t color;

    if (!product_ui_status_view_ready(view) || (status == NULL)) {
        return;
    }
    color = view->is_light ? lv_color_white() :
            product_ui_color(PRODUCT_UI_COLOR_TEXT);
    lv_snprintf(time_text, sizeof(time_text), "%02u:%02u",
                status->hour, status->minute);
    lv_label_set_text(view->time, time_text);
    lv_obj_set_style_text_font(view->time,
                               product_ui_font(PRODUCT_UI_FONT_STATUS), 0);
    lv_obj_set_style_text_color(view->time, color, 0);
    product_ui_status_connectivity_update(view, status, color);
    product_ui_status_battery_update(view, status, color);
    product_ui_status_visibility_update(view, status);
}

void product_ui_status_bar_refresh(const product_ui_status_t *status)
{
    product_ui_status_view_t *view;

    if (status == NULL) {
        return;
    }
    for (view = s_status_views; view != NULL; view = view->next) {
        if (!product_ui_status_view_ready(view)) {
            continue;
        }
        product_ui_status_view_apply(view, status);
    }
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
