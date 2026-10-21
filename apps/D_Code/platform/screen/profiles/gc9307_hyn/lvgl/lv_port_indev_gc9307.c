/**
 * @file lv_port_indev_gc9307.c
 * @brief LVGL input port for the GC9307 + HYN/CST8xxT screen profile.
 * D++: Owned by the D_Code GC9307/HYN screen profile.
 */

#include "framework/d_lvgl_port.h"
#include "lvgl.h"
#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#include "platform/screen/profiles/gc9307_hyn/d_touch_hyn.h"
#include "platform/screen/profiles/gc9307_hyn/d_screen_gc9307_hyn.h"
#include "platform/screen/power/d_screen_power.h"
#include "system/sys_time.h"

#ifdef D_LVGL_ENABLE
#include "framework/d_page_manager.h"
#endif

#define GESTURE_THRESHOLD   80
#define GESTURE_TIME_MAX    500

static void touchpad_init(void);
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);
static void touchpad_dispatch_page_gesture(int gesture_type);

static struct {
    int is_tracking;
    int32_t start_x;
    int32_t start_y;
    uint32_t start_time;
} s_gesture = {0};

void lv_port_indev_init(void)
{
    lv_indev_t *indev_touchpad;

    /* D++: A pointer indev without a display is later dereferenced by LVGL's pointer processor. */
    if (lv_display_get_default() == NULL) {
        printf("[GC9307-INDEV] display unavailable, input disabled\n");
        return;
    }

    touchpad_init();
    indev_touchpad = lv_indev_create();
    if (indev_touchpad == NULL) {
        printf("[GC9307-INDEV] input create failed\n");
        return;
    }
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}

static void touchpad_init(void)
{
}

static void touchpad_dispatch_page_gesture(int gesture_type)
{
#ifdef D_LVGL_ENABLE
    if (page_manager_get_current() == PAGE_TYPE_MAIN) {
        return;
    }
    page_manager_handle_gesture(gesture_type);
#else
    (void)gesture_type;
#endif
}

static void touchpad_report_release(int32_t logic_x, int32_t logic_y)
{
    int32_t dx;
    int32_t dy;
    int32_t abs_dx;
    int32_t abs_dy;
    uint32_t elapsed;

    if (!s_gesture.is_tracking) {
        return;
    }

    dx = logic_x - s_gesture.start_x;
    dy = logic_y - s_gesture.start_y;
    abs_dx = (dx >= 0) ? dx : -dx;
    abs_dy = (dy >= 0) ? dy : -dy;
    elapsed = timer_get_ms() - s_gesture.start_time;

    if (elapsed < GESTURE_TIME_MAX) {
        if ((abs_dy > abs_dx) && (abs_dy > GESTURE_THRESHOLD)) {
            touchpad_dispatch_page_gesture((dy < 0) ? 1 : 2);
        } else if ((abs_dx > abs_dy) && (abs_dx > GESTURE_THRESHOLD)) {
            touchpad_dispatch_page_gesture((dx < 0) ? 3 : 4);
        }
    }

    s_gesture.is_tracking = 0;
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    short raw_x;
    short raw_y;
    int32_t last_x;
    int32_t last_y;
    s32 logic_x;
    s32 logic_y;
    unsigned char status;
    uint8_t raw_pressed;

    (void)indev;
    d_touch_hyn_get_state(&raw_x, &raw_y, &status);
    raw_pressed = status ? 1U : 0U;
    status = d_screen_power_filter_touch(raw_pressed);
    last_x = raw_x;
    last_y = raw_y;
    d_screen_gc9307_hyn_native_to_logical(last_x, last_y, &logic_x, &logic_y);

    if (status) {
        data->state = LV_INDEV_STATE_PR;
        if (!s_gesture.is_tracking) {
            s_gesture.is_tracking = 1;
            s_gesture.start_x = logic_x;
            s_gesture.start_y = logic_y;
            s_gesture.start_time = timer_get_ms();
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
        if (raw_pressed != 0U) {
            /* Wake/guard presses must not complete a stale page gesture. */
            s_gesture.is_tracking = 0;
        } else {
            touchpad_report_release(logic_x, logic_y);
        }
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
