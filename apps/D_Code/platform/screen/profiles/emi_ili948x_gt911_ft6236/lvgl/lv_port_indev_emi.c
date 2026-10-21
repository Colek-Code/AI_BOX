/**
 * @file lv_port_indev_emi.c
 * @brief LVGL input port for the EMI/ILI948x/GT911/FT6236 screen profile.
 * D++: Owned by the D_Code EMI/ILI948x/GT911/FT6236 screen profile.
 */

#include "framework/d_lvgl_port.h"
#include "lvgl.h"
#include "config/app_config.h"
#include "platform/screen/profiles/emi_ili948x_gt911_ft6236/d_touch.h"

#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)

#ifdef D_LVGL_ENABLE
#include "framework/d_page_manager.h"
#endif

#define GESTURE_THRESHOLD 80
#define GESTURE_TIME_MAX  500

typedef struct {
    int is_tracking;
    int32_t start_x;
    int32_t start_y;
    uint32_t start_time;
} emi_touch_gesture_t;

static emi_touch_gesture_t s_gesture;

static void touchpad_map_to_display(int32_t *x, int32_t *y)
{
#if D_LVGL_LOGICAL_LANDSCAPE
    int32_t raw_x;
    int32_t raw_y;
    int32_t mapped_x;
    int32_t mapped_y;

    if (x == NULL || y == NULL) {
        return;
    }

    raw_x = *x;
    raw_y = *y;
    mapped_x = raw_y;
    mapped_y = MY_DISP_VER_RES - 1 - raw_x;

    if (mapped_x < 0) {
        mapped_x = 0;
    } else if (mapped_x >= MY_DISP_HOR_RES) {
        mapped_x = MY_DISP_HOR_RES - 1;
    }
    if (mapped_y < 0) {
        mapped_y = 0;
    } else if (mapped_y >= MY_DISP_VER_RES) {
        mapped_y = MY_DISP_VER_RES - 1;
    }

    *x = mapped_x;
    *y = mapped_y;
#else
    (void)x;
    (void)y;
#endif
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

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    short raw_x;
    short raw_y;
    int32_t last_x;
    int32_t last_y;
    unsigned char status;

    (void)indev;

    extern u32 timer_get_ms(void);
    get_touch_x_y_status(&raw_x, &raw_y, &status);
    last_x = raw_x;
    last_y = raw_y;
    touchpad_map_to_display(&last_x, &last_y);

    if (status) {
        data->state = LV_INDEV_STATE_PR;
        if (!s_gesture.is_tracking) {
            s_gesture.is_tracking = 1;
            s_gesture.start_x = last_x;
            s_gesture.start_y = last_y;
            s_gesture.start_time = timer_get_ms();
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
        if (s_gesture.is_tracking) {
            int32_t dx = last_x - s_gesture.start_x;
            int32_t dy = last_y - s_gesture.start_y;
            int32_t abs_dx = (dx >= 0) ? dx : -dx;
            int32_t abs_dy = (dy >= 0) ? dy : -dy;
            uint32_t elapsed = timer_get_ms() - s_gesture.start_time;

            if (elapsed < GESTURE_TIME_MAX) {
                if (abs_dy > abs_dx && abs_dy > GESTURE_THRESHOLD) {
                    touchpad_dispatch_page_gesture((dy < 0) ? 1 : 2);
                } else if (abs_dx > abs_dy && abs_dx > GESTURE_THRESHOLD) {
                    touchpad_dispatch_page_gesture((dx < 0) ? 3 : 4);
                }
            }

            s_gesture.is_tracking = 0;
        }
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

void lv_port_indev_init(void)
{
    lv_indev_t *indev_touchpad = lv_indev_create();

    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}

#endif
