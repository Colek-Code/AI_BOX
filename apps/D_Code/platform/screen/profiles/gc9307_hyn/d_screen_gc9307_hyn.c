/**
 * @file d_screen_gc9307_hyn.c
 * @brief 装配 GC9307 面板及 HYN 触摸配置，复用显示端口的运行方向。
 */

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#include "platform/screen/profiles/gc9307_hyn/d_screen_gc9307_hyn.h"
#include "framework/d_log.h"
#include "os/os_api.h"
#include "platform/screen/lcd/d_lcd_compat.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307.h"

#ifdef D_TOUCH_HYN_DRIVER_ENABLE
#include "platform/screen/profiles/gc9307_hyn/d_touch_hyn.h"
#endif

#define TAG "SCREEN_GC9307"
#define SCREEN_GC9307_IDLE_TICKS 200

/**
 * @brief 变换原生触摸点；完整合同见 d_screen_gc9307_hyn.h。
 */
void d_screen_gc9307_hyn_native_to_logical(int32_t native_x, int32_t native_y,
                                            int32_t *logical_x, int32_t *logical_y)
{
    int32_t x;
    int32_t y;
    gc9307_frame_orientation_t orientation;
    lv_port_disp_gc9307_get_orientation(&orientation);
    gc9307_native_to_logical(orientation.angle, native_x, native_y, &x, &y);

    if (logical_x != NULL) {
        *logical_x = x;
    }
    if (logical_y != NULL) {
        *logical_y = y;
    }
}

/**
 * @brief 变换原生触摸位移；完整合同见 d_screen_gc9307_hyn.h。
 */
void d_screen_gc9307_hyn_delta_to_logical(int32_t native_dx, int32_t native_dy,
                                           int32_t *logical_dx, int32_t *logical_dy)
{
    gc9307_frame_orientation_t orientation;
    int32_t dx;
    int32_t dy;
    lv_port_disp_gc9307_get_orientation(&orientation);
    dx = orientation.angle == 90U ? -native_dy : native_dy;
    dy = orientation.angle == 90U ? native_dx : -native_dx;

    if (logical_dx != NULL) {
        *logical_dx = dx;
    }
    if (logical_dy != NULL) {
        *logical_dy = dy;
    }
}

/**
 * @brief 按当前硬件开关尝试初始化 HYN 触摸，并输出结果诊断。
 * @param[in] pdata 由显示初始化调用方保证有效的板级平台描述符。
 * @return 启用时为 d_touch_hyn_init 结果；未启用驱动时为 D_ERR_NOT_READY。
 */
static d_err_t d_screen_gc9307_hyn_start_touch(const struct lcd_platform_data *pdata)
{
#ifdef D_TOUCH_HYN_DRIVER_ENABLE
    d_err_t touch_err = d_touch_hyn_init(pdata);

    if (touch_err == D_OK) {
        LOGI(TAG, "HYN touch driver enabled");
    } else {
        LOGE(TAG, "HYN touch driver start failed: %d", touch_err);
    }
    return touch_err;
#else
    (void)pdata;
    return D_ERR_NOT_READY;
#endif
}

/**
 * @brief 初始化屏幕配置；完整合同见 d_screen_gc9307_hyn.h。
 */
d_err_t d_screen_gc9307_hyn_init(const struct lcd_platform_data *pdata,
                                 uint16_t *width, uint16_t *height)
{
    d_err_t touch_err;

    if (pdata == NULL) {
        return D_ERR_INVALID_ARG;
    }

    LOGI(TAG, "GC9307/HYN profile start");
    LOGI(TAG, "GPIO: rst=%d dc=%d cs=%d clk=%d mosi=%d bl=%d te=%d t_int=%d t_rst=%d spi=%s",
         pdata->rst_pin, pdata->rs_pin, pdata->cs_pin,
         pdata->spi_clk_pin, pdata->spi_mosi_pin, pdata->bl_pin,
         pdata->te_pin, pdata->touch_int_pin, pdata->touch_reset_pin,
         pdata->spi_id ? pdata->spi_id : "null");

    if (d_lcd_init(pdata) != 0) {
        LOGE(TAG, "GC9307 init failed");
        return D_ERR_IO;
    }
    d_lcd_gc9307_te_init(pdata);
    if (width) {
        *width = d_lcd_get_width();
    }
    if (height) {
        *height = d_lcd_get_height();
    }
    LOGI(TAG, "GC9307 SDK LCD %dx%d ready", d_lcd_get_width(), d_lcd_get_height());

    touch_err = d_screen_gc9307_hyn_start_touch(pdata);
    if (touch_err != D_OK) {
        LOGE(TAG, "Display ready, touch start failed: %d", touch_err);
        return D_OK;
    }
    LOGI(TAG, "Display ready: GC9307 LCD, touch ready");

    return D_OK;
}

/**
 * @brief 保留显示任务休眠循环；完整合同见 d_screen_gc9307_hyn.h。
 */
void d_screen_gc9307_hyn_idle(void)
{
    while (1) {
        os_time_dly(SCREEN_GC9307_IDLE_TICKS);
    }
}

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
