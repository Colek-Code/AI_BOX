/** @file power_ctrl_appearance.h @brief Appearance provider's shutdown boundary. */
#ifndef POWER_CTRL_APPEARANCE_H
#define POWER_CTRL_APPEARANCE_H
#include "framework/d_error.h"
#include <stdint.h>

/* Product's provider implements this fixed phase; no additional shutdown callback is registered. */
void power_ctrl_appearance_begin(void);
d_err_t power_ctrl_appearance_wait_idle(uint32_t timeout_ms);
void power_ctrl_appearance_cancel(void);
#endif
