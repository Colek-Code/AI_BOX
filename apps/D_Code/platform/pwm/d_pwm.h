/**
 * @file    d_pwm.h
 * @brief   提供 D_Code PWM 平台适配，不在上层暴露 SDK 设备调用。
 */

#ifndef D_PWM_H
#define D_PWM_H

#include "framework/d_error.h"
#include <stdint.h>

struct pwm_platform_data;

/**
 * @brief 按 SDK 设备名同步打开 PWM 设备。
 * @param[in] dev_name SDK PWM 设备名称。
 * @param[in,out] cfg SDK 平台配置，由底层设备接口使用；调用方持有。
 * @param[out] handle 成功时接收设备句柄；打开失败时置 NULL，参数无效或功能关闭时不写。
 * @return D_OK 为成功；参数无效为 D_ERR_INVALID_ARG；打开失败为 D_ERR_NOT_FOUND；未启用 PWM 为 D_ERR_NOT_READY。
 * @note 成功句柄由调用方保存，并通过 d_pwm_close 释放；本层不保存共享句柄。
 */
d_err_t d_pwm_open(const char *dev_name, struct pwm_platform_data *cfg, void **handle);
/**
 * @brief 限制百分比上限后更新配置并同步写入 PWM 设备。
 * @param[in] handle 已打开的 PWM 句柄。
 * @param[in,out] cfg 调用方持有的配置，duty 字段在设备写入前更新。
 * @param[in] duty 目标占空比，大于 100 的值按 100 处理。
 * @return D_OK 为写入成功；参数无效为 D_ERR_INVALID_ARG；设备写失败为 D_ERR_IO；功能关闭为 D_ERR_NOT_READY。
 * @note 写失败时不回滚 cfg->duty，不能据配置字段认定硬件已经生效；同一设备的调用串行由调用方保证。
 */
d_err_t d_pwm_set_duty(void *handle, struct pwm_platform_data *cfg, uint8_t duty);
/**
 * @brief 用给定配置同步发送 PWM_RUN。
 * @param[in] handle 已打开的 PWM 句柄。
 * @param[in,out] cfg SDK 平台配置，由底层 ioctl 使用。
 * @return D_OK 为 SDK 接受；参数无效为 D_ERR_INVALID_ARG；ioctl 失败为 D_ERR_IO；功能关闭为 D_ERR_NOT_READY。
 */
d_err_t d_pwm_run(void *handle, struct pwm_platform_data *cfg);
/**
 * @brief 关闭非空 PWM 句柄；功能关闭时不执行设备操作。
 * @param[in] handle 调用方持有的 PWM 句柄，可为 NULL。
 * @note 不等待其他调用完成，也不返回 SDK 关闭结果；调用方须确保没有并发使用并自行清除保存的句柄。
 */
void d_pwm_close(void *handle);

#endif /* D_PWM_H */
