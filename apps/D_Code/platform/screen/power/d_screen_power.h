/**
 * @file d_screen_power.h
 * @brief LCD backlight screen-power service.
 */
#ifndef D_SCREEN_POWER_H
#define D_SCREEN_POWER_H

#include <stdint.h>
#include "framework/d_error.h"

/*
 * 平台级屏幕电源服务：统一管理空闲计时、背光关闭、触摸唤醒过滤。
 * UI 页面只负责配置超时时间和触摸唤醒开关，不直接操作触摸门控和 TE 背光恢复。
 */
void d_screen_power_init(void);
void d_screen_power_update(uint32_t elapsed_ms);
void d_screen_power_reset_idle(void);
/* UI thread only. Each successful hold requires one release; manual off still wins. */
d_err_t d_screen_power_hold_auto_off(void);
void d_screen_power_release_auto_off(void);
uint8_t d_screen_power_filter_touch(uint8_t pressed);
void d_screen_power_set_timeout_index(uint8_t index);
uint8_t d_screen_power_get_timeout_index(void);
uint32_t d_screen_power_get_timeout_ms(void);
void d_screen_power_set_touch_wake_enabled(uint8_t enabled);
uint8_t d_screen_power_get_touch_wake_enabled(void);
uint8_t d_screen_power_is_off(void);
void d_screen_power_wake(void);
/*
 * Clear screen-off without driving backlight. Use when the caller will apply
 * the target brightness once (avoids last_nonzero flash then re-apply).
 */
void d_screen_power_clear_off(void);
/* Force screen-off + backlight off (rollback after failed clear_off apply). */
void d_screen_power_force_off(void);

#endif /* D_SCREEN_POWER_H */
