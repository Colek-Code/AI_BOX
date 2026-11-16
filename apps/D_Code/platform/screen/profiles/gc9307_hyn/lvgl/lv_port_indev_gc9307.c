/**
 * @file lv_port_indev_gc9307.c
 * @brief 将 HYN 质量快照接入真实 LVGL 输入，保留唤醒与取消过滤。
 * D++: 归属 D_Code GC9307/HYN 显示配置。
 */

#include "framework/d_lvgl_port.h"
#include "lvgl.h"
#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#include "platform/screen/profiles/gc9307_hyn/d_touch_hyn.h"
#include "platform/screen/profiles/gc9307_hyn/d_screen_gc9307_hyn.h"
#include "platform/screen/power/d_screen_power.h"
#include "system/sys_time.h"
#include "lv_port_indev_gc9307.h"
#include "lv_port_disp_gc9307.h"
static lv_indev_t *s_touchpad;
static uint8_t s_port_wait_release;
static uint32_t s_hyn_cancel_epoch_seen;

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

/**
 * @brief 注册 HYN 指针输入及读取回调；通用合同见 framework/d_lvgl_port.h。
 */
void lv_port_indev_init(void)
{
    lv_indev_t *indev_touchpad;

    /* D++: 没有显示对象的指针输入会被 LVGL 后续处理解引用，故先拒绝装配。 */
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
    s_touchpad = indev_touchpad;
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}

/**
 * @brief 取消按压和本地手势并封闭至真实松手；合同见 lv_port_indev_gc9307.h。
 */
void lv_port_indev_gc9307_reset(void)
{
    s_gesture.is_tracking = 0;
    s_port_wait_release = 1U;
    if (s_touchpad != NULL) {
        lv_indev_reset(s_touchpad, NULL);
        lv_indev_wait_release(s_touchpad);
    }
}

/**
 * @brief 保留本配置的输入初始化钩子，当前无额外操作。
 */
static void touchpad_init(void)
{
}

/**
 * @brief 在启用 LVGL 时把非首页的手势交给页面管理器。
 * @param[in] gesture_type 已有页面手势编号。
 * @note 首页手势由首页自身处理，不通过本后备入口导航。
 */
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

/**
 * @brief 只在正常松手时按本次位移和时长结算页面后备手势。
 * @param[in] logic_x 松手时的逻辑横坐标。
 * @param[in] logic_y 松手时的逻辑纵坐标。
 * @note 取消及唤醒过滤路径不调用此结算入口；结算后清本地跟踪。
 */
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

/**
 * @brief 复制 HYN 质量事实并处理方向门禁，再交付真实 LVGL 原生坐标样本。
 * @param[in] indev 本次 LVGL 输入设备，用于取消及等待松手。
 * @param[out] data LVGL 提供的状态和点坐标输出。
 * @note 仅 UI 线程执行；取消代次变化可能跨过整个轮询间隔，必须按变化处理而不能只观察当前电平。
 * @note 恢复门禁只由真实接触松开打开。取消路径先 reset 并交付 REL，不结算旧手势。
 * @note 原生坐标由 LVGL 按当前方向转换，不重复旋转坐标。
 */
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
    uint8_t release_gate;
    uint8_t hyn_fault;
    d_touch_hyn_snapshot_t touch;

    (void)indev;
    d_touch_hyn_get_snapshot(&touch);
    raw_x = (short)touch.x;
    raw_y = (short)touch.y;
    status = touch.pressed;
    /* 取消可能在生产者/消费者两次读取之间完整发生，不能只看当前电平。 */
    hyn_fault = touch.cancel_epoch != s_hyn_cancel_epoch_seen;
    s_hyn_cancel_epoch_seen = touch.cancel_epoch;
    raw_pressed = status ? 1U : 0U;
    release_gate = hyn_fault || touch.needs_release ||
        lv_port_disp_gc9307_input_blocked() || s_port_wait_release;
    if (release_gate) {
        /* 只有真实接触松开才能解除门禁；
         * 先向 LVGL 交付中性样本，下一次读取才可能开始新动作。 */
        s_gesture.is_tracking = 0;
        s_port_wait_release = raw_pressed;
        lv_indev_reset(indev, NULL);
        lv_indev_wait_release(indev);
        (void)d_screen_power_filter_touch(0U);
        data->state = LV_INDEV_STATE_REL;
        data->point.x = raw_x;
        data->point.y = raw_y;
        return;
    }
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
            /* 唤醒或保护期内按压不能结算先前遗留的页面手势。 */
            s_gesture.is_tracking = 0;
        } else {
            touchpad_report_release(logic_x, logic_y);
        }
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
