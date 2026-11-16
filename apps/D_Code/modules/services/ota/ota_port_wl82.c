/** @file ota_port_wl82.c @brief OTA 的 RTOS 入口与可信 SDK 软件 UTC 边界。 */
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
/**
 * @brief 取得 OTA 短元数据自旋锁；完整合同见 ota_port.h。
 */
void ota_port_lock(void) { spin_lock(&s_ota_lock); }
/**
 * @brief 释放 OTA 元数据自旋锁；完整合同见 ota_port.h。
 */
void ota_port_unlock(void) { spin_unlock(&s_ota_lock); }
/**
 * @brief 读取 OTA 期限使用的单调毫秒时钟；完整合同见 ota_port.h。
 */
uint32_t ota_port_now(void) { return (uint32_t)timer_get_ms(); }
/**
 * @brief 读取当前编译配置的板型编号；完整合同见 ota_port.h。
 */
uint32_t ota_port_board(void) { return D_BOARD_TARGET; }
/**
 * @brief 借用编译配置中的默认发布地址；完整合同见 ota_port.h。
 */
const char *ota_port_default_url(void) { return D_OTA_RELEASE_URL; }

/**
 * @brief 核对 WiFi DHCP 与曾经 NTP 确认的 SDK UTC；完整合同见 ota_port.h。
 */
d_err_t ota_port_ready(void)
{
    int64_t utc;
    if (!wifi_is_on() || wifi_get_sta_connect_state() != WIFI_STA_NETWORK_STACK_DHCP_SUCC)
        return D_ERR_NETWORK;
    if (!s_ntp_confirmed && ntp_client_get_time_status() == 1U) s_ntp_confirmed = 1U;
    if (!s_ntp_confirmed) return D_ERR_NOT_READY;
    utc = (int64_t)time(NULL);
    /* SDK time_t 的范围独立受限，不能用产品手动时钟替代。 */
    return utc >= 1704067200LL && utc <= INT32_MAX ? D_OK : D_ERR_NOT_READY;
}

/**
 * @brief 由 sys_timer 独立观察 OTA 操作期限。
 * @param[in] context 定时器兼容参数，本实现不使用。
 */
static void ota_monitor(void *context)
{ (void)context; ota_service_observe_deadline(); }

/**
 * @brief 建立持续期限观察定时器并创建一次 OTA 工作任务；完整合同见 ota_port.h。
 */
d_err_t ota_port_start(void (*worker)(void *), void *context)
{
    if (!s_monitor) s_monitor = sys_timer_add_to_task("sys_timer", NULL, ota_monitor, 1000U);
    if (!s_monitor) return D_ERR_NO_MEMORY;
    return thread_fork("d_ota", 10, 2048, 0, NULL, worker, context) == OS_NO_ERR ? D_OK : D_ERR_NO_MEMORY;
}
/**
 * @brief 按 10 ms 系统节拍向上取整并延时；完整合同见 ota_port.h。
 */
void ota_port_wait(uint32_t milliseconds)
{ os_time_dly((milliseconds + 9U) / 10U); }
/**
 * @brief 调用 SDK 系统复位入口；完整合同见 ota_port.h。
 */
void ota_port_reboot(void) { system_reset(); }
/**
 * @brief 记录当前请求快照并发布相应 OTA 唤醒事件；完整合同见 ota_port.h。
 */
void ota_port_notify(ota_state_e state)
{
    ota_snapshot_t snapshot;
    ota_service_get_snapshot(&snapshot);
    /* 事件仅为唤醒提示；即使旧任务完成后已受理新请求，
     * 日志中的请求身份仍以当前采样快照明确记录。 */
    LOGI("OTA", "event=%u req=%u gen=%u state=%u error=%u detail=%d bytes=%u/%u",
        (unsigned)state, (unsigned)snapshot.request, (unsigned)snapshot.generation,
        (unsigned)snapshot.state, (unsigned)snapshot.error, snapshot.detail,
        (unsigned)snapshot.received, (unsigned)snapshot.total);
    if (state == OTA_WRITING) d_event_post(EVT_OTA_START, NULL, 0);
    else if (state == OTA_REBOOT_PENDING) d_event_post(EVT_OTA_SUCCESS, NULL, 0);
    else if (state == OTA_FAILED || state == OTA_UNCERTAIN) d_event_post(EVT_OTA_FAIL, NULL, 0);
}
#endif
