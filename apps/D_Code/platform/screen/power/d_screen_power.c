/**
 * @file d_screen_power.c
 * @brief RAM-only LCD backlight screen-power service.
 */
#include "config/app_config.h"
#include "platform/screen/power/d_screen_power.h"

#ifdef D_LCD_ENABLE
#include "platform/screen/d_display.h"
#endif

#define D_SCREEN_POWER_DEFAULT_TIMEOUT_INDEX      4
#define D_SCREEN_POWER_TIMEOUT_COUNT              5
#define D_SCREEN_POWER_TIMEOUT_NEVER_MS           0U
#define D_SCREEN_POWER_WAKE_GUARD_MS              500U

typedef struct {
    uint8_t initialized;
    uint8_t screen_off;
    uint8_t touch_wake_enabled;
    uint8_t timeout_index;
    uint8_t suppress_until_release;
    uint8_t last_raw_pressed;
    uint32_t wake_guard_ms;
    uint32_t idle_ms;
    uint16_t auto_off_holds;
} d_screen_power_ctx_t;

static const uint32_t s_screen_power_timeout_ms[D_SCREEN_POWER_TIMEOUT_COUNT] = {
    30U * 1000U,
    60U * 1000U,
    5U * 60U * 1000U,
    15U * 60U * 1000U,
    D_SCREEN_POWER_TIMEOUT_NEVER_MS,
};

static d_screen_power_ctx_t s_screen_power;

static void d_screen_power_set_backlight(uint8_t on)
{
#ifdef D_LCD_ENABLE
    d_display_set_backlight(on ? 1U : 0U);
#else
    (void)on;
#endif
}

static void d_screen_power_tick_wake_guard(uint32_t elapsed_ms)
{
    if (s_screen_power.wake_guard_ms == 0U) {
        return;
    }

    if (elapsed_ms >= s_screen_power.wake_guard_ms) {
        s_screen_power.wake_guard_ms = 0U;
    } else {
        s_screen_power.wake_guard_ms -= elapsed_ms;
    }
}

static void d_screen_power_enter_off(void)
{
    s_screen_power.screen_off = 1U;
    s_screen_power.wake_guard_ms = D_SCREEN_POWER_WAKE_GUARD_MS;
    s_screen_power.suppress_until_release = s_screen_power.last_raw_pressed ? 1U : 0U;
    d_screen_power_set_backlight(0U);
}

static void d_screen_power_ensure_init(void)
{
    if (s_screen_power.initialized != 0U) {
        return;
    }

    d_screen_power_init();
}

void d_screen_power_init(void)
{
    if (s_screen_power.initialized != 0U) {
        return;
    }

    s_screen_power.initialized = 1U;
    s_screen_power.screen_off = 0U;
    s_screen_power.touch_wake_enabled = 1U;
    s_screen_power.timeout_index = D_SCREEN_POWER_DEFAULT_TIMEOUT_INDEX;
    s_screen_power.suppress_until_release = 0U;
    s_screen_power.last_raw_pressed = 0U;
    s_screen_power.wake_guard_ms = 0U;
    s_screen_power.idle_ms = 0U;
}

void d_screen_power_reset_idle(void)
{
    d_screen_power_ensure_init();

    if (s_screen_power.screen_off == 0U) {
        s_screen_power.idle_ms = 0U;
    }
}

d_err_t d_screen_power_hold_auto_off(void)
{
    d_screen_power_ensure_init();
    if (s_screen_power.auto_off_holds == UINT16_MAX) {
        return D_ERR_FULL;
    }
    s_screen_power.auto_off_holds++;
    s_screen_power.idle_ms = 0U;
    return D_OK;
}

void d_screen_power_release_auto_off(void)
{
    d_screen_power_ensure_init();
    if (s_screen_power.auto_off_holds == 0U) {
        return;
    }
    if (--s_screen_power.auto_off_holds == 0U) {
        s_screen_power.idle_ms = 0U;
    }
}

void d_screen_power_update(uint32_t elapsed_ms)
{
    uint32_t timeout_ms;

    d_screen_power_ensure_init();
    if (s_screen_power.screen_off != 0U) {
        d_screen_power_tick_wake_guard(elapsed_ms);
        return;
    }

    if (s_screen_power.auto_off_holds != 0U) {
        s_screen_power.idle_ms = 0U;
        return;
    }

    timeout_ms = d_screen_power_get_timeout_ms();
    if (timeout_ms == D_SCREEN_POWER_TIMEOUT_NEVER_MS) {
        s_screen_power.idle_ms = 0U;
        return;
    }

    if (s_screen_power.idle_ms >= timeout_ms) {
        d_screen_power_enter_off();
        return;
    }

    if (elapsed_ms >= (timeout_ms - s_screen_power.idle_ms)) {
        s_screen_power.idle_ms = timeout_ms;
        d_screen_power_enter_off();
    } else {
        s_screen_power.idle_ms += elapsed_ms;
    }
}

uint8_t d_screen_power_filter_touch(uint8_t pressed)
{
    uint8_t is_pressed = pressed ? 1U : 0U;
    uint8_t was_pressed;

    d_screen_power_ensure_init();
    was_pressed = s_screen_power.last_raw_pressed;

    if (is_pressed == 0U) {
        s_screen_power.suppress_until_release = 0U;
        s_screen_power.last_raw_pressed = 0U;
        return 0U;
    }

    if (s_screen_power.screen_off != 0U) {
        /* 息屏后只允许新的按下沿唤醒，过滤残留触摸电平和背光切换瞬间的干扰。 */
        if ((s_screen_power.wake_guard_ms == 0U) &&
            (s_screen_power.suppress_until_release == 0U) &&
            (was_pressed == 0U) &&
            (s_screen_power.touch_wake_enabled != 0U)) {
            d_screen_power_wake();
        }
        s_screen_power.suppress_until_release = 1U;
        s_screen_power.last_raw_pressed = 1U;
        return 0U;
    }

    if (s_screen_power.suppress_until_release != 0U) {
        s_screen_power.last_raw_pressed = 1U;
        return 0U;
    }

    s_screen_power.idle_ms = 0U;
    s_screen_power.last_raw_pressed = 1U;
    return 1U;
}

void d_screen_power_set_timeout_index(uint8_t index)
{
    d_screen_power_ensure_init();

    if (index >= D_SCREEN_POWER_TIMEOUT_COUNT) {
        index = D_SCREEN_POWER_DEFAULT_TIMEOUT_INDEX;
    }
    s_screen_power.timeout_index = index;
    d_screen_power_reset_idle();
}

uint8_t d_screen_power_get_timeout_index(void)
{
    d_screen_power_ensure_init();
    return s_screen_power.timeout_index;
}

uint32_t d_screen_power_get_timeout_ms(void)
{
    d_screen_power_ensure_init();
    return s_screen_power_timeout_ms[s_screen_power.timeout_index];
}

void d_screen_power_set_touch_wake_enabled(uint8_t enabled)
{
    d_screen_power_ensure_init();
    s_screen_power.touch_wake_enabled = enabled ? 1U : 0U;
    d_screen_power_reset_idle();
}

uint8_t d_screen_power_get_touch_wake_enabled(void)
{
    d_screen_power_ensure_init();
    return s_screen_power.touch_wake_enabled;
}

uint8_t d_screen_power_is_off(void)
{
    d_screen_power_ensure_init();
    return s_screen_power.screen_off;
}

void d_screen_power_clear_off(void)
{
    d_screen_power_ensure_init();
    s_screen_power.screen_off = 0U;
    s_screen_power.idle_ms = 0U;
    s_screen_power.wake_guard_ms = 0U;
}

void d_screen_power_force_off(void)
{
    d_screen_power_ensure_init();
    d_screen_power_enter_off();
}

void d_screen_power_wake(void)
{
    d_screen_power_clear_off();
#ifdef D_LCD_ENABLE
    /*
     * If backlight restore fails after clear_off, re-enter off so SW idle
     * state matches a still-dark panel (settings path uses force_off similarly).
     */
    if (d_display_set_backlight(1U) != D_OK) {
        d_screen_power_enter_off();
        return;
    }
#else
    d_screen_power_set_backlight(1U);
#endif
}
