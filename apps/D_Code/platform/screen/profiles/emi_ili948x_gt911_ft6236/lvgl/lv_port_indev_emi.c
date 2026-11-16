/**
 * @file lv_port_indev_emi.c
 * @brief 把 EMI 配置的 GT911/FT6236 状态接入 LVGL，并处理既有页面后备手势。
 * D++: 归属 D_Code EMI/ILI948x/GT911/FT6236 显示配置。
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

/**
 * @brief 按编译期横屏选择变换并裁剪 EMI 原生点。
 * @param[in,out] x 有效横坐标指针；横屏时可为空并直接返回。
 * @param[in,out] y 有效纵坐标指针。
 * @note 竖屏配置不变换；不使用 GC9307 的运行方向或原生坐标规则。
 */
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

/**
 * @brief 将非首页的后备手势转给页面管理器。
 * @param[in] gesture_type 既有编号：1上、2下、3左、4右。
 * @note 仅 UI/LVGL 线程调用；首页由自身手势逻辑处理。
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
 * @brief 读取经唤醒过滤的 EMI 状态，交给 LVGL 并在正常结束时结算后备手势。
 * @param[in] indev LVGL 输入对象，当前未直接使用。
 * @param[out] data LVGL 提供的状态及逻辑坐标输出。
 * @note 使用本配置的旧状态接口，不具有 HYN 的取消质量快照或代次门禁；不在这里切换硬件/产品方向。
 */
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

/**
 * @brief 注册 EMI 指针输入及读取回调；通用合同见 framework/d_lvgl_port.h。
 */
void lv_port_indev_init(void)
{
    lv_indev_t *indev_touchpad = lv_indev_create();

    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}

#endif
