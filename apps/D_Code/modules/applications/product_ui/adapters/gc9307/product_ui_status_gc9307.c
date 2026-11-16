/** @file product_ui_status_gc9307.c @brief 汇聚可信时钟、内存 WiFi 缓存及有效电池采样，不在 UI 周期执行网络采样。 */

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

/**
 * @brief 逐字段比较时间域摘要和新的有效墙钟值。
 * @param[in] summary 借用的时间域摘要，可为 NULL。
 * @param[in] value 借用的候选日期时间，可为 NULL。
 * @return 摘要墙钟有效且日期时分秒相同返回非零，否则为零。
 */
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

/**
 * @brief 仅在能力或不可用原因变化时向时间域执行能力命令。
 * @param[in] summary 当前时间域摘要。
 * @param[in] capability 待发布的时间能力。
 * @param[in] reason 相应不可用原因。
 * @return 未变化或命令成功返回 0，失败返回 -1。
 */
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

/**
 * @brief 把候选墙钟复制到时间域命令并执行。
 * @param[in] value 调用链保证非空的候选日期时间。
 * @return 时间域接受返回 0，否则返回 -1。
 */
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

/**
 * @brief 同步时间域能力与墙钟，再发布 TIME 服务就绪或不可用。
 * @param[in] value 候选时间；reason 有效时才使用。
 * @param[in] reason 来源可用性原因，NONE 表示可用。
 * @return 全部成功返回 0，读取、命令或注册表发布失败返回 -1。
 */
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
/**
 * @brief 映射状态栏链路状态，断开及错误必须呈现关闭。
 * @param[in] state WiFi 服务连接状态。
 * @return 连接为 CONNECTED，连接中／关联为 ON，其余为 OFF。
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

/**
 * @brief 将已连接网络的百分比信号映射成零至四格。
 * @param[in] signal_percent 服务发布的信号百分比。
 * @param[in] state 连接状态，仅 CONNECTED 展示信号。
 * @return 未连接或零信号返回零，其余按 25、50、75 阈值返回一至四格。
 */
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

/**
 * @brief 借用内存缓存，给状态副本补充 WiFi 能力、有效位及图标状态。
 * @param[in,out] status 待发布的状态副本，其他字段保留。
 */
static void product_ui_status_gc9307_add_wifi(product_ui_status_t *status)
{
    wifi_service_status_t wifi;

    memset(&wifi, 0, sizeof(wifi));
    wifi_service_get_cached_status(&wifi);
    status->capability_mask |= PRODUCT_UI_STATUS_CAP_WIFI;
    status->valid_mask |= PRODUCT_UI_STATUS_VALID_WIFI;
    status->wifi_state = product_ui_status_gc9307_wifi_state(wifi.state);
    status->wifi_level = product_ui_status_gc9307_wifi_level(
        wifi.signal_strength, wifi.state);
}
#endif

/**
 * @brief 将有效 ADC 电压映射电量，并缓存最近有效采样。
 * @param[out] percent 成功时写入零至一百的电量。
 * @param[out] is_charging 成功时写入充电状态。
 * @return 得到新采样或复用有效旧采样返回 1；空输出或尚无有效采样返回 0。
 * @note ADC 启动阶段为零时保留旧有效采样；从未有效时不伪造零电量。
 */
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
    /* 启动时 ADC 未稳定可能返回零；不伪造零电量，
     * 优先保留最近有效采样，没有有效采样前不发布电量有效位。 */
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

/**
 * @brief 汇聚时间、电量及 WiFi 投影并交统一状态规范化发布。
 * @return 成功返回 0；时间同步或状态发布失败返回 -1。
 */
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

/**
 * @brief 建立状态采样节奏并尝试首次采样；后续周期补充尚未就绪的 ADC。
 */
int product_ui_status_gc9307_init(void)
{
    if (s_status_initialized) {
        return product_ui_status_gc9307_refresh();
    }
    s_status_elapsed_ms = 0U;
    s_status_fast_polls_left = PRODUCT_UI_STATUS_GC9307_FAST_POLL_COUNT;
    /* 尝试首个采样；ADC 可能尚未稳定，周期处理继续重试。 */
    (void)product_ui_status_gc9307_refresh();
    s_status_initialized = 1U;
    return 0;
}

/**
 * @brief 按快速启动轮询、普通轮询或 WiFi 版本变化刷新状态。
 */
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

        /* WiFi 状态版本变化时立即刷新，不只等待一秒轮询。 */
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

#endif /* Product UI 的 GC9307 配置。 */
