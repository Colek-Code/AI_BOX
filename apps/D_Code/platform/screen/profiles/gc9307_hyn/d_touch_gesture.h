/**
 * @file d_touch_gesture.h
 * @brief 声明 HYN 任务中的软件手势观察器，不直接驱动 LVGL 页面输入。
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
 * @brief 同步接收逻辑 320×240 坐标域的手势观察事件。
 * @param[in] event 栈上借用事件，只在当前回调期间有效。
 * @note 在 hyn_touch 任务执行，不能直接调用 LVGL，也不能保存该事件指针。
 */
typedef void (*d_touch_gesture_event_cb_t)(const d_touch_gesture_event_t *event);

typedef struct {
    d_touch_hyn_point_t point;
    s16 dx;
    s16 dy;
    u32 timestamp_ms;
    u8 points;
} d_touch_gesture_sample_t;

/**
 * @brief 设置或清除软件手势观察回调。
 * @param[in] cb 持续有效的静态回调；NULL 表示不再通知。
 * @return 当前始终为 D_OK，不检查旧回调是否在途。
 * @note 由调用方保证注册与 HYN 任务使用的生命周期协调；本层没有锁、替换凭据或在途卸载确认，不将清回调等同于回调已退出。
 */
d_err_t d_touch_gesture_set_event_cb(d_touch_gesture_event_cb_t cb);
/**
 * @brief 清除本观察器的跟踪及双击记忆，不生成 UP、点击或滑动。
 * @note 与样本报告串行；不清 HYN 取消代次，不复位 LVGL 输入设备，两者由各自模块负责。
 */
void d_touch_gesture_reset(void);
/**
 * @brief 记录有效接触的开始位置与时刻，并同步通知 DOWN。
 * @param[in] sample HYN 任务本次有效样本，调用中复制；NULL 时不处理。
 * @note 本次 DOWN 的位移清零；回调只借用转换后的栈上事件。
 */
void d_touch_gesture_report_down(const d_touch_gesture_sample_t *sample);
/**
 * @brief 同步通知有效移动及当前已跟踪的持续时间。
 * @param[in] sample HYN 任务本次有效移动样本，NULL 时不处理。
 * @note 没有开始跟踪时持续时间为零；不自行建立新的按下。
 */
void d_touch_gesture_report_move(const d_touch_gesture_sample_t *sample);
/**
 * @brief 通知正常松手，并按开关尝试识别点击、双击或轴向滑动。
 * @param[in] sample 由读取成功的无接触路径产生的样本，NULL 时不处理。
 * @note 未知读取失败须调用 reset 而非此入口。软件识别开启时按既有阈值完成跟踪；本观察器不替代 LVGL 的正常点击/滚动链。
 */
void d_touch_gesture_report_up(const d_touch_gesture_sample_t *sample);

#endif /* D_LCD_ENABLE && D_TOUCH_HYN_DRIVER_ENABLE */
#endif /* D_TOUCH_GESTURE_H */
