/**
 * @file    d_pwm.h
 * @brief   D_Code PWM platform adapter.
 */

#ifndef D_PWM_H
#define D_PWM_H

#include "framework/d_error.h"
#include <stdint.h>

struct pwm_platform_data;

d_err_t d_pwm_open(const char *dev_name, struct pwm_platform_data *cfg, void **handle);
d_err_t d_pwm_set_duty(void *handle, struct pwm_platform_data *cfg, uint8_t duty);
d_err_t d_pwm_run(void *handle, struct pwm_platform_data *cfg);
void d_pwm_close(void *handle);

#endif /* D_PWM_H */
