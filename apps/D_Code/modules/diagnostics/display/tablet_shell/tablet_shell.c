/**
 * @file tablet_shell.c
 * @brief GC9307 landscape tablet-style home shell.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_TABLET_SHELL)

#include "modules/diagnostics/display/tablet_shell/tablet_shell.h"
#include "modules/diagnostics/display/tablet_shell/tablet_shell_panel.h"
#include "framework/d_page_manager.h"
#include "lvgl.h"

#define TABLET_LOG_TAG              "[TabletShell] "
#define TABLET_STATUS_H             28
#define TABLET_PULL_ZONE_H          38
#define TABLET_APP_COUNT            8
#define TABLET_SCAN_UPDATE_MS       50
#define TABLET_PANEL_GESTURE_MIN    18

typedef struct {
    const char *icon;
    const char *label;
} tablet_app_t;

static const tablet_app_t s_apps[TABLET_APP_COUNT] = {
    {LV_SYMBOL_SETTINGS, "Settings"},
    {LV_SYMBOL_WIFI, "Network"},
    {LV_SYMBOL_IMAGE, "Gallery"},
    {LV_SYMBOL_VOLUME_MAX, "Audio"},
    {LV_SYMBOL_LIST, "Files"},
    {LV_SYMBOL_REFRESH, "Update"},
    {LV_SYMBOL_BATTERY_FULL, "Power"},
    {LV_SYMBOL_HOME, "Home"},
};

static lv_obj_t *s_scan_line;
static uint32_t s_scan_accum_ms;
static int16_t s_scan_x;
static int16_t s_panel_gesture_start_x;
static int16_t s_panel_gesture_start_y;
static uint8_t s_panel_gesture_active;
static uint8_t s_panel_gesture_consumed;
static uint8_t s_panel_click_blocked;

static void tablet_shell_reset_runtime_state(void)
{
    s_scan_line = NULL;
    s_scan_accum_ms = 0;
    s_scan_x = 0;
    s_panel_gesture_start_x = 0;
    s_panel_gesture_start_y = 0;
    s_panel_gesture_active = 0;
    s_panel_gesture_consumed = 0;
    s_panel_click_blocked = 0;
}

static void tablet_clean_obj(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void tablet_accept_gesture(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void tablet_panel_track_touch(lv_event_code_t code, const lv_point_t *point)
{
    if (point == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        s_panel_gesture_start_x = (int16_t)point->x;
        s_panel_gesture_start_y = (int16_t)point->y;
        s_panel_gesture_active = 1;
        s_panel_gesture_consumed = 0;
        s_panel_click_blocked = 0;
    } else if ((code == LV_EVENT_RELEASED) || (code == LV_EVENT_PRESS_LOST)) {
        s_panel_gesture_active = 0;
        if (code == LV_EVENT_PRESS_LOST) {
            s_panel_click_blocked = 1;
        }
    }
}

static int tablet_panel_get_drag(int16_t *drag_x, int16_t *drag_y)
{
    lv_indev_t *indev;
    lv_point_t point;
    int16_t dx;
    int16_t dy;

    if ((drag_x == NULL) || (drag_y == NULL) ||
        !s_panel_gesture_active || s_panel_gesture_consumed) {
        return 0;
    }

    indev = lv_indev_active();
    if (indev == NULL) {
        return 0;
    }

    lv_indev_get_point(indev, &point);
    dx = (int16_t)(point.x - s_panel_gesture_start_x);
    dy = (int16_t)(point.y - s_panel_gesture_start_y);
    if ((LV_ABS(dx) < TABLET_PANEL_GESTURE_MIN) &&
        (LV_ABS(dy) < TABLET_PANEL_GESTURE_MIN)) {
        return 0;
    }

    *drag_x = dx;
    *drag_y = dy;
    return 1;
}

static void tablet_handle_panel_gesture(lv_dir_t dir)
{
    lv_indev_t *indev;
    int16_t drag_x = 0;
    int16_t drag_y = 0;

    (void)dir;

    if (!tablet_panel_get_drag(&drag_x, &drag_y)) {
        return;
    }

    s_panel_click_blocked = 1;
    if ((LV_ABS(drag_y) < TABLET_PANEL_GESTURE_MIN) ||
        (LV_ABS(drag_y) <= LV_ABS(drag_x))) {
        return;
    }

    s_panel_gesture_consumed = 1;
    if (drag_y < 0) {
        tablet_shell_panel_show_control(0);
    } else {
        tablet_shell_panel_show_control(1);
    }

    indev = lv_indev_active();
    if (indev != NULL) {
        lv_indev_wait_release(indev);
    }
}

static void tablet_panel_track_event(lv_event_t *event)
{
    lv_event_code_t code;
    lv_indev_t *indev;
    lv_point_t point;

    if (event == NULL) {
        return;
    }

    code = lv_event_get_code(event);
    if ((code != LV_EVENT_PRESSED) &&
        (code != LV_EVENT_RELEASED) &&
        (code != LV_EVENT_PRESS_LOST)) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }

    lv_indev_get_point(indev, &point);
    tablet_panel_track_touch(code, &point);
}

static void tablet_panel_handle_click(lv_event_t *event)
{
    lv_obj_t *target;

    if (event == NULL) {
        return;
    }

    target = (lv_obj_t *)lv_event_get_current_target(event);
    if (tablet_shell_panel_is_scrim(target) && !s_panel_click_blocked) {
        tablet_shell_panel_show_control(0);
    }

    s_panel_gesture_active = 0;
    s_panel_gesture_consumed = 0;
    s_panel_click_blocked = 0;
}

static void tablet_panel_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_CLICKED) {
        tablet_panel_handle_click(event);
    } else if ((code == LV_EVENT_PRESSED) ||
               (code == LV_EVENT_RELEASED) ||
               (code == LV_EVENT_PRESS_LOST)) {
        tablet_panel_track_event(event);
    } else if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        tablet_handle_panel_gesture(dir);
    }
}

static void tablet_app_event_cb(lv_event_t *event)
{
    const tablet_app_t *app;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    app = (const tablet_app_t *)lv_event_get_user_data(event);
    if (app == &s_apps[0]) {
        lv_event_stop_bubbling(event);
        tablet_shell_panel_toggle_settings();
    }
}

static void tablet_touch_event_cb(lv_event_t *event)
{
    lv_event_code_t code;
    lv_indev_t *indev;
    lv_point_t point;

    if (event == NULL) {
        return;
    }

    code = lv_event_get_code(event);
    if ((code != LV_EVENT_PRESSED) &&
        (code != LV_EVENT_PRESSING) &&
        (code != LV_EVENT_RELEASED) &&
        (code != LV_EVENT_PRESS_LOST)) {
        return;
    }

    indev = lv_event_get_indev(event);
    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
        tablet_panel_track_touch(code, &point);
    }
}

static void tablet_root_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
        code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        tablet_touch_event_cb(event);
    } else if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        tablet_handle_panel_gesture(dir);
    }
}

static lv_obj_t *tablet_make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL) {
        return NULL;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

static lv_obj_t *tablet_make_button(lv_obj_t *parent, int32_t w, int32_t h, lv_color_t bg, lv_color_t fg)
{
    lv_obj_t *button = lv_button_create(parent);

    if (button == NULL) {
        return NULL;
    }

    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, bg, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x263247), 0);
    lv_obj_set_style_text_color(button, fg, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return button;
}

static int tablet_create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar;
    lv_obj_t *spacer;
    lv_obj_t *label;

    bar = lv_obj_create(parent);
    if (bar == NULL) {
        return -1;
    }
    tablet_clean_obj(bar);
    lv_obj_set_size(bar, MY_DISP_HOR_RES, TABLET_STATUS_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x101826), 0);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 6, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    tablet_accept_gesture(bar);
    lv_obj_add_event_cb(bar, tablet_panel_event_cb, LV_EVENT_ALL, NULL);

    spacer = lv_obj_create(bar);
    if (spacer == NULL) {
        return -1;
    }
    tablet_clean_obj(spacer);
    lv_obj_set_size(spacer, 1, TABLET_STATUS_H);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);

    label = tablet_make_label(bar, LV_SYMBOL_WIFI "  " LV_SYMBOL_BATTERY_3, &lv_font_montserrat_16,
                              lv_color_hex(0xB7E4F8));
    if (label == NULL) {
        return -1;
    }
    lv_obj_set_width(label, 52);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    return 0;
}

static int tablet_create_pull_zone(lv_obj_t *parent)
{
    lv_obj_t *zone;

    zone = lv_obj_create(parent);
    if (zone == NULL) {
        return -1;
    }

    tablet_clean_obj(zone);
    lv_obj_set_size(zone, MY_DISP_HOR_RES, TABLET_PULL_ZONE_H);
    lv_obj_set_pos(zone, 0, 0);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    tablet_accept_gesture(zone);
    lv_obj_add_event_cb(zone, tablet_panel_event_cb, LV_EVENT_ALL, NULL);
    return 0;
}

static int tablet_create_apps(lv_obj_t *parent)
{
    lv_obj_t *grid;
    uint8_t i;

    grid = lv_obj_create(parent);
    if (grid == NULL) {
        return -1;
    }
    tablet_clean_obj(grid);
    lv_obj_set_size(grid, MY_DISP_HOR_RES - 18, 132);
    lv_obj_set_pos(grid, 9, TABLET_STATUS_H + 16);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < TABLET_APP_COUNT; i++) {
        lv_obj_t *app = tablet_make_button(grid, 66, 58, lv_color_hex(0x142033), lv_color_hex(0xE5F3FF));
        lv_obj_t *icon;
        lv_obj_t *label;

        if (app == NULL) {
            return -1;
        }
        lv_obj_set_flex_flow(app, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(app, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(app, tablet_app_event_cb, LV_EVENT_CLICKED, (void *)&s_apps[i]);

        icon = tablet_make_label(app, s_apps[i].icon, &lv_font_montserrat_22, lv_color_hex(0x7DD3FC));
        label = tablet_make_label(app, s_apps[i].label, &lv_font_montserrat_12, lv_color_hex(0xD9EAF7));
        if ((icon == NULL) || (label == NULL)) {
            return -1;
        }
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    return 0;
}

static int tablet_create_footer(lv_obj_t *parent)
{
    lv_obj_t *footer;

    footer = lv_obj_create(parent);
    if (footer == NULL) {
        return -1;
    }
    tablet_clean_obj(footer);
    lv_obj_set_size(footer, MY_DISP_HOR_RES - 18, 36);
    lv_obj_set_pos(footer, 9, MY_DISP_VER_RES - 44);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_radius(footer, 8, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(0x233047), 0);
    lv_obj_set_style_pad_left(footer, 10, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_scan_line = lv_obj_create(footer);
    if (s_scan_line == NULL) {
        return -1;
    }
    tablet_clean_obj(s_scan_line);
    lv_obj_set_size(s_scan_line, 48, 3);
    lv_obj_set_style_bg_opa(s_scan_line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scan_line, lv_color_hex(0x22D3EE), 0);
    lv_obj_set_style_radius(s_scan_line, 2, 0);
    lv_obj_set_style_translate_x(s_scan_line, 0, 0);
    return 0;
}

static int tablet_shell_create(lv_obj_t *parent)
{
    tablet_shell_reset_runtime_state();
    if (parent == NULL) {
        return -1;
    }

    tablet_clean_obj(parent);
    lv_obj_set_size(parent, MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x07111F), 0);
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    tablet_accept_gesture(parent);
    lv_obj_add_event_cb(parent, tablet_root_event_cb, LV_EVENT_ALL, NULL);
    if (tablet_create_status_bar(parent) != 0 ||
        tablet_create_apps(parent) != 0 ||
        tablet_create_footer(parent) != 0 ||
        tablet_create_pull_zone(parent) != 0 ||
        tablet_shell_panel_create(parent, tablet_panel_event_cb) != 0) {
        tablet_shell_reset_runtime_state();
        return -1;
    }

    printf(TABLET_LOG_TAG "created logical=%dx%d\n", MY_DISP_HOR_RES, MY_DISP_VER_RES);
    return 0;
}

static void tablet_shell_update(uint32_t elapsed_ms)
{
    if (s_scan_line != NULL) {
        lv_obj_t *parent;
        uint32_t offset;
        uint32_t steps;
        int32_t travel;

        s_scan_accum_ms += elapsed_ms;
        if (s_scan_accum_ms >= TABLET_SCAN_UPDATE_MS) {
            steps = s_scan_accum_ms / TABLET_SCAN_UPDATE_MS;
            s_scan_accum_ms %= TABLET_SCAN_UPDATE_MS;
            parent = lv_obj_get_parent(s_scan_line);
            if (parent == NULL) {
                return;
            }
            travel = lv_obj_get_content_width(parent) - lv_obj_get_width(s_scan_line) + 1;
            if (travel < 1) {
                travel = 1;
            }
            offset = (steps * 4U) % (uint32_t)travel;
            s_scan_x = (int16_t)(((uint32_t)s_scan_x + offset) % (uint32_t)travel);
            lv_obj_set_style_translate_x(s_scan_line, s_scan_x, 0);
        }
    }
}

static void tablet_shell_enter(void)
{
    printf(TABLET_LOG_TAG "enter\n");
}

static void tablet_shell_exit(void)
{
    tablet_shell_panel_hide_all();
}

static const page_ops_t s_tablet_shell_ops = {
    .create  = tablet_shell_create,
    .enter   = tablet_shell_enter,
    .exit    = tablet_shell_exit,
    .destroy = NULL,
    .update  = tablet_shell_update,
    .gesture = NULL,
    .flags   = 0,
};

static const page_desc_t s_tablet_shell_desc = {
    .page_type  = PAGE_TYPE_MAIN,
    .name       = "TabletShell",
    .ops        = &s_tablet_shell_ops,
    .container  = NULL,
    .is_created = 0,
    .is_active  = 0,
};

int tablet_shell_register(void)
{
    return page_manager_register(&s_tablet_shell_desc);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_TABLET_SHELL) */
