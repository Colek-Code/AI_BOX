/**
 * @file    d_pwm.c
 * @brief   提供 D_Code PWM 平台适配，不在上层暴露 SDK 设备调用。
 */

#include "config/app_config.h"
#include "platform/pwm/d_pwm.h"
#include "asm/pwm.h"
#include "device/device.h"

/**
 * @brief 将占空比限制在不超过 100 的范围。
 * @param[in] duty 无符号百分比输入。
 * @return 输入不超过 100 时为原值，否则为 100。
 */
static uint8_t d_pwm_clamp_duty(uint8_t duty)
{
    if (duty > 100) {
        return 100;
    }

    return duty;
}

/**
 * @brief 按 SDK 设备名同步打开 PWM 设备。 完整合同见 d_pwm.h。
 */
d_err_t d_pwm_open(const char *dev_name, struct pwm_platform_data *cfg, void **handle)
{
#ifdef D_PWM_ENABLE
    void *pwm_handle;

    if ((dev_name == NULL) || (cfg == NULL) || (handle == NULL)) {
        return D_ERR_INVALID_ARG;
    }

    pwm_handle = dev_open(dev_name, cfg);
    if (pwm_handle == NULL) {
        *handle = NULL;
        return D_ERR_NOT_FOUND;
    }

    *handle = pwm_handle;
    return D_OK;
#else
    (void)dev_name;
    (void)cfg;
    (void)handle;
    return D_ERR_NOT_READY;
#endif
}

/**
 * @brief 用给定配置同步发送 PWM_RUN。 完整合同见 d_pwm.h。
 */
d_err_t d_pwm_run(void *handle, struct pwm_platform_data *cfg)
{
#ifdef D_PWM_ENABLE
    if ((handle == NULL) || (cfg == NULL)) {
        return D_ERR_INVALID_ARG;
    }

    if (dev_ioctl(handle, PWM_RUN, (u32)cfg) != 0) {
        return D_ERR_IO;
    }

    return D_OK;
#else
    (void)handle;
    (void)cfg;
    return D_ERR_NOT_READY;
#endif
}

/**
 * @brief 限制百分比上限后更新配置并同步写入 PWM 设备。 完整合同见 d_pwm.h。
 */
d_err_t d_pwm_set_duty(void *handle, struct pwm_platform_data *cfg, uint8_t duty)
{
#ifdef D_PWM_ENABLE
    if ((handle == NULL) || (cfg == NULL)) {
        return D_ERR_INVALID_ARG;
    }

    cfg->duty = (float)d_pwm_clamp_duty(duty);
    if (dev_write(handle, cfg, 0) != 0) {
        return D_ERR_IO;
    }

    return D_OK;
#else
    (void)handle;
    (void)cfg;
    (void)duty;
    return D_ERR_NOT_READY;
#endif
}

/**
 * @brief 关闭非空 PWM 句柄；功能关闭时不执行设备操作。 完整合同见 d_pwm.h。
 */
void d_pwm_close(void *handle)
{
#ifdef D_PWM_ENABLE
    if (handle != NULL) {
        dev_close(handle);
    }
#else
    (void)handle;
#endif
}
