/**
 * @file screen_test_page.c
 * @brief Minimal LVGL validation page for the GC9307/HYN screen profile.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_SCREEN_TEST)

#include "modules/diagnostics/display/screen_test/screen_test_page.h"
#include "framework/d_page_manager.h"
#include "lvgl.h"
#include <stdint.h>

#define SCREEN_TEST_LOG_TAG          "[SCREEN_TEST] "
#define SCREEN_TEST_LINE_WIDTH       4
#define SCREEN_TEST_LINE_HEIGHT      148
#define SCREEN_TEST_UPDATE_MS        50
#define SCREEN_TEST_BAR_COUNT        4
#define SCREEN_TEST_TOUCH_DOT_SIZE   12

static lv_obj_t *s_scan_line = NULL;
static lv_obj_t *s_touch_dot = NULL;
static uint32_t s_update_accum_ms = 0;
static int16_t s_scan_x = 0;
static int16_t s_touch_x = 0;
static int16_t s_touch_y = 0;
static uint8_t s_touch_pressed = 0;

static void screen_test_reset_runtime_state(void)
{
    s_scan_line = NULL;
    s_touch_dot = NULL;
    s_update_accum_ms = 0;
    s_scan_x = 0;
    s_touch_x = 0;
    s_touch_y = 0;
    s_touch_pressed = 0;
}

static void screen_test_clean_obj(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void screen_test_label(lv_obj_t *parent, lv_obj_t **label, int y, const char *text)
{
    *label = lv_label_create(parent);
    if (*label == NULL) {
        return;
    }

    lv_label_set_text(*label, text);
    lv_obj_set_width(*label, MY_DISP_HOR_RES - 16);
    lv_obj_set_pos(*label, 8, y);
    lv_obj_set_style_text_color(*label, lv_color_white(), 0);
}

static void screen_test_update_touch_dot(void)
{
    int16_t x;
    int16_t y;
    int16_t max_x;
    int16_t max_y;

    if (s_touch_dot == NULL) {
        return;
    }

    max_x = MY_DISP_HOR_RES - SCREEN_TEST_TOUCH_DOT_SIZE;
    max_y = MY_DISP_VER_RES - SCREEN_TEST_TOUCH_DOT_SIZE;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    x = s_touch_x - (SCREEN_TEST_TOUCH_DOT_SIZE / 2);
    y = s_touch_y - (SCREEN_TEST_TOUCH_DOT_SIZE / 2);
    if (x < 0) {
        x = 0;
    } else if (x > max_x) {
        x = max_x;
    }
    if (y < 0) {
        y = 0;
    } else if (y > max_y) {
        y = max_y;
    }

    lv_obj_set_pos(s_touch_dot, x, y);
    lv_obj_set_style_bg_color(s_touch_dot,
                              s_touch_pressed ? lv_color_hex(0x22C55E) : lv_color_hex(0x64748B),
                              0);
}

static void screen_test_touch_event_cb(lv_event_t *event)
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
        s_touch_x = (int16_t)point.x;
        s_touch_y = (int16_t)point.y;
    }

    s_touch_pressed = (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) ? 1 : 0;
    screen_test_update_touch_dot();
}

static int screen_test_create_bars(lv_obj_t *parent)
{
    static const uint32_t bar_colors[SCREEN_TEST_BAR_COUNT] = {
        0xF43F5E,
        0x22C55E,
        0x38BDF8,
        0xFACC15,
    };
    lv_obj_t *bar;
    int bar_width;
    int i;

    bar_width = MY_DISP_HOR_RES / SCREEN_TEST_BAR_COUNT;
    for (i = 0; i < SCREEN_TEST_BAR_COUNT; i++) {
        bar = lv_obj_create(parent);
        if (bar == NULL) {
            return -1;
        }
        screen_test_clean_obj(bar);
        lv_obj_set_size(bar, bar_width, 40);
        lv_obj_set_pos(bar, i * bar_width, MY_DISP_VER_RES - 56);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(bar_colors[i]), 0);
    }

    return 0;
}

static int screen_test_create_touch_dot(lv_obj_t *parent)
{
    s_touch_dot = lv_obj_create(parent);
    if (s_touch_dot == NULL) {
        return -1;
    }

    screen_test_clean_obj(s_touch_dot);
    lv_obj_set_size(s_touch_dot, SCREEN_TEST_TOUCH_DOT_SIZE, SCREEN_TEST_TOUCH_DOT_SIZE);
    lv_obj_set_pos(s_touch_dot, 8, 96);
    lv_obj_set_style_bg_opa(s_touch_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(0x64748B), 0);
    return 0;
}

static int screen_test_page_create(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *hint;

    screen_test_reset_runtime_state();
    if (parent == NULL) {
        return -1;
    }

    screen_test_clean_obj(parent);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x050A0F), 0);
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, screen_test_touch_event_cb, LV_EVENT_ALL, NULL);

    screen_test_label(parent, &title, 12, "GC9307 240x320 SPI1 80MHz");
    if (title != NULL) {
        lv_obj_set_style_text_color(title, lv_color_hex(0xE0F2FE), 0);
    }
    screen_test_label(parent, &hint, 44, "refresh scan");
    screen_test_label(parent, &hint, 76, "touch dot follows pointer");

    if (screen_test_create_bars(parent) != 0) {
        screen_test_reset_runtime_state();
        return -1;
    }

    s_scan_line = lv_obj_create(parent);
    if (s_scan_line == NULL) {
        screen_test_reset_runtime_state();
        return -1;
    }
    screen_test_clean_obj(s_scan_line);
    lv_obj_set_size(s_scan_line, SCREEN_TEST_LINE_WIDTH, SCREEN_TEST_LINE_HEIGHT);
    lv_obj_set_pos(s_scan_line, 0, 112);
    lv_obj_set_style_bg_opa(s_scan_line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scan_line, lv_color_hex(0xFFFFFF), 0);

    if (screen_test_create_touch_dot(parent) != 0) {
        screen_test_reset_runtime_state();
        return -1;
    }

    printf(SCREEN_TEST_LOG_TAG "created\n");
    return 0;
}

static void screen_test_page_update(uint32_t elapsed_ms)
{
    int16_t max_x;

    if (s_scan_line == NULL) {
        return;
    }

    s_update_accum_ms += elapsed_ms;
    if (s_update_accum_ms < SCREEN_TEST_UPDATE_MS) {
        return;
    }
    s_update_accum_ms = 0;

    max_x = MY_DISP_HOR_RES - SCREEN_TEST_LINE_WIDTH;
    if (max_x <= 0) {
        max_x = 1;
    }

    s_scan_x = (int16_t)((s_scan_x + 3) % max_x);
    lv_obj_set_x(s_scan_line, s_scan_x);
}

static void screen_test_page_enter(void)
{
    printf(SCREEN_TEST_LOG_TAG "enter\n");
}

static void screen_test_page_exit(void)
{
    printf(SCREEN_TEST_LOG_TAG "exit\n");
}

static const page_ops_t s_screen_test_page_ops = {
    .create  = screen_test_page_create,
    .enter   = screen_test_page_enter,
    .exit    = screen_test_page_exit,
    .destroy = NULL,
    .update  = screen_test_page_update,
    .gesture = NULL,
    .flags   = 0,
};

static const page_desc_t s_screen_test_page_desc = {
    .page_type  = PAGE_TYPE_MAIN,
    .name       = "ScreenTest",
    .ops        = &s_screen_test_page_ops,
    .container  = NULL,
    .is_created = 0,
    .is_active  = 0,
};

int screen_test_page_register(void)
{
    return page_manager_register(&s_screen_test_page_desc);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_SCREEN_TEST) */
