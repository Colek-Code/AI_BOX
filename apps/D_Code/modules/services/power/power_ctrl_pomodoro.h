/** @file power_ctrl_pomodoro.h @brief Optional Product reminder shutdown provider. */
#ifndef POWER_CTRL_POMODORO_H
#define POWER_CTRL_POMODORO_H
#include "framework/d_error.h"
#include <stdint.h>
void power_ctrl_pomodoro_begin(void);
d_err_t power_ctrl_pomodoro_wait_idle(uint32_t timeout_ms);
void power_ctrl_pomodoro_cancel(void);
#endif
