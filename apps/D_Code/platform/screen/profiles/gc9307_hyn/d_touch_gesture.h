/**
 * @file d_touch_gesture.h
 * @brief Touch event and software gesture recognizer.
 */

#ifndef D_TOUCH_GESTURE_H
#define D_TOUCH_GESTURE_H

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_TOUCH_HYN_DRIVER_ENABLE)

#include "platform/screen/profiles/gc9307_hyn/d_touch_hyn.h"

typedef enum {
    D_TOUCH_GESTURE_EVENT_NONE = 0,
    D_TOUCH_GESTURE_EVENT_DOWN,
    D_TOUCH_GESTURE_EVENT_MOVE,
    D_TOUCH_GESTURE_EVENT_UP,
    D_TOUCH_GESTURE_EVENT_TAP,
    D_TOUCH_GESTURE_EVENT_DOUBLE_TAP,
    D_TOUCH_GESTURE_EVENT_SWIPE_UP,
    D_TOUCH_GESTURE_EVENT_SWIPE_DOWN,
    D_TOUCH_GESTURE_EVENT_SWIPE_LEFT,
    D_TOUCH_GESTURE_EVENT_SWIPE_RIGHT,
} d_touch_gesture_event_type_t;

typedef struct {
    d_touch_gesture_event_type_t type;
    u16 x;
    u16 y;
    s16 dx;
    s16 dy;
    u8 id;
    u8 points;
    u32 duration_ms;
} d_touch_gesture_event_t;

/**
 * Gesture events use the logical 320x240 visual coordinate space. The callback
 * runs synchronously in the hyn_touch task; the event pointer is valid only
 * during the callback and the callback must not call LVGL directly.
 */
typedef void (*d_touch_gesture_event_cb_t)(const d_touch_gesture_event_t *event);

typedef struct {
    d_touch_hyn_point_t point;
    s16 dx;
    s16 dy;
    u32 timestamp_ms;
    u8 points;
} d_touch_gesture_sample_t;

d_err_t d_touch_gesture_set_event_cb(d_touch_gesture_event_cb_t cb);
void d_touch_gesture_reset(void);
void d_touch_gesture_report_down(const d_touch_gesture_sample_t *sample);
void d_touch_gesture_report_move(const d_touch_gesture_sample_t *sample);
void d_touch_gesture_report_up(const d_touch_gesture_sample_t *sample);

#endif /* D_LCD_ENABLE && D_TOUCH_HYN_DRIVER_ENABLE */
#endif /* D_TOUCH_GESTURE_H */
