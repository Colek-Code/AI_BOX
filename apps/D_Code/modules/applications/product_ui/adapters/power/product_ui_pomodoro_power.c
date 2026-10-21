/** @file product_ui_pomodoro_power.c @brief Drain completed data and cue audio before shutdown. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/services/power/power_ctrl_pomodoro.h"
#include "modules/services/audio/d_local_prompt.h"
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_service.h"
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_port.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_service.h"

/* Existing Product reminder provider now drains both owners before shared audio. */
void power_ctrl_pomodoro_begin(void)
{ product_ui_alarm_quiesce_begin(); product_ui_pomodoro_quiesce_begin(); }
void power_ctrl_pomodoro_cancel(void)
{ product_ui_pomodoro_quiesce_cancel(); product_ui_alarm_quiesce_cancel(); }
d_err_t power_ctrl_pomodoro_wait_idle(uint32_t timeout_ms)
{
    uint32_t started = product_ui_pomodoro_clock_ms();
    uint32_t elapsed;
    d_err_t result = product_ui_alarm_wait_idle(timeout_ms);
    if (result != D_OK) return result;
    elapsed = product_ui_pomodoro_clock_ms() - started;
    result = d_local_prompt_wait_stopped(elapsed >= timeout_ms ? 0U : timeout_ms - elapsed);
    if (result != D_OK) return result;
    elapsed = product_ui_pomodoro_clock_ms() - started;
    return product_ui_pomodoro_wait_idle(elapsed >= timeout_ms ? 0U : timeout_ms - elapsed);
}
#endif
