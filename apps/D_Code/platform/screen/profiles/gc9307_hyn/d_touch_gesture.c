#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_TOUCH_HYN_DRIVER_ENABLE)

#include "system/includes.h"
#include "platform/screen/profiles/gc9307_hyn/d_touch_gesture.h"
#include "platform/screen/profiles/gc9307_hyn/d_screen_gc9307_hyn.h"

#define TOUCH_GESTURE_TAG                "[TOUCH_GESTURE] "
#define TOUCH_TAP_MAX_MOVE_PX            12
#define TOUCH_TAP_MAX_MS                 250
#define TOUCH_DOUBLE_TAP_MAX_MS          350
#define TOUCH_DOUBLE_TAP_MAX_DIST_PX     24
#define TOUCH_SWIPE_MIN_DIST_PX          50
#define TOUCH_SWIPE_MAX_MS               800
#define TOUCH_SWIPE_AXIS_MARGIN_PX       20

typedef struct {
    u8 tracking;
    u8 last_tap_valid;
    u16 start_x;
    u16 start_y;
    u16 last_tap_x;
    u16 last_tap_y;
    u32 start_ms;
    u32 last_tap_ms;
    d_touch_gesture_event_cb_t event_cb;
} d_touch_gesture_state_t;

typedef struct {
    d_touch_gesture_sample_t event_sample;
    s32 logical_dx;
    s32 logical_dy;
    u16 abs_dx;
    u16 abs_dy;
    u32 duration_ms;
} d_touch_gesture_motion_t;

static d_touch_gesture_state_t s_touch_gesture;

static s16 touch_delta_u16(u16 end, u16 start)
{
    return (s16)((s32)end - (s32)start);
}

static u16 touch_abs_s16(s16 value)
{
    return (value < 0) ? (u16)(-value) : (u16)value;
}

#ifdef D_TOUCH_GESTURE_LOG_ENABLE
static const char *touch_event_name(d_touch_gesture_event_type_t type)
{
    switch (type) {
    case D_TOUCH_GESTURE_EVENT_DOWN:
        return "down";
    case D_TOUCH_GESTURE_EVENT_MOVE:
        return "move";
    case D_TOUCH_GESTURE_EVENT_UP:
        return "up";
    case D_TOUCH_GESTURE_EVENT_TAP:
        return "tap";
    case D_TOUCH_GESTURE_EVENT_DOUBLE_TAP:
        return "double_tap";
    case D_TOUCH_GESTURE_EVENT_SWIPE_UP:
        return "swipe_up";
    case D_TOUCH_GESTURE_EVENT_SWIPE_DOWN:
        return "swipe_down";
    case D_TOUCH_GESTURE_EVENT_SWIPE_LEFT:
        return "swipe_left";
    case D_TOUCH_GESTURE_EVENT_SWIPE_RIGHT:
        return "swipe_right";
    default:
        return "unknown";
    }
}
#endif

static void touch_emit_event(d_touch_gesture_event_type_t type,
                             const d_touch_gesture_sample_t *sample, u32 duration_ms)
{
    d_touch_gesture_event_t event;
    s32 logical_x;
    s32 logical_y;
    s32 logical_dx;
    s32 logical_dy;

    if (sample == NULL) {
        return;
    }

    d_screen_gc9307_hyn_native_to_logical(sample->point.x, sample->point.y, &logical_x, &logical_y);
    d_screen_gc9307_hyn_delta_to_logical(sample->dx, sample->dy, &logical_dx, &logical_dy);
    event.type = type;
    event.x = (u16)logical_x;
    event.y = (u16)logical_y;
    event.dx = (s16)logical_dx;
    event.dy = (s16)logical_dy;
    event.id = sample->point.id;
    event.points = sample->points;
    event.duration_ms = duration_ms;

    if (s_touch_gesture.event_cb != NULL) {
        s_touch_gesture.event_cb(&event);
    }

#ifdef D_TOUCH_GESTURE_LOG_ENABLE
    if (type != D_TOUCH_GESTURE_EVENT_MOVE) {
        printf(TOUCH_GESTURE_TAG "%s x=%u y=%u dx=%d dy=%d id=%u points=%u duration=%u\n",
               touch_event_name(type), event.x, event.y, event.dx, event.dy,
               event.id, event.points, (unsigned)event.duration_ms);
    }
#endif
}

#ifdef D_TOUCH_GESTURE_ENABLE
static void touch_emit_tap(const d_touch_gesture_sample_t *sample, u32 duration_ms)
{
    d_touch_gesture_sample_t tap_sample = *sample;
    s16 tap_dx = touch_delta_u16(sample->point.x, s_touch_gesture.last_tap_x);
    s16 tap_dy = touch_delta_u16(sample->point.y, s_touch_gesture.last_tap_y);
    u32 tap_gap = sample->timestamp_ms - s_touch_gesture.last_tap_ms;

    tap_sample.dx = 0;
    tap_sample.dy = 0;

    if (s_touch_gesture.last_tap_valid &&
        (tap_gap <= TOUCH_DOUBLE_TAP_MAX_MS) &&
        (touch_abs_s16(tap_dx) <= TOUCH_DOUBLE_TAP_MAX_DIST_PX) &&
        (touch_abs_s16(tap_dy) <= TOUCH_DOUBLE_TAP_MAX_DIST_PX)) {
        touch_emit_event(D_TOUCH_GESTURE_EVENT_DOUBLE_TAP, &tap_sample, tap_gap);
        s_touch_gesture.last_tap_valid = 0;
        return;
    }

    touch_emit_event(D_TOUCH_GESTURE_EVENT_TAP, &tap_sample, duration_ms);
    s_touch_gesture.last_tap_valid = 1;
    s_touch_gesture.last_tap_x = sample->point.x;
    s_touch_gesture.last_tap_y = sample->point.y;
    s_touch_gesture.last_tap_ms = sample->timestamp_ms;
}

static void touch_prepare_motion(const d_touch_gesture_sample_t *sample,
                                 d_touch_gesture_motion_t *motion)
{
    s16 dx = touch_delta_u16(sample->point.x, s_touch_gesture.start_x);
    s16 dy = touch_delta_u16(sample->point.y, s_touch_gesture.start_y);

    motion->event_sample = *sample;
    motion->event_sample.dx = dx;
    motion->event_sample.dy = dy;
    /* Release reports zero contacts; synthesized gestures describe the primary contact. */
    motion->event_sample.points = 1;
    d_screen_gc9307_hyn_delta_to_logical(dx, dy, &motion->logical_dx, &motion->logical_dy);
    motion->abs_dx = touch_abs_s16((s16)motion->logical_dx);
    motion->abs_dy = touch_abs_s16((s16)motion->logical_dy);
    motion->duration_ms = sample->timestamp_ms - s_touch_gesture.start_ms;
}

static void touch_finish_gesture(const d_touch_gesture_sample_t *sample)
{
    d_touch_gesture_motion_t motion;

    if (!s_touch_gesture.tracking) {
        return;
    }

    touch_prepare_motion(sample, &motion);
    s_touch_gesture.tracking = 0;

    if ((motion.duration_ms <= TOUCH_SWIPE_MAX_MS) &&
        (motion.abs_dy >= TOUCH_SWIPE_MIN_DIST_PX) &&
        (motion.abs_dy >= (motion.abs_dx + TOUCH_SWIPE_AXIS_MARGIN_PX))) {
        touch_emit_event((motion.logical_dy < 0) ? D_TOUCH_GESTURE_EVENT_SWIPE_UP :
                         D_TOUCH_GESTURE_EVENT_SWIPE_DOWN,
                         &motion.event_sample, motion.duration_ms);
        s_touch_gesture.last_tap_valid = 0;
        return;
    }

    if ((motion.duration_ms <= TOUCH_SWIPE_MAX_MS) &&
        (motion.abs_dx >= TOUCH_SWIPE_MIN_DIST_PX) &&
        (motion.abs_dx >= (motion.abs_dy + TOUCH_SWIPE_AXIS_MARGIN_PX))) {
        touch_emit_event((motion.logical_dx < 0) ? D_TOUCH_GESTURE_EVENT_SWIPE_LEFT :
                         D_TOUCH_GESTURE_EVENT_SWIPE_RIGHT,
                         &motion.event_sample, motion.duration_ms);
        s_touch_gesture.last_tap_valid = 0;
        return;
    }

    if ((motion.duration_ms <= TOUCH_TAP_MAX_MS) &&
        (motion.abs_dx <= TOUCH_TAP_MAX_MOVE_PX) &&
        (motion.abs_dy <= TOUCH_TAP_MAX_MOVE_PX)) {
        touch_emit_tap(&motion.event_sample, motion.duration_ms);
    }
}
#else
static void touch_finish_gesture(const d_touch_gesture_sample_t *sample)
{
    (void)sample;
}
#endif

d_err_t d_touch_gesture_set_event_cb(d_touch_gesture_event_cb_t cb)
{
    s_touch_gesture.event_cb = cb;
    return D_OK;
}

void d_touch_gesture_reset(void)
{
    s_touch_gesture.tracking = 0;
    s_touch_gesture.last_tap_valid = 0;
}

void d_touch_gesture_report_down(const d_touch_gesture_sample_t *sample)
{
    d_touch_gesture_sample_t event_sample;

    if (sample == NULL) {
        return;
    }
    event_sample = *sample;
    event_sample.dx = 0;
    event_sample.dy = 0;
    s_touch_gesture.tracking = 1;
    s_touch_gesture.start_x = sample->point.x;
    s_touch_gesture.start_y = sample->point.y;
    s_touch_gesture.start_ms = sample->timestamp_ms;
    touch_emit_event(D_TOUCH_GESTURE_EVENT_DOWN, &event_sample, 0);
}

void d_touch_gesture_report_move(const d_touch_gesture_sample_t *sample)
{
    u32 duration_ms;

    if (sample == NULL) {
        return;
    }
    duration_ms = s_touch_gesture.tracking ?
                  (sample->timestamp_ms - s_touch_gesture.start_ms) : 0;
    touch_emit_event(D_TOUCH_GESTURE_EVENT_MOVE, sample, duration_ms);
}

void d_touch_gesture_report_up(const d_touch_gesture_sample_t *sample)
{
    d_touch_gesture_sample_t event_sample;
    u32 duration_ms = 0;

    if (sample == NULL) {
        return;
    }
    event_sample = *sample;
    event_sample.dx = 0;
    event_sample.dy = 0;
    if (s_touch_gesture.tracking) {
        event_sample.dx = touch_delta_u16(sample->point.x, s_touch_gesture.start_x);
        event_sample.dy = touch_delta_u16(sample->point.y, s_touch_gesture.start_y);
        duration_ms = sample->timestamp_ms - s_touch_gesture.start_ms;
    }
    touch_emit_event(D_TOUCH_GESTURE_EVENT_UP, &event_sample, duration_ms);
    touch_finish_gesture(&event_sample);
}

#endif /* D_LCD_ENABLE && D_TOUCH_HYN_DRIVER_ENABLE */
