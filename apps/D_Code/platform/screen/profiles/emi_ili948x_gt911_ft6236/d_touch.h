/**
 * @file d_touch.h
 * @brief 声明 EMI 配置的 GT911/FT6236 触摸适配，不复用 GC9307/HYN 的取消代次合同。
 */

#ifndef D_TOUCH_H
#define D_TOUCH_H

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)

#include "framework/d_error.h"
#include "platform/screen/lcd/d_lcd_compat.h"

/**
 * @brief 在启动阶段探测 GT911/FT6236，创建读取信号量、中断及常驻任务。
 * @param[in] pdata 有效且持续存在的板级描述符；保留其借用指针。
 * @return 成功创建能力为 D_OK；空参数为 D_ERR_INVALID_ARG；无中断脚或未识别设备为 D_ERR_NOT_FOUND；总线/OS 准备失败为 D_ERR_IO。
 * @note 仅串行启动一次，不提供重复初始化或运行期停止合同。若触摸与面板共用复位脚，沿原策略避免再次复位面板。
 */
d_err_t d_touch_init(const struct lcd_platform_data *pdata);
/**
 * @brief 读取最近触摸坐标，并在 UI 线程执行屏幕唤醒过滤。
 * @param[out] x 可为 NULL，接收原生横坐标。
 * @param[out] y 可为 NULL，接收原生纵坐标。
 * @param[out] status 可为 NULL，接收过滤后的 0/1 接触状态。
 * @note 不是纯只读查询：过滤可能更新空闲及背光唤醒状态。旧配置逐项读取缓存，不提供 HYN 的原子质量快照或取消代次。
 */
void get_touch_x_y_status(s16 *x, s16 *y, unsigned char *status);

#endif /* D_LCD_ENABLE && D_LCD_EMI_ILI948X_ENABLE */
#endif /* D_TOUCH_H */
