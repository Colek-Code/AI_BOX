/**
 * @file product_ui_status_gc9307.c
 * @brief GC9307 Product UI status aggregation outside the UI layer.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && \
    (D_BOARD_IS_GC9307)

#include "modules/applications/product_ui/adapters/gc9307/product_ui_gc9307_adapter.h"
#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "modules/applications/product_ui/features/time/product_ui_clock.h"
#include "asm/adc_api.h"
#include "system/app_power_manage.h"
#include "system/includes.h"
#if D_FEATURE_WIFI
#include "modules/services/wifi/wifi_service.h"
#endif
#if D_FEATURE_WIFI && defined(CONFIG_NET_ENABLE)
#include "ntp/ntp.h"
#include <time.h>
#endif
#include <string.h>

#define PRODUCT_UI_STATUS_GC9307_POLL_MS          1000U
#define PRODUCT_UI_STATUS_GC9307_FAST_POLL_MS     200U
#define PRODUCT_UI_STATUS_GC9307_FAST_POLL_COUNT  10U
#define PRODUCT_UI_STATUS_GC9307_BATTERY_FULL     420U

static uint32_t s_status_elapsed_ms;
static uint8_t s_status_initialized;
static uint8_t s_status_fast_polls_left;
static uint8_t s_battery_have_sample;
static uint8_t s_battery_last_percent;
static uint8_t s_battery_last_charging;
#if D_FEATURE_WIFI
static uint32_t s_seen_wifi_status_version = 0xFFFFFFFFU;
#endif

static uint8_t product_ui_status_gc9307_same_time(
    const product_ui_time_domain_summary_t *summary,
    const product_ui_datetime_t *value)
{
    if ((summary == NULL) || (value == NULL) ||
        !summary->wall_time_valid) {
        return 0U;
    }
    return (uint8_t)(
        (summary->wall_time.date.year == value->date.year) &&
        (summary->wall_time.date.month == value->date.month) &&
        (summary->wall_time.date.day == value->date.day) &&
        (summary->wall_time.time.hour == value->time.hour) &&
        (summary->wall_time.time.minute == value->time.minute) &&
        (summary->wall_time.time.second == value->time.second));
}

static int product_ui_status_gc9307_set_time_capability(
    const product_ui_time_domain_summary_t *summary,
    product_ui_time_capability_e capability,
    product_ui_time_unavailable_reason_e reason)
{
    product_ui_time_domain_command_t command;

    if ((summary->capability == capability) &&
        (summary->unavailable_reason == reason)) {
        return 0;
    }
    memset(&command, 0, sizeof(command));
    command.type = PRODUCT_UI_TIME_COMMAND_SET_CAPABILITY;
    command.data.capability.capability = capability;
    command.data.capability.unavailable_reason = reason;
    return product_ui_time_domain_execute(&command, NULL) ==
           PRODUCT_UI_TIME_OK ? 0 : -1;
}

static int product_ui_status_gc9307_set_wall_time(
    const product_ui_datetime_t *value)
{
    product_ui_time_domain_command_t command;

    memset(&command, 0, sizeof(command));
    command.type = PRODUCT_UI_TIME_COMMAND_SET_WALL_TIME;
    command.data.wall_time = *value;
    return product_ui_time_domain_execute(&command, NULL) ==
           PRODUCT_UI_TIME_OK ? 0 : -1;
}

static int product_ui_status_gc9307_sync_time(
    const product_ui_datetime_t *value,
    product_ui_time_unavailable_reason_e reason)
{
    product_ui_time_domain_summary_t summary;

    if (product_ui_time_domain_get_summary(&summary) != PRODUCT_UI_TIME_OK) {
        return -1;
    }
    if (reason != PRODUCT_UI_TIME_UNAVAILABLE_NONE) {
        if (product_ui_status_gc9307_set_time_capability(
                &summary, PRODUCT_UI_TIME_CAPABILITY_UNAVAILABLE,
                reason) != 0) {
            return -1;
        }
        return product_ui_service_publish(
            PRODUCT_UI_SERVICE_TIME, PRODUCT_UI_SERVICE_UNAVAILABLE,
            0U, (int32_t)reason);
    }

    if (product_ui_status_gc9307_set_time_capability(
            &summary, PRODUCT_UI_TIME_CAPABILITY_AVAILABLE,
            PRODUCT_UI_TIME_UNAVAILABLE_NONE) != 0) {
        return -1;
    }
    if ((summary.capability != PRODUCT_UI_TIME_CAPABILITY_AVAILABLE) ||
        !product_ui_status_gc9307_same_time(&summary, value)) {
        if (product_ui_status_gc9307_set_wall_time(value) != 0) {
            return -1;
        }
    }
    return product_ui_service_publish(
        PRODUCT_UI_SERVICE_TIME, PRODUCT_UI_SERVICE_READY,
        PRODUCT_UI_STATUS_VALID_TIME, 0);
}

#if D_FEATURE_WIFI
/*
 * Map wifi_service state → status-bar link enum.
 * Status bar opa: CONNECTED/ON = full, OFF/UNKNOWN = dim.
 * DISCONNECTED/ERROR must be OFF — previously they fell through to ON, so
 * the icon stayed "lit" while the service said disconnected.
 */
static product_ui_link_state_e product_ui_status_gc9307_wifi_state(
    wifi_service_conn_state_t state)
{
    switch (state) {
    case WIFI_SERVICE_CONN_CONNECTED:
        return PRODUCT_UI_LINK_CONNECTED;
    case WIFI_SERVICE_CONN_CONNECTING:
    case WIFI_SERVICE_CONN_ASSOCIATED:
        return PRODUCT_UI_LINK_ON;
    case WIFI_SERVICE_CONN_IDLE:
    case WIFI_SERVICE_CONN_DISCONNECTED:
    case WIFI_SERVICE_CONN_ERROR:
    default:
        return PRODUCT_UI_LINK_OFF;
    }
}

/* wifi_service signal_strength is 0..100; status model wifi_level is 0..4. */
static uint8_t product_ui_status_gc9307_wifi_level(uint8_t signal_percent,
                                                   wifi_service_conn_state_t state)
{
    if (state != WIFI_SERVICE_CONN_CONNECTED) {
        return 0U;
    }
    if (signal_percent == 0U) {
        return 0U;
    }
    if (signal_percent < 25U) {
        return 1U;
    }
    if (signal_percent < 50U) {
        return 2U;
    }
    if (signal_percent < 75U) {
        return 3U;
    }
    return 4U;
}

static void product_ui_status_gc9307_add_wifi(product_ui_status_t *status)
{
    wifi_service_status_t wifi;

    memset(&wifi, 0, sizeof(wifi));
    wifi_service_get_status(&wifi);
    status->capability_mask |= PRODUCT_UI_STATUS_CAP_WIFI;
    status->valid_mask |= PRODUCT_UI_STATUS_VALID_WIFI;
    status->wifi_state = product_ui_status_gc9307_wifi_state(wifi.state);
    status->wifi_level = product_ui_status_gc9307_wifi_level(
        wifi.signal_strength, wifi.state);
}
#endif

static uint8_t product_ui_status_gc9307_read_battery(
    uint8_t *percent, uint8_t *is_charging)
{
    u32 adc_value;
    u32 battery_value;
    u32 battery_percent;

    if ((percent == NULL) || (is_charging == NULL)) {
        return 0U;
    }
    adc_value = adc_get_voltage(AD_CH_VBAT);
    /*
     * Boot race: AD_CH_VBAT can return 0 until ADC settles. Do not publish a
     * fake 0% shell — keep last good sample so the bar is not empty until
     * the first real reading (user saw empty-on-boot, OK after settings).
     */
    if (adc_value == 0U) {
        if (s_battery_have_sample) {
            *percent = s_battery_last_percent;
            *is_charging = s_battery_last_charging;
            return 1U;
        }
        return 0U;
    }
    battery_value = adc_value * 4U / 10U;
    if (battery_value <= LOW_POWER_OFF_VAL) {
        battery_percent = 0U;
    } else if (battery_value >= PRODUCT_UI_STATUS_GC9307_BATTERY_FULL) {
        battery_percent = 100U;
    } else {
        battery_percent =
            ((battery_value - LOW_POWER_OFF_VAL) * 100U) /
            (PRODUCT_UI_STATUS_GC9307_BATTERY_FULL - LOW_POWER_OFF_VAL);
    }
    *percent = (uint8_t)battery_percent;
    *is_charging = sys_power_is_charging() ? 1U : 0U;
    s_battery_last_percent = *percent;
    s_battery_last_charging = *is_charging;
    s_battery_have_sample = 1U;
    return 1U;
}

static int product_ui_status_gc9307_refresh(void)
{
    product_ui_datetime_t wall_time = {0};
    product_ui_status_t status;
    product_ui_time_unavailable_reason_e time_reason;

    memset(&status, 0, sizeof(status));
    status.capability_mask = PRODUCT_UI_STATUS_CAP_TIME |
                             PRODUCT_UI_STATUS_CAP_BATTERY;
    time_reason = product_ui_clock_read(&wall_time);
    if (product_ui_status_gc9307_sync_time(&wall_time, time_reason) != 0) {
        return -1;
    }
    if (time_reason == PRODUCT_UI_TIME_UNAVAILABLE_NONE) {
        status.valid_mask |= PRODUCT_UI_STATUS_VALID_TIME;
        status.hour = wall_time.time.hour;
        status.minute = wall_time.time.minute;
    }
    if (product_ui_status_gc9307_read_battery(
            &status.battery_percent, &status.is_charging)) {
        status.valid_mask |= PRODUCT_UI_STATUS_VALID_BATTERY;
    }
#if D_FEATURE_WIFI
    product_ui_status_gc9307_add_wifi(&status);
#endif
    return product_ui_status_update(&status) < 0 ? -1 : 0;
}

int product_ui_status_gc9307_init(void)
{
    if (s_status_initialized) {
        return product_ui_status_gc9307_refresh();
    }
    s_status_elapsed_ms = 0U;
    s_status_fast_polls_left = PRODUCT_UI_STATUS_GC9307_FAST_POLL_COUNT;
    /* Best-effort first sample; ADC may still be cold — process() retries. */
    (void)product_ui_status_gc9307_refresh();
    s_status_initialized = 1U;
    return 0;
}

void product_ui_status_gc9307_process(uint32_t elapsed_ms)
{
    uint8_t due = 0U;
    uint32_t poll_ms;

    if (!s_status_initialized) {
        return;
    }
#if D_FEATURE_WIFI
    {
        uint32_t wifi_ver = wifi_service_get_status_version();

        /* Refresh as soon as wifi_service state changes (not only 1s poll). */
        if (wifi_ver != s_seen_wifi_status_version) {
            s_seen_wifi_status_version = wifi_ver;
            due = 1U;
        }
    }
#endif
    poll_ms = (s_status_fast_polls_left > 0U) ?
              PRODUCT_UI_STATUS_GC9307_FAST_POLL_MS :
              PRODUCT_UI_STATUS_GC9307_POLL_MS;
    if ((elapsed_ms < poll_ms) &&
        (s_status_elapsed_ms < (poll_ms - elapsed_ms))) {
        s_status_elapsed_ms += elapsed_ms;
    } else {
        s_status_elapsed_ms = 0U;
        due = 1U;
    }
    if (due != 0U) {
        if (s_status_fast_polls_left > 0U) {
            s_status_fast_polls_left--;
        }
        (void)product_ui_status_gc9307_refresh();
    }
}

#endif /* Product UI GC9307 profile */
