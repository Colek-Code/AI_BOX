/** @file product_ui_clock_gc9307.c @brief NTP-confirmed SDK software UTC; RTC failure cannot invent today. */
#include "config/app_config.h"
#include <stddef.h>
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && D_BOARD_IS_GC9307
#include "modules/applications/product_ui/features/time/product_ui_clock_port.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include "system/includes.h"
#if D_FEATURE_WIFI && defined(CONFIG_NET_ENABLE)
#include "ntp/ntp.h"
#include <time.h>
static volatile uint8_t s_clock_ntp_confirmed;
#endif

uint32_t product_ui_clock_port_ms(void) { return (uint32_t)timer_get_ms(); }
uint16_t product_ui_clock_port_start(void (*callback)(void *))
{ return sys_timer_add_to_task("sys_timer", NULL, callback, 100U); }
void product_ui_clock_port_stop(uint16_t timer) { if (timer) sys_timer_del(timer); }

product_ui_time_unavailable_reason_e product_ui_clock_port_network(int64_t *value)
{
    if (value == NULL) return PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID;
#if D_FEATURE_WIFI && defined(CONFIG_NET_ENABLE)
    int64_t utc_seconds;
    product_ui_datetime_t date;
    /* Monotonic latch: neither a UI re-entry nor network loss resets boot trust. */
    if (!s_clock_ntp_confirmed && ntp_client_get_time_status() == 1U) s_clock_ntp_confirmed = 1U;
    if (!s_clock_ntp_confirmed) return PRODUCT_UI_TIME_UNAVAILABLE_NOT_READY;
    utc_seconds = (int64_t)time(NULL);
    if (utc_seconds < 0 || utc_seconds > INT32_MAX ||
        product_ui_time_utc_to_beijing(utc_seconds, &date) != PRODUCT_UI_TIME_OK ||
        date.date.year < 2024U ||
        product_ui_time_domain_validate_datetime(&date) != PRODUCT_UI_TIME_OK)
        return PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID;
    *value = utc_seconds;
    return PRODUCT_UI_TIME_UNAVAILABLE_NONE;
#else
    return PRODUCT_UI_TIME_UNAVAILABLE_NOT_SUPPORTED;
#endif
}
#endif
