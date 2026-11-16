/**
 * @file d_touch_gesture.c
 * @brief 在 HYN 任务内识别软件点击/滑动并同步通知观察者，不持有 UI 对象。
 */

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

/**
 * @brief 计算两次原生坐标的有符号差。
 * @param[in] end 终点坐标。
 * @param[in] start 起点坐标。
 * @return end-start 的 s16 转换值。
 */
static s16 touch_delta_u16(u16 end, u16 start)
{
    return (s16)((s32)end - (s32)start);
}

/**
 * @brief 把有符号位移转成无符号幅度。
 * @param[in] value 16 位有符号位移。
 * @return 位移绝对值，以 u16 表示。
 */
static u16 touch_abs_s16(s16 value)
{
    return (value < 0) ? (u16)(-value) : (u16)value;
}

#ifdef D_TOUCH_GESTURE_LOG_ENABLE
/**
 * @brief 选择不改变既有日志格式的手势名称。
 * @param[in] type 手势事件类型。
 * @return 已知类型的静态日志名称；未知类型为原有 unknown 字符串。
 */
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

/**
 * @brief 把原生样本转换为逻辑事件并同步通知观察者。
 * @param[in] type 本次事件类型。
 * @param[in] sample 借用的原生样本，NULL 时不处理。
 * @param[in] duration_ms 本事件关联的时间值；双击使用两次点击间隔，其他事件由调用方给定。
 * @note 点和向量使用本次方向查询转换；事件只在栈上存在，回调不在 LVGL 线程执行。
 */
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
/**
 * @brief 按相邻点击的时间和距离选择单击或双击，并更新双击记忆。
 * @param[in] sample 已通过点击位移/时长条件的有效样本。
 * @param[in] duration_ms 本次按压持续毫秒数。
 * @note 双击事件的时间字段改用两次点击间隔，成功识别后清双击记忆。
 */
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

/**
 * @brief 从跟踪起点计算逻辑总位移、幅度及持续时间。
 * @param[in] sample 正常结束样本。
 * @param[out] motion 有效运动描述，包含复制样本及统计。
 * @note 合成手势描述主接触，将样本点数设为 1；不把正常松手报告的零点数沿用为手势点数。
 */
static void touch_prepare_motion(const d_touch_gesture_sample_t *sample,
                                 d_touch_gesture_motion_t *motion)
{
    s16 dx = touch_delta_u16(sample->point.x, s_touch_gesture.start_x);
    s16 dy = touch_delta_u16(sample->point.y, s_touch_gesture.start_y);

    motion->event_sample = *sample;
    motion->event_sample.dx = dx;
    motion->event_sample.dy = dy;
    /* 正常松手报告零接触点；合成手势描述本次主接触。 */
    motion->event_sample.points = 1;
    d_screen_gc9307_hyn_delta_to_logical(dx, dy, &motion->logical_dx, &motion->logical_dy);
    motion->abs_dx = touch_abs_s16((s16)motion->logical_dx);
    motion->abs_dy = touch_abs_s16((s16)motion->logical_dy);
    motion->duration_ms = sample->timestamp_ms - s_touch_gesture.start_ms;
}

/**
 * @brief 按已有时长、距离及轴优势阈值结算一次已跟踪手势。
 * @param[in] sample 结束样本；关闭识别时不读取。
 */
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
/**
 * @brief 软件手势关闭时保留空结算入口，不生成高级手势。
 * @param[in] sample 结束样本；关闭识别时不读取。
 */
static void touch_finish_gesture(const d_touch_gesture_sample_t *sample)
{
    (void)sample;
}
#endif

/**
 * @brief 登记观察回调；完整合同见 d_touch_gesture.h。
 */
d_err_t d_touch_gesture_set_event_cb(d_touch_gesture_event_cb_t cb)
{
    s_touch_gesture.event_cb = cb;
    return D_OK;
}

/**
 * @brief 取消本地手势跟踪；完整合同见 d_touch_gesture.h。
 */
void d_touch_gesture_reset(void)
{
    s_touch_gesture.tracking = 0;
    s_touch_gesture.last_tap_valid = 0;
}

/**
 * @brief 报告有效按下；完整合同见 d_touch_gesture.h。
 */
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

/**
 * @brief 报告有效移动；完整合同见 d_touch_gesture.h。
 */
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

/**
 * @brief 报告正常松手并尝试识别手势；完整合同见 d_touch_gesture.h。
 */
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
