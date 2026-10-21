/**
 * @file    d_pwm.c
 * @brief   D_Code PWM platform adapter.
 */

#include "config/app_config.h"
#include "platform/pwm/d_pwm.h"
#include "asm/pwm.h"
#include "device/device.h"

static uint8_t d_pwm_clamp_duty(uint8_t duty)
{
    if (duty > 100) {
        return 100;
    }

    return duty;
}

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
