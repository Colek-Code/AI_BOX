/**
 * @file d_screen_gc9307_hyn.h
 * @brief 声明 GC9307/HYN 屏幕配置的初始化与坐标适配，不承载页面或存储策略。
 */

#ifndef D_SCREEN_GC9307_HYN_H
#define D_SCREEN_GC9307_HYN_H

#include <stdint.h>

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#include "framework/d_error.h"
#include "platform/screen/lcd/d_lcd_compat.h"

/**
 * @brief 同步初始化面板、TE 及可选 HYN 触摸，并报告面板尺寸。
 * @param[in] pdata 有效且持续存活的板级平台描述符。
 * @param[out] width 可为 NULL；仅在面板初始化成功后写入面板宽度。
 * @param[out] height 可为 NULL；仅在面板初始化成功后写入面板高度。
 * @return pdata 为空为 D_ERR_INVALID_ARG；面板初始化失败为 D_ERR_IO；面板成功时为 D_OK，即使可选触摸启动失败也允许显示降级。
 * @note 由显示启动任务串行调用；不会因可选触摸失败回收已初始化的面板，不把显示成功当作触摸就绪证明。
 */
d_err_t d_screen_gc9307_hyn_init(const struct lcd_platform_data *pdata,
                                 uint16_t *width, uint16_t *height);
/**
 * @brief 在显示启动任务中保留低频休眠循环。
 * @note 常驻且不返回，每轮延迟既有 SCREEN_GC9307_IDLE_TICKS；不在 UI 线程调用，不驱动页面生命周期。
 */
void d_screen_gc9307_hyn_idle(void);

/**
 * @brief 按本次读取的渲染角度把触摸原生点转换为逻辑点。
 * @param[in] native_x 原生触摸横坐标。
 * @param[in] native_y 原生触摸纵坐标。
 * @param[out] logical_x 可为 NULL，接收逻辑横坐标。
 * @param[out] logical_y 可为 NULL，接收逻辑纵坐标。
 * @note 用于自定义手势；真实 LVGL 输入仍由其自身旋转。只转换，不裁剪范围；方向取本次快照，不证明该方向已经显示完成。
 */
void d_screen_gc9307_hyn_native_to_logical(int32_t native_x, int32_t native_y,
                                            int32_t *logical_x, int32_t *logical_y);
/**
 * @brief 按本次读取的渲染角度把触摸位移向量转换为逻辑位移。
 * @param[in] native_dx 原生横向位移。
 * @param[in] native_dy 原生纵向位移。
 * @param[out] logical_dx 可为 NULL，接收逻辑横向位移。
 * @param[out] logical_dy 可为 NULL，接收逻辑纵向位移。
 * @note 仅旋转向量，不加入屏幕尺寸偏移；本次方向读取独立于点坐标转换的另一次读取。
 */
void d_screen_gc9307_hyn_delta_to_logical(int32_t native_dx, int32_t native_dy,
                                           int32_t *logical_dx, int32_t *logical_dy);

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
#endif /* D_SCREEN_GC9307_HYN_H */
