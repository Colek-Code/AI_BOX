/** @file ota_port_wl82.c @brief RTOS and trusted SDK software UTC boundaries. */
#include "config/app_config.h"
#if D_FEATURE_OTA
#include "ota_port.h"
#include "system/includes.h"
#include "wifi/wifi_connect.h"
#include "ntp/ntp.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include <time.h>

static DEFINE_SPINLOCK(s_ota_lock);
static uint8_t s_ntp_confirmed;
static uint16_t s_monitor;
void ota_port_lock(void) { spin_lock(&s_ota_lock); }
void ota_port_unlock(void) { spin_unlock(&s_ota_lock); }
uint32_t ota_port_now(void) { return (uint32_t)timer_get_ms(); }
uint32_t ota_port_board(void) { return D_BOARD_TARGET; }
const char *ota_port_default_url(void) { return D_OTA_RELEASE_URL; }

d_err_t ota_port_ready(void)
{
    int64_t utc;
    if (!wifi_is_on() || wifi_get_sta_connect_state() != WIFI_STA_NETWORK_STACK_DHCP_SUCC)
        return D_ERR_NETWORK;
    if (!s_ntp_confirmed && ntp_client_get_time_status() == 1U) s_ntp_confirmed = 1U;
    if (!s_ntp_confirmed) return D_ERR_NOT_READY;
    utc = (int64_t)time(NULL);
    /* SDK time_t remains bounded separately from the Product manual clock. */
    return utc >= 1704067200LL && utc <= INT32_MAX ? D_OK : D_ERR_NOT_READY;
}

static void ota_monitor(void *context)
{ (void)context; ota_service_observe_deadline(); }

d_err_t ota_port_start(void (*worker)(void *), void *context)
{
    if (!s_monitor) s_monitor = sys_timer_add_to_task("sys_timer", NULL, ota_monitor, 1000U);
    if (!s_monitor) return D_ERR_NO_MEMORY;
    return thread_fork("d_ota", 10, 2048, 0, NULL, worker, context) == OS_NO_ERR ? D_OK : D_ERR_NO_MEMORY;
}
void ota_port_wait(uint32_t milliseconds)
{ os_time_dly((milliseconds + 9U) / 10U); }
void ota_port_reboot(void) { system_reset(); }
void ota_port_notify(ota_state_e state)
{
    ota_snapshot_t snapshot;
    ota_service_get_snapshot(&snapshot);
    /* Events are wake-up hints. The sampled request identity is explicit even
     * if another request was accepted after a previous worker completed. */
    LOGI("OTA", "event=%u req=%u gen=%u state=%u error=%u detail=%d bytes=%u/%u",
        (unsigned)state, (unsigned)snapshot.request, (unsigned)snapshot.generation,
        (unsigned)snapshot.state, (unsigned)snapshot.error, snapshot.detail,
        (unsigned)snapshot.received, (unsigned)snapshot.total);
    if (state == OTA_WRITING) d_event_post(EVT_OTA_START, NULL, 0);
    else if (state == OTA_REBOOT_PENDING) d_event_post(EVT_OTA_SUCCESS, NULL, 0);
    else if (state == OTA_FAILED || state == OTA_UNCERTAIN) d_event_post(EVT_OTA_FAIL, NULL, 0);
}
#endif
