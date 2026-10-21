/**
 * @file    wifi_service.c
 * @brief   WiFi 服务模块
 */

#include "config/app_config.h"

#if D_FEATURE_WIFI

#include "wifi_service.h"
#include "system/includes.h"
#include "wifi/wifi_connect.h"
#include "lwip.h"
#include "dhcp_srv/dhcp_srv.h"
#include "event/net_event.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "framework/d_event_payloads.h"
#include "syscfg/syscfg_id.h"
#include <string.h>

#ifdef CONFIG_MBEDTLS_ENABLE
#include "ntp/ntp.h"
#include "framework/d_crypto.h"
#include <time.h>
static void wifi_start_ntp_sync(void);
#endif

#if D_FEATURE_BLE_PROVISION
#include "modules/services/ble_provisioning/ble_service.h"
#endif

#define TAG "WIFI_SVC"

//*********************************************************************************//
//                              模式定义                                            //
//*********************************************************************************//

#define WIFI_RUN_MODE_AP   1
#define WIFI_RUN_MODE_STA  2
#ifdef D_WIFI_AP_MODE
#define WIFI_RUN_MODE      WIFI_RUN_MODE_AP
#else
#define WIFI_RUN_MODE      WIFI_RUN_MODE_STA
#endif

/* WiFi 凭证持久化 VM ID（用户区 1~59；16 为 Qwen SDK 保留，
 * 24/25 已在 device_credentials.h 分配表预留给 WiFi） */
#define WIFI_VM_SSID        24
#define WIFI_VM_PWD         25
#define WIFI_SSID_MAXLEN    33
#define WIFI_PWD_MAXLEN     65
#define WIFI_SCAN_TIMEOUT_MS 8000U
#define WIFI_SCAN_RETRY_INTERVAL_MS 1000U
#define WIFI_SCAN_SIGNAL_MAX 100U
#define WIFI_SCAN_CHANNEL_MAX 14U
#define WIFI_SCAN_RSSI_STRONG_DBM (-50)
#define WIFI_SCAN_RSSI_WEAK_DBM   (-90)
#define WIFI_SDK_PMK_CACHE_LEN     36U

/* BLE/UI 暂存：待 WiFi 连接成功后再持久化，避免存入错误密码 */
static char s_pending_ssid[WIFI_SSID_MAXLEN];
static char s_pending_pwd[WIFI_PWD_MAXLEN];
/*
 * Pending generation: bumps whenever pending is set or cleared so async
 * DHCP/EVT_WIFI_CONNECTED cannot write an older password for the same SSID.
 */
static uint32_t s_pending_gen = 1U;
/* SDK STA connect is async by default; keep arguments alive after caller returns. */
static char s_connect_ssid[WIFI_SSID_MAXLEN];
static char s_connect_pwd[WIFI_PWD_MAXLEN];
static wifi_service_scan_result_t s_scan_results[WIFI_SERVICE_SCAN_MAX_RESULTS];
static uint8_t s_scan_result_count = 0;
static volatile uint8_t s_scan_completed = 0;
static wifi_service_scan_state_t s_scan_state = WIFI_SERVICE_SCAN_IDLE;
static uint32_t s_scan_elapsed_ms = 0;
static uint32_t s_scan_version = 0;
static wifi_service_status_t s_wifi_status = {
    .state = WIFI_SERVICE_CONN_IDLE,
    .error = WIFI_SERVICE_ERR_NONE,
};
static uint32_t s_wifi_status_version = 0;
/* Soft watchdog if SDK stays in CONNECTING/ASSOCIATED without terminal event. */
#define WIFI_CONNECT_STALL_MS  45000U
static uint32_t s_connect_watch_ms = 0;
/* Bump on each start/abort so late SDK events cannot apply to a newer attempt. */
static uint32_t s_connect_attempt_id = 0;
static uint32_t s_active_connect_attempt = 0;
/*
 * Attempt token bound at wifi_enter_sta_mode (not at callback entry).
 * Fail/DHCP side-effects use this so a late ASSOC_FAIL cannot adopt a
 * newly-armed attempt id snapshotted at event entry (SDK gives no gen).
 * 0 = no committed enter (arm-to-enter gap drops terminal fails).
 */
static uint32_t s_enter_attempt = 0;
/*
 * Credential generation: bumps on forget/clear AND on each new pending
 * password so same-SSID password change invalidates in-flight async saves.
 */
static uint32_t s_cred_epoch = 1U;
/* Shared state lock: SDK/wifi_task/LVGL/BLE may touch status+attempt+pending. */
static OS_MUTEX s_wifi_svc_mutex;
static uint8_t s_wifi_svc_mutex_ready = 0;
/*
 * Serializes connect abort/claim/enter AND forget/disconnect so they
 * cannot interleave (wrong enter token, or reconnect in forget gap).
 * Lock order: connect_seq -> cred_txn -> svc (never reverse).
 */
static OS_MUTEX s_wifi_connect_seq_mutex;
static uint8_t s_wifi_connect_seq_ready = 0;
/* Product coordination may freeze configuration without stopping the link.
 * Writers hold connect_seq; readers/writers of this flag hold svc as well. */
static uint8_t s_configuration_held;
/*
 * Single owner for VM 24/25 load/save/clear. Old async writers must not
 * wipe after a newer connect has already committed credentials.
 */
static OS_MUTEX s_wifi_cred_txn_mutex;
static uint8_t s_wifi_cred_txn_ready = 0;
/*
 * Private switch SM (public status stays CONNECTING). Wait for real old
 * STA exit before enter_sta_mode(new) — do not count-suppress disconnects.
 */
typedef enum {
    WIFI_SWITCH_NONE = 0,
    WIFI_SWITCH_WAIT_OLD_DISCONNECT,
} wifi_switch_phase_t;
static wifi_switch_phase_t s_switch_phase = WIFI_SWITCH_NONE;
static char s_switch_ssid[WIFI_SSID_MAXLEN];
static char s_switch_pwd[WIFI_PWD_MAXLEN];
static uint32_t s_switch_arm_token = 0U;
static uint32_t s_switch_wait_ms = 0U;
#define WIFI_SWITCH_WAIT_MS  12000U

static void wifi_svc_lock(void)
{
    if (s_wifi_svc_mutex_ready != 0U) {
        os_mutex_pend(&s_wifi_svc_mutex, 0);
    }
}

static void wifi_svc_unlock(void)
{
    if (s_wifi_svc_mutex_ready != 0U) {
        os_mutex_post(&s_wifi_svc_mutex);
    }
}

static void wifi_svc_connect_seq_lock(void)
{
    if (s_wifi_connect_seq_ready != 0U) {
        os_mutex_pend(&s_wifi_connect_seq_mutex, 0);
    }
}

static void wifi_svc_connect_seq_unlock(void)
{
    if (s_wifi_connect_seq_ready != 0U) {
        os_mutex_post(&s_wifi_connect_seq_mutex);
    }
}

uint8_t wifi_service_configuration_is_held(void)
{
    uint8_t held;
    wifi_svc_lock();
    held = s_configuration_held;
    wifi_svc_unlock();
    return held;
}

static d_err_t wifi_svc_configuration_lock(void)
{
    wifi_svc_connect_seq_lock();
    if (wifi_service_configuration_is_held()) {
        wifi_svc_connect_seq_unlock();
        return D_ERR_BUSY;
    }
    return D_OK;
}

d_err_t wifi_service_configuration_hold_begin(void)
{
    d_err_t result = D_ERR_BUSY;
    if (!s_wifi_connect_seq_ready || !s_wifi_cred_txn_ready || !s_wifi_svc_mutex_ready)
        return D_ERR_NOT_READY;
    /* Do not wait for an SDK call or VM write during maintenance admission. */
    if (os_mutex_accept(&s_wifi_connect_seq_mutex) != 0) return D_ERR_BUSY;
    if (os_mutex_accept(&s_wifi_cred_txn_mutex) != 0) {
        wifi_svc_connect_seq_unlock();
        return D_ERR_BUSY;
    }
    wifi_svc_lock();
    /* CONNECTED is published after DHCP credential persistence. Pending
     * credentials also exclude a failed-save retry that could still write VM. */
    if (!s_configuration_held && s_switch_phase == WIFI_SWITCH_NONE &&
        s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED && !s_pending_ssid[0]) {
        s_configuration_held = 1U;
        result = D_OK;
    }
    wifi_svc_unlock();
    os_mutex_post(&s_wifi_cred_txn_mutex);
    wifi_svc_connect_seq_unlock();
    return result;
}

void wifi_service_configuration_hold_end(void)
{
    wifi_svc_connect_seq_lock();
    wifi_svc_lock();
    s_configuration_held = 0U;
    wifi_svc_unlock();
    wifi_svc_connect_seq_unlock();
}

static void wifi_svc_cred_txn_lock(void)
{
    if (s_wifi_cred_txn_ready != 0U) {
        os_mutex_pend(&s_wifi_cred_txn_mutex, 0);
    }
}

static void wifi_svc_cred_txn_unlock(void)
{
    if (s_wifi_cred_txn_ready != 0U) {
        os_mutex_post(&s_wifi_cred_txn_mutex);
    }
}

/* Caller holds svc lock. */
static void wifi_svc_switch_cancel_locked(void)
{
    s_switch_phase = WIFI_SWITCH_NONE;
    s_switch_arm_token = 0U;
    s_switch_wait_ms = 0U;
    memset(s_switch_ssid, 0, sizeof(s_switch_ssid));
    memset(s_switch_pwd, 0, sizeof(s_switch_pwd));
}

/* True only while an armed attempt is still live (not invalidated). */
static uint8_t wifi_svc_attempt_live_locked(uint32_t token)
{
    return ((token != 0U) &&
            (token == s_active_connect_attempt) &&
            (token == s_connect_attempt_id)) ? 1U : 0U;
}

/* syscfg_read/write: success only when return value > 0 (syscfg_id.h). */
static int wifi_syscfg_ok(int ret)
{
    return (ret > 0) ? 1 : 0;
}

static void wifi_svc_bump_cred_epoch_locked(void)
{
    s_cred_epoch++;
    if (s_cred_epoch == 0U) {
        s_cred_epoch = 1U;
    }
}

static void wifi_svc_bump_pending_gen_locked(void)
{
    s_pending_gen++;
    if (s_pending_gen == 0U) {
        s_pending_gen = 1U;
    }
}

/* Invalidate live attempt + enter binding. Caller holds lock. */
static void wifi_svc_invalidate_attempt_locked(void)
{
    s_active_connect_attempt = 0U;
    s_enter_attempt = 0U;
    s_connect_attempt_id++;
    if (s_connect_attempt_id == 0U) {
        s_connect_attempt_id = 1U;
    }
    s_connect_watch_ms = 0U;
}

/*
 * True if copied pending snapshot still matches live pending (ssid+pwd+gen)
 * and cred_epoch unchanged. Used before/after wifi_cred_save.
 */
static uint8_t wifi_svc_pending_snapshot_live_locked(
    uint32_t cred_epoch, uint32_t pending_gen,
    const char *ssid, const char *pwd)
{
    if ((cred_epoch != s_cred_epoch) || (pending_gen != s_pending_gen)) {
        return 0U;
    }
    if ((ssid == NULL) || (ssid[0] == '\0') || (s_pending_ssid[0] == '\0')) {
        return 0U;
    }
    if (strncmp(s_pending_ssid, ssid, WIFI_SSID_MAXLEN) != 0) {
        return 0U;
    }
    if (strncmp(s_pending_pwd, (pwd != NULL) ? pwd : "", WIFI_PWD_MAXLEN) != 0) {
        return 0U;
    }
    return 1U;
}

/* 1=match; 0=hard mismatch. Empty expected is NOT a match (reject forget race). */
static uint8_t wifi_svc_ssid_matches_expected(const char *expected)
{
    struct wifi_mode_info info;

    if ((expected == NULL) || (expected[0] == '\0')) {
        return 0U;
    }
    memset(&info, 0, sizeof(info));
    info.mode = STA_MODE;
    wifi_get_mode_cur_info(&info);
    if ((info.ssid == NULL) || (info.ssid[0] == '\0')) {
        /* Unknown — accept; hard reject only on explicit mismatch. */
        return 1U;
    }
    return (strncmp(expected, info.ssid, WIFI_SSID_MAXLEN) == 0) ? 1U : 0U;
}

static void wifi_scan_set_state(wifi_service_scan_state_t state)
{
    wifi_svc_lock();
    if (s_scan_state != state) {
        s_scan_state = state;
        s_scan_version++;
    }
    wifi_svc_unlock();
}

static uint8_t wifi_scan_utf8_seq_len(uint8_t ch)
{
    if (ch < 0x80U) {
        return 1U;
    }
    if ((ch >= 0xC2U) && (ch <= 0xDFU)) {
        return 2U;
    }
    if ((ch >= 0xE0U) && (ch <= 0xEFU)) {
        return 3U;
    }
    if ((ch >= 0xF0U) && (ch <= 0xF4U)) {
        return 4U;
    }
    return 0U;
}

static uint8_t wifi_scan_utf8_tail_valid(const char *src, uint8_t src_len,
                                         uint8_t pos, uint8_t seq_len)
{
    uint8_t lead;
    uint8_t second;
    uint8_t i;

    if ((src == NULL) || (pos >= src_len) || ((uint8_t)(src_len - pos) < seq_len)) {
        return 0U;
    }
    lead = (uint8_t)src[pos];
    second = (uint8_t)src[pos + 1U];
    if (((lead == 0xE0U) && (second < 0xA0U)) ||
        ((lead == 0xEDU) && (second > 0x9FU)) ||
        ((lead == 0xF0U) && (second < 0x90U)) ||
        ((lead == 0xF4U) && (second > 0x8FU))) {
        return 0U;
    }

    for (i = 1U; i < seq_len; i++) {
        uint8_t ch = (uint8_t)src[pos + i];
        if ((ch < 0x80U) || (ch > 0xBFU)) {
            return 0U;
        }
    }
    return 1U;
}

static void wifi_scan_copy_ssid(char *dst, uint8_t dst_size,
                                const char *src, uint8_t src_len)
{
    uint8_t in_pos = 0;
    uint8_t out_pos = 0;

    if ((dst == NULL) || (dst_size == 0U) || (src == NULL)) {
        return;
    }

    /* SSID is raw bytes; keep valid UTF-8 and hide bytes LVGL cannot decode. */
    memset(dst, 0, dst_size);
    while ((in_pos < src_len) && (out_pos < (uint8_t)(dst_size - 1U))) {
        uint8_t ch = (uint8_t)src[in_pos];
        uint8_t seq_len = wifi_scan_utf8_seq_len(ch);

        if ((seq_len == 1U) && (ch >= 0x20U) && (ch <= 0x7EU)) {
            dst[out_pos++] = (char)ch;
            in_pos++;
        } else if ((seq_len > 1U) && wifi_scan_utf8_tail_valid(src, src_len, in_pos, seq_len)
                   && ((uint8_t)(out_pos + seq_len) < dst_size)) {
            memcpy(&dst[out_pos], &src[in_pos], seq_len);
            out_pos = (uint8_t)(out_pos + seq_len);
            in_pos = (uint8_t)(in_pos + seq_len);
        } else {
            dst[out_pos++] = '?';
            in_pos++;
        }
    }
}

static uint8_t wifi_scan_rssi_to_signal(int8_t rssi)
{
    int rssi_dbm = (int)rssi;

    if (rssi_dbm >= WIFI_SCAN_RSSI_STRONG_DBM) {
        return WIFI_SCAN_SIGNAL_MAX;
    }
    if (rssi_dbm <= WIFI_SCAN_RSSI_WEAK_DBM) {
        return 0U;
    }

    return (uint8_t)(((rssi_dbm - WIFI_SCAN_RSSI_WEAK_DBM) * WIFI_SCAN_SIGNAL_MAX) /
                     (WIFI_SCAN_RSSI_STRONG_DBM - WIFI_SCAN_RSSI_WEAK_DBM));
}

static uint8_t wifi_scan_clamp_channel(char channel)
{
    if ((channel <= 0) || ((uint8_t)channel > WIFI_SCAN_CHANNEL_MAX)) {
        return 0U;
    }
    return (uint8_t)channel;
}

static int wifi_scan_better(const wifi_service_scan_result_t *left,
                            const wifi_service_scan_result_t *right)
{
    if (left->signal_strength != right->signal_strength) {
        return left->signal_strength > right->signal_strength;
    }
    return left->rssi > right->rssi;
}

static void wifi_scan_cache_store(const struct wifi_scan_ssid_info *src)
{
    wifi_service_scan_result_t item;
    uint8_t len;
    uint8_t i;

    if ((src == NULL) || (src->ssid_len == 0U)) {
        return;
    }
    memset(&item, 0, sizeof(item));
    len = src->ssid_len;
    if (len >= WIFI_SERVICE_SCAN_SSID_MAX) {
        len = WIFI_SERVICE_SCAN_SSID_MAX - 1U;
    }
    wifi_scan_copy_ssid(item.ssid, WIFI_SERVICE_SCAN_SSID_MAX, src->ssid, len);
    item.rssi = (int8_t)src->rssi;
    /* SDK SignalStrength 实测不单调，UI 百分比统一由 RSSI dBm 映射。 */
    item.signal_strength = wifi_scan_rssi_to_signal(item.rssi);
    item.channel = wifi_scan_clamp_channel(src->channel_number);
    item.auth_mode = (uint8_t)src->auth_mode;

    for (i = 0; i < s_scan_result_count; i++) {
        if (strncmp(s_scan_results[i].ssid, item.ssid, WIFI_SERVICE_SCAN_SSID_MAX) == 0) {
            if (wifi_scan_better(&item, &s_scan_results[i])) {
                s_scan_results[i] = item;
            }
            return;
        }
    }
    if (s_scan_result_count < WIFI_SERVICE_SCAN_MAX_RESULTS) {
        s_scan_results[s_scan_result_count++] = item;
    } else {
        uint8_t weakest = 0;
        for (i = 1; i < s_scan_result_count; i++) {
            if (wifi_scan_better(&s_scan_results[weakest], &s_scan_results[i])) {
                weakest = i;
            }
        }
        if (wifi_scan_better(&item, &s_scan_results[weakest])) {
            s_scan_results[weakest] = item;
        }
    }
}

static void wifi_scan_cache_sort(void)
{
    uint8_t i;
    uint8_t j;

    for (i = 0; i < s_scan_result_count; i++) {
        for (j = (uint8_t)(i + 1U); j < s_scan_result_count; j++) {
            if (wifi_scan_better(&s_scan_results[j], &s_scan_results[i])) {
                wifi_service_scan_result_t swap_item = s_scan_results[i];
                s_scan_results[i] = s_scan_results[j];
                s_scan_results[j] = swap_item;
            }
        }
    }
}

static void wifi_scan_cache_finish(void)
{
    struct wifi_scan_ssid_info *ssid_info;
    u32 ssid_count = 0;
    u32 i;

    ssid_info = wifi_get_scan_result(&ssid_count);
    memset(s_scan_results, 0, sizeof(s_scan_results));
    s_scan_result_count = 0;
    if (ssid_info != NULL) {
        for (i = 0; i < ssid_count; i++) {
            wifi_scan_cache_store(&ssid_info[i]);
        }
        wifi_scan_cache_sort();
    }

    /* SDK 要求取完扫描结果后清理内部缓存，再释放返回 buffer。 */
    wifi_clear_scan_result();
    if (ssid_info != NULL) {
        free(ssid_info);
    }
}

static d_err_t wifi_scan_request_start(void)
{
    s_scan_completed = 0;
    s_scan_elapsed_ms = 0;

    if (!wifi_is_on()) {
        wifi_scan_set_state(WIFI_SERVICE_SCAN_NOT_READY);
        return D_ERR_NOT_READY;
    }

    if (wifi_scan_req() != 0) {
        wifi_scan_set_state(WIFI_SERVICE_SCAN_BUSY);
        return D_ERR_BUSY;
    }

    wifi_scan_set_state(WIFI_SERVICE_SCAN_SCANNING);
    return D_OK;
}

static void wifi_scan_complete_current(void)
{
    wifi_scan_cache_finish();
    s_scan_completed = 0;
    s_scan_elapsed_ms = 0;
    wifi_scan_set_state(WIFI_SERVICE_SCAN_DONE);
}

/* Raw VM ops — caller must hold s_wifi_cred_txn_mutex (or be nested under it). */
static int wifi_cred_load_raw(char *ssid, int ssid_sz, char *pwd, int pwd_sz)
{
    int r_ssid;
    int r_pwd;

    memset(ssid, 0, ssid_sz);
    memset(pwd, 0, pwd_sz);
    r_ssid = syscfg_read(WIFI_VM_SSID, ssid, (u16)(ssid_sz - 1));
    r_pwd = syscfg_read(WIFI_VM_PWD, pwd, (u16)(pwd_sz - 1));
    if ((!wifi_syscfg_ok(r_ssid)) || (ssid[0] == '\0') ||
        (ssid[0] == (char)0xFF)) {
        ssid[0] = '\0';
        pwd[0] = '\0';
        return 0;
    }
    if (!wifi_syscfg_ok(r_pwd)) {
        LOGW(TAG, "WiFi cred load: pwd slot read failed (%d)", r_pwd);
        ssid[0] = '\0';
        pwd[0] = '\0';
        return 0;
    }
    return 1;
}

static int wifi_cred_save_raw(const char *ssid, const char *pwd)
{
    char old_ssid[WIFI_SSID_MAXLEN];
    char old_pwd[WIFI_PWD_MAXLEN];
    char empty_ssid[WIFI_SSID_MAXLEN - 1];
    int had_old;
    int r_ssid;
    int r_pwd;
    u16 ssid_len;
    u16 pwd_len;

    if ((ssid == NULL) || (ssid[0] == '\0')) {
        return -1;
    }
    memset(old_ssid, 0, sizeof(old_ssid));
    memset(old_pwd, 0, sizeof(old_pwd));
    memset(empty_ssid, 0, sizeof(empty_ssid));
    had_old = wifi_cred_load_raw(old_ssid, sizeof(old_ssid), old_pwd,
                                 sizeof(old_pwd));

    ssid_len = (u16)(strlen(ssid) + 1U);
    pwd_len = (u16)((pwd != NULL) ? (strlen(pwd) + 1U) : 1U);
    r_ssid = syscfg_write(WIFI_VM_SSID, (void *)ssid, ssid_len);
    if (!wifi_syscfg_ok(r_ssid)) {
        LOGE(TAG, "WiFi 凭证写入失败: ssid_ret=%d", r_ssid);
        return -1;
    }
    r_pwd = syscfg_write(WIFI_VM_PWD, (void *)(pwd ? pwd : ""), pwd_len);
    if (!wifi_syscfg_ok(r_pwd)) {
        LOGE(TAG, "WiFi 凭证写入失败: pwd_ret=%d, rollback ssid", r_pwd);
        if (had_old) {
            (void)syscfg_write(WIFI_VM_SSID, old_ssid,
                               (u16)(strlen(old_ssid) + 1U));
            (void)syscfg_write(WIFI_VM_PWD, old_pwd,
                               (u16)(strlen(old_pwd) + 1U));
        } else {
            (void)syscfg_write(WIFI_VM_SSID, empty_ssid,
                               (u16)sizeof(empty_ssid));
        }
        return -1;
    }
    LOGI(TAG, "WiFi 凭证已保存: ssid=%s", ssid);
    return 0;
}

static int wifi_cred_clear_raw(void)
{
    char empty_ssid[WIFI_SSID_MAXLEN - 1] = {0};
    char empty_pwd[WIFI_PWD_MAXLEN - 1] = {0};
    char old_ssid[WIFI_SSID_MAXLEN];
    char old_pwd[WIFI_PWD_MAXLEN];
    int had_old;
    int r_ssid;
    int r_pwd;

    memset(old_ssid, 0, sizeof(old_ssid));
    memset(old_pwd, 0, sizeof(old_pwd));
    had_old = wifi_cred_load_raw(old_ssid, sizeof(old_ssid), old_pwd,
                                 sizeof(old_pwd));

    r_ssid = syscfg_write(WIFI_VM_SSID, empty_ssid, (u16)sizeof(empty_ssid));
    if (!wifi_syscfg_ok(r_ssid)) {
        LOGE(TAG, "WiFi 凭证清空失败: ssid_ret=%d", r_ssid);
        return -1;
    }
    r_pwd = syscfg_write(WIFI_VM_PWD, empty_pwd, (u16)sizeof(empty_pwd));
    if (!wifi_syscfg_ok(r_pwd)) {
        LOGE(TAG, "WiFi 凭证清空失败: pwd_ret=%d, rollback ssid", r_pwd);
        if (had_old) {
            (void)syscfg_write(WIFI_VM_SSID, old_ssid,
                               (u16)(strlen(old_ssid) + 1U));
            (void)syscfg_write(WIFI_VM_PWD, old_pwd,
                               (u16)(strlen(old_pwd) + 1U));
        }
        return -1;
    }
    return 0;
}

/* Public wrappers: single owner for VM 24/25. */
static int wifi_cred_load(char *ssid, int ssid_sz, char *pwd, int pwd_sz)
{
    int ret;

    wifi_svc_cred_txn_lock();
    ret = wifi_cred_load_raw(ssid, ssid_sz, pwd, pwd_sz);
    wifi_svc_cred_txn_unlock();
    return ret;
}

static int wifi_cred_save(const char *ssid, const char *pwd)
{
    int ret;

    wifi_svc_cred_txn_lock();
    ret = wifi_cred_save_raw(ssid, pwd);
    wifi_svc_cred_txn_unlock();
    return ret;
}

static int wifi_cred_clear(void)
{
    int ret;

    wifi_svc_cred_txn_lock();
    ret = wifi_cred_clear_raw();
    wifi_svc_cred_txn_unlock();
    return ret;
}

static void wifi_service_abort_sdk_connect(void)
{
    wifi_set_sta_connect_best_ssid(0);
    wifi_clear_scan_result();
    wifi_enter_sta_mode("", "");
}

/* Forward: used by claim/fail rollback. */
static void wifi_sdk_apply_empty_sta_default(void);
static void wifi_sdk_clear_fast_connect_cache(void);
static void wifi_sdk_clear_pmk_cache(void);

/*
 * After forget we may persist empty STA as SDK default so OID will not probe
 * a deleted SSID. That empty default later triggers:
 *   connect default ssid[], wifi_force_time=1 → enter_sta_mode->,
 * and aborts an in-flight user connect (~3s later, device log).
 *
 * Claim overwrites that default with the intentional target once per connect.
 * App VM (syscfg 24/25) still saves only on DHCP success (pending path).
 * Call once only — dual claim double-wrote flash.
 */
static void wifi_sdk_claim_sta_target(const char *ssid, const char *pwd)
{
    struct wifi_store_info def;
    char ssid_buf[WIFI_SSID_MAXLEN];
    char pwd_buf[WIFI_PWD_MAXLEN];
    u8 slot;

    if ((ssid == NULL) || (ssid[0] == '\0')) {
        return;
    }

    memset(ssid_buf, 0, sizeof(ssid_buf));
    memset(pwd_buf, 0, sizeof(pwd_buf));
    strncpy(ssid_buf, ssid, sizeof(ssid_buf) - 1U);
    if (pwd != NULL) {
        strncpy(pwd_buf, pwd, sizeof(pwd_buf) - 1U);
    }

    memset(&def, 0, sizeof(def));
    def.mode = STA_MODE;
    def.connect_best_network = 0;
    /* Same indexing as boot path: ssid[mode - STA_MODE]. */
    slot = (u8)(def.mode - STA_MODE);
    strncpy((char *)def.ssid[slot], ssid_buf, sizeof(def.ssid[slot]) - 1U);
    strncpy((char *)def.pwd[slot], pwd_buf, sizeof(def.pwd[slot]) - 1U);
    /*
     * force=1 store=1: replace forget empty default so OID "connect default"
     * cannot re-enter empty STA. store_mode_info keeps last-mode in sync.
     */
    (void)wifi_set_default_mode(&def, 1, 1);
    (void)wifi_store_mode_info(STA_MODE, ssid_buf, pwd_buf);
    wifi_set_sta_connect_best_ssid(0);
    LOGI(TAG, "SDK STA target claimed: ssid=%s", ssid_buf);
}

/*
 * Connect failed or aborted without DHCP: do not leave a wrong password as
 * SDK default/last-mode. Restore app VM if present; else empty default.
 */
static void wifi_sdk_rollback_sta_default_after_fail(void)
{
    char ssid[WIFI_SSID_MAXLEN];
    char pwd[WIFI_PWD_MAXLEN];

    wifi_sdk_clear_fast_connect_cache();
    memset(ssid, 0, sizeof(ssid));
    memset(pwd, 0, sizeof(pwd));
    if (wifi_cred_load(ssid, sizeof(ssid), pwd, sizeof(pwd)) != 0) {
        wifi_sdk_claim_sta_target(ssid, pwd);
        LOGI(TAG, "SDK STA default restored from app VM after fail");
        return;
    }
    wifi_sdk_apply_empty_sta_default();
    LOGI(TAG, "SDK STA default cleared after fail (no app VM)");
}

static void wifi_service_clear_pending(void)
{
    memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
    memset(s_pending_pwd, 0, sizeof(s_pending_pwd));
    wifi_svc_bump_pending_gen_locked();
}

static uint8_t wifi_service_has_saved_credentials(void)
{
    char ssid[WIFI_SSID_MAXLEN];
    char pwd[WIFI_PWD_MAXLEN];

    return wifi_cred_load(ssid, sizeof(ssid), pwd, sizeof(pwd)) ? 1U : 0U;
}

/*
 * Memory-only status update. No SDK / syscfg under lock.
 * has_saved_credentials and rssi are refreshed in get_status().
 */
static void wifi_service_status_set_locked(wifi_service_conn_state_t state,
                                           wifi_service_conn_error_t error,
                                           const char *ssid)
{
    wifi_service_status_t next = s_wifi_status;

    next.state = state;
    next.error = error;
    if (state != WIFI_SERVICE_CONN_CONNECTED) {
        next.cred_save_failed = 0U;
        next.rssi = 0;
        next.signal_strength = 0;
    }
    if (ssid != NULL) {
        memset(next.ssid, 0, sizeof(next.ssid));
        strncpy(next.ssid, ssid, sizeof(next.ssid) - 1U);
    }

    if ((state != WIFI_SERVICE_CONN_CONNECTING) &&
        (state != WIFI_SERVICE_CONN_ASSOCIATED)) {
        s_connect_watch_ms = 0U;
    }

    if (memcmp(&next, &s_wifi_status, sizeof(next)) != 0) {
        s_wifi_status = next;
        s_wifi_status_version++;
    }
}

static void wifi_service_status_set(wifi_service_conn_state_t state,
                                    wifi_service_conn_error_t error,
                                    const char *ssid)
{
    wifi_svc_lock();
    wifi_service_status_set_locked(state, error, ssid);
    wifi_svc_unlock();
}

/* Caller holds lock. */
static uint32_t wifi_service_arm_connect_attempt_locked(const char *ssid)
{
    s_connect_attempt_id++;
    if (s_connect_attempt_id == 0U) {
        s_connect_attempt_id = 1U;
    }
    s_active_connect_attempt = s_connect_attempt_id;
    /* Fail/DHCP terminal events wait until enter_sta_mode binds s_enter_attempt. */
    s_enter_attempt = 0U;
    s_connect_watch_ms = 0U;
    if ((ssid != NULL) && (ssid[0] != '\0')) {
        memset(s_connect_ssid, 0, sizeof(s_connect_ssid));
        strncpy(s_connect_ssid, ssid, sizeof(s_connect_ssid) - 1U);
    }
    wifi_service_status_set_locked(WIFI_SERVICE_CONN_CONNECTING,
                                   WIFI_SERVICE_ERR_NONE, ssid);
    return s_active_connect_attempt;
}

/* Arm attempt id for any path that expects STA/DHCP completion events. */
static uint32_t wifi_service_arm_connect_attempt(const char *ssid)
{
    uint32_t token;

    wifi_svc_lock();
    token = wifi_service_arm_connect_attempt_locked(ssid);
    wifi_svc_unlock();
    return token;
}

/*
 * Finish deferred enter after old STA left. Caller holds connect_seq.
 * Uses s_switch_* target + arm_token; clears switch SM on success/fail.
 */
static void wifi_service_switch_enter_new(void)
{
    uint32_t arm_token;
    char local_ssid[WIFI_SSID_MAXLEN];
    char local_pwd[WIFI_PWD_MAXLEN];

    memset(local_ssid, 0, sizeof(local_ssid));
    memset(local_pwd, 0, sizeof(local_pwd));

    wifi_svc_lock();
    if (s_switch_phase != WIFI_SWITCH_WAIT_OLD_DISCONNECT) {
        wifi_svc_unlock();
        return;
    }
    arm_token = s_switch_arm_token;
    strncpy(local_ssid, s_switch_ssid, sizeof(local_ssid) - 1U);
    strncpy(local_pwd, s_switch_pwd, sizeof(local_pwd) - 1U);
    if (wifi_svc_attempt_live_locked(arm_token) == 0U) {
        wifi_svc_switch_cancel_locked();
        wifi_svc_unlock();
        LOGW(TAG, "switch enter cancelled: arm not live");
        return;
    }
    wifi_svc_switch_cancel_locked();
    wifi_svc_unlock();

    wifi_set_sta_connect_best_ssid(0);
    wifi_sdk_clear_pmk_cache();
    wifi_sdk_claim_sta_target(local_ssid, local_pwd);

    wifi_svc_lock();
    if (wifi_svc_attempt_live_locked(arm_token) == 0U) {
        wifi_svc_unlock();
        LOGW(TAG, "switch enter raced invalidate; skip sta mode");
        return;
    }
    s_enter_attempt = arm_token;
    wifi_svc_unlock();
    wifi_enter_sta_mode(local_ssid, local_pwd);
    LOGI(TAG, "switch enter new ssid=%s", local_ssid);
}

static d_err_t wifi_service_start_connect(const char *ssid, const char *pwd,
                                          uint8_t save_on_success,
                                          uint8_t allow_override)
{
    uint8_t need_leave_old = 0U;
    uint8_t defer_enter = 0U;
    uint32_t arm_token = 0U;
    char local_ssid[WIFI_SSID_MAXLEN];
    char local_pwd[WIFI_PWD_MAXLEN];

    if ((ssid == NULL) || (ssid[0] == '\0')) {
        wifi_service_status_set(WIFI_SERVICE_CONN_ERROR,
                                WIFI_SERVICE_ERR_NOT_FOUND, "");
        return D_ERR_INVALID_ARG;
    }
    if (!wifi_is_on()) {
        wifi_service_status_set(WIFI_SERVICE_CONN_ERROR,
                                WIFI_SERVICE_ERR_NOT_READY, ssid);
        return D_ERR_NOT_READY;
    }

    memset(local_ssid, 0, sizeof(local_ssid));
    memset(local_pwd, 0, sizeof(local_pwd));
    strncpy(local_ssid, ssid, sizeof(local_ssid) - 1U);
    if (pwd != NULL) {
        strncpy(local_pwd, pwd, sizeof(local_pwd) - 1U);
    }

    if (wifi_svc_configuration_lock() != D_OK) return D_ERR_BUSY;

    wifi_svc_lock();
    /*
     * Single-flight. Override invalidates old enter first so late ASSOC_FAIL
     * during leave sees enter_attempt==0. CONNECTED/busy leave is async —
     * no os_time_dly on UI path.
     */
    if ((s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTING) ||
        (s_wifi_status.state == WIFI_SERVICE_CONN_ASSOCIATED) ||
        (wifi_svc_attempt_live_locked(s_active_connect_attempt) != 0U) ||
        (wifi_svc_attempt_live_locked(s_enter_attempt) != 0U) ||
        (s_switch_phase != WIFI_SWITCH_NONE)) {
        if (allow_override == 0U) {
            wifi_svc_unlock();
            wifi_svc_connect_seq_unlock();
            LOGW(TAG, "WiFi connect busy, reject ssid=%s", ssid);
            return D_ERR_BUSY;
        }
        LOGW(TAG, "WiFi connect override (busy/switch): ssid=%s", ssid);
        wifi_svc_switch_cancel_locked();
        wifi_svc_invalidate_attempt_locked();
        need_leave_old = 1U;
        defer_enter = 1U;
    } else if (s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED) {
        LOGW(TAG, "WiFi connect switch from CONNECTED: ssid=%s", ssid);
        need_leave_old = 1U;
        defer_enter = 1U;
    }

    /* Commit connect args + pending + arm under one critical section. */
    memset(s_connect_ssid, 0, sizeof(s_connect_ssid));
    memset(s_connect_pwd, 0, sizeof(s_connect_pwd));
    strncpy(s_connect_ssid, local_ssid, sizeof(s_connect_ssid) - 1U);
    strncpy(s_connect_pwd, local_pwd, sizeof(s_connect_pwd) - 1U);
    wifi_service_clear_pending();
    if (save_on_success != 0U) {
        wifi_svc_bump_cred_epoch_locked();
        strncpy(s_pending_ssid, s_connect_ssid, sizeof(s_pending_ssid) - 1U);
        strncpy(s_pending_pwd, s_connect_pwd, sizeof(s_pending_pwd) - 1U);
    }
    arm_token = wifi_service_arm_connect_attempt_locked(local_ssid);

    if (defer_enter != 0U) {
        memset(s_switch_ssid, 0, sizeof(s_switch_ssid));
        memset(s_switch_pwd, 0, sizeof(s_switch_pwd));
        strncpy(s_switch_ssid, local_ssid, sizeof(s_switch_ssid) - 1U);
        strncpy(s_switch_pwd, local_pwd, sizeof(s_switch_pwd) - 1U);
        s_switch_arm_token = arm_token;
        s_switch_wait_ms = 0U;
        s_switch_phase = WIFI_SWITCH_WAIT_OLD_DISCONNECT;
    }
    wifi_svc_unlock();

    if (need_leave_old != 0U) {
        /*
         * Tear down old STA only. Do NOT poll wifi_get_sta_connect_state()
         * and enter here — SDK may already report DISCONNECT while a delayed
         * STA_DISCONNECT event is still pending; early enter would clear the
         * switch SM and the late event would kill the new attempt.
         * Advance only on WIFI_EVENT_STA_DISCONNECT or switch wait timeout.
         */
        wifi_service_abort_sdk_connect();
        wifi_svc_connect_seq_unlock();
        return D_OK;
    }

    wifi_set_sta_connect_best_ssid(0);
    wifi_sdk_clear_pmk_cache();
    wifi_sdk_claim_sta_target(local_ssid, local_pwd);

    wifi_svc_lock();
    if (wifi_svc_attempt_live_locked(arm_token) == 0U) {
        wifi_svc_unlock();
        LOGW(TAG, "connect superseded before enter; skip sta mode ssid=%s",
             local_ssid);
        wifi_svc_connect_seq_unlock();
        return D_OK;
    }
    s_enter_attempt = arm_token;
    wifi_svc_unlock();
    wifi_enter_sta_mode(local_ssid, local_pwd);

    wifi_svc_connect_seq_unlock();
    return D_OK;
}

static void wifi_sdk_clear_stored_sta_info(void)
{
    struct wifi_stored_sta_info stored_info[NETWORK_SSID_INFO_CNT];
    int count;
    int i;
    int pass;

    /*
     * SDK may hold multiple saved SSIDs. Delete in a few passes so a
     * reordered list cannot leave a residual entry for auto-reconnect.
     */
    for (pass = 0; pass < 3; pass++) {
        memset(stored_info, 0, sizeof(stored_info));
        count = wifi_get_stored_sta_info(stored_info);
        if (count <= 0) {
            break;
        }
        if (count > NETWORK_SSID_INFO_CNT) {
            count = NETWORK_SSID_INFO_CNT;
        }
        for (i = 0; i < count; i++) {
            if ((stored_info[i].ssid[0] == '\0') ||
                (stored_info[i].ssid[0] == 0xFFU)) {
                continue;
            }
            wifi_del_stored_sta_info((char *)stored_info[i].ssid);
        }
    }
    LOGI(TAG, "SDK stored STA clear done");
}

/*
 * Overwrite last-remembered STA (OID reconnect source) with empty + disable
 * best-network so forget/reconfig does not keep probing an old SSID.
 */
static void wifi_sdk_apply_empty_sta_default(void)
{
    struct wifi_store_info default_info;

    memset(&default_info, 0, sizeof(default_info));
    default_info.mode = STA_MODE;
    default_info.connect_best_network = 0;
    /* force=1 store=1: persist empty as default / last mode. */
    wifi_set_default_mode(&default_info, 1, 1);
    (void)wifi_store_mode_info(STA_MODE, "", "");
    wifi_set_sta_connect_best_ssid(0);
    LOGI(TAG, "SDK empty STA default applied (stored)");
}

static void wifi_sdk_clear_pmk_cache(void)
{
    u8 empty[WIFI_SDK_PMK_CACHE_LEN] = {0};

    /* WIFI_PMK_INFO 长度来自 SDK wifi_conf.c:
     * struct pmk_info { u32 crc; char pmk[32]; }。 */
    syscfg_write(WIFI_PMK_INFO, empty, (u16)sizeof(empty));
}

static void wifi_sdk_clear_fast_connect_cache(void)
{
    wifi_clear_scan_result();
    wifi_sdk_clear_pmk_cache();
    LOGI(TAG, "SDK PMK cache cleared");
}

/* Drop association and leave STA idle without best-network auto-join. */
static void wifi_sdk_leave_network_idle(void)
{
    enum wifi_sta_connect_state sta_st;

    wifi_set_sta_connect_best_ssid(0);
    wifi_clear_scan_result();
    /*
     * Empty enter restarts STA and clears the association. Skip when already
     * idle to avoid SCAN-FAILED / empty-SSID thrash after forget.
     */
    sta_st = wifi_get_sta_connect_state();
    if (sta_st != WIFI_STA_DISCONNECT) {
        wifi_enter_sta_mode("", "");
    }
    wifi_sdk_apply_empty_sta_default();
    wifi_clear_scan_result();
}

/*
 * take_seq=1: public path (self-lock). take_seq=0: caller already holds
 * connect-seq (forget_saved composite — no gap for reconnect).
 */
static d_err_t wifi_service_clear_saved_credentials_inner(uint8_t take_seq)
{
    /*
     * 1) Cancel switch SM, bump epoch, clear pending, invalidate, clear expected.
     * 2) Wipe VM under cred_txn (via wifi_cred_clear).
     * 3) Only after VM OK: DISCONNECTED (leave events ignored in that state).
     * If step 2 fails: no fake DISCONNECTED (retry forget).
     */
    if (take_seq != 0U) {
        if (wifi_svc_configuration_lock() != D_OK) return D_ERR_BUSY;
    }

    wifi_svc_lock();
    wifi_svc_switch_cancel_locked();
    wifi_svc_bump_cred_epoch_locked();
    wifi_service_clear_pending();
    memset(s_connect_ssid, 0, sizeof(s_connect_ssid));
    memset(s_connect_pwd, 0, sizeof(s_connect_pwd));
    wifi_svc_invalidate_attempt_locked();
    wifi_svc_unlock();

    if (wifi_cred_clear() != 0) {
        LOGE(TAG,
             "WiFi credentials clear incomplete (VM write failed); "
             "epoch advanced, VM may still hold old creds — retry forget");
        if (take_seq != 0U) {
            wifi_svc_connect_seq_unlock();
        }
        return D_ERR_IO;
    }

    wifi_svc_lock();
    wifi_service_status_set_locked(WIFI_SERVICE_CONN_DISCONNECTED,
                                   WIFI_SERVICE_ERR_NONE, "");
    wifi_svc_unlock();

    wifi_sdk_clear_stored_sta_info();
    wifi_sdk_apply_empty_sta_default();
    wifi_sdk_clear_fast_connect_cache();
    if (take_seq != 0U) {
        wifi_svc_connect_seq_unlock();
    }
    LOGI(TAG, "WiFi credentials cleared");
    return D_OK;
}

d_err_t wifi_service_clear_saved_credentials(void)
{
    return wifi_service_clear_saved_credentials_inner(1U);
}

static d_err_t wifi_service_disconnect_for_reconfig_inner(uint8_t take_seq)
{
    uint8_t already_down;
    enum wifi_sta_connect_state sta_st;

    if (take_seq != 0U) {
        if (wifi_svc_configuration_lock() != D_OK) return D_ERR_BUSY;
    }

    wifi_svc_lock();
    already_down =
        (s_wifi_status.state == WIFI_SERVICE_CONN_DISCONNECTED) ? 1U : 0U;
    /*
     * If clear_saved already moved us to DISCONNECTED, do not re-bump epoch.
     * Status DISCONNECTED first so leave's STA_DISCONNECT is ignored (no
     * suppress counter).
     */
    if (already_down == 0U) {
        wifi_svc_switch_cancel_locked();
        wifi_svc_bump_cred_epoch_locked();
        wifi_service_clear_pending();
        memset(s_connect_ssid, 0, sizeof(s_connect_ssid));
        memset(s_connect_pwd, 0, sizeof(s_connect_pwd));
        wifi_svc_invalidate_attempt_locked();
        wifi_service_status_set_locked(WIFI_SERVICE_CONN_DISCONNECTED,
                                       WIFI_SERVICE_ERR_NONE, "");
    } else {
        wifi_svc_switch_cancel_locked();
    }
    wifi_svc_unlock();

    if (!wifi_is_on()) {
        LOGW(TAG, "WiFi is not on, skip disconnect");
        if (take_seq != 0U) {
            wifi_svc_connect_seq_unlock();
        }
        return D_OK;
    }

    sta_st = wifi_get_sta_connect_state();
    if ((sta_st == WIFI_STA_NETWORK_STACK_DHCP_SUCC) ||
        (sta_st == WIFI_STA_CONNECT_SUCC) ||
        (already_down == 0U)) {
        /* Drop live association / force idle empty STA once. */
        wifi_sdk_leave_network_idle();
    } else {
        /* Service already DISCONNECTED: avoid extra module restart thrash. */
        wifi_set_sta_connect_best_ssid(0);
        wifi_sdk_apply_empty_sta_default();
        wifi_clear_scan_result();
    }
    if (take_seq != 0U) {
        wifi_svc_connect_seq_unlock();
    }
    LOGI(TAG, "WiFi reconnect mode requested");
    return D_OK;
}

d_err_t wifi_service_disconnect_for_reconfig(void)
{
    return wifi_service_disconnect_for_reconfig_inner(1U);
}

//*********************************************************************************//
//                              STA 连接（供外部事件调用）                          //
//*********************************************************************************//

void wifi_service_sta_connect(const char *ssid, const char *pwd)
{
    LOGI(TAG, "STA connect: ssid=%s", ssid ? ssid : "");
    (void)wifi_service_start_connect(ssid, pwd, 1U, 1U);
}

/* SDK 兼容性接口：config_network.c 等 SDK 文件会调用此函数 */
void wifi_sta_connect(char *ssid, char *pwd, char save)
{
    LOGI(TAG, "wifi_sta_connect: ssid=%s, save=%d", ssid ? ssid : "", save);
    (void)wifi_service_start_connect(ssid, pwd, (save != 0) ? 1U : 0U, 1U);
}

//*********************************************************************************//
//                              LAN 配置                                            //
//*********************************************************************************//

static void wifi_set_lan_setting_info(void)
{
    struct lan_setting lan_setting_info = {
        .WIRELESS_IP_ADDR0  = 192, .WIRELESS_IP_ADDR1  = 168,
        .WIRELESS_IP_ADDR2  = 1,   .WIRELESS_IP_ADDR3  = 1,
        .WIRELESS_NETMASK0  = 255, .WIRELESS_NETMASK1  = 255,
        .WIRELESS_NETMASK2  = 255, .WIRELESS_NETMASK3  = 0,
        .WIRELESS_GATEWAY0  = 192, .WIRELESS_GATEWAY1  = 168,
        .WIRELESS_GATEWAY2  = 1,   .WIRELESS_GATEWAY3  = 1,
        .SERVER_IPADDR1 = 192, .SERVER_IPADDR2 = 168,
        .SERVER_IPADDR3 = 1,   .SERVER_IPADDR4 = 1,
        .CLIENT_IPADDR1 = 192, .CLIENT_IPADDR2 = 168,
        .CLIENT_IPADDR3 = 1,   .CLIENT_IPADDR4 = 2,
        .SUB_NET_MASK1  = 255, .SUB_NET_MASK2  = 255,
        .SUB_NET_MASK3  = 255, .SUB_NET_MASK4  = 0,
    };
    net_set_lan_info(&lan_setting_info);
}

//*********************************************************************************//
//                              STA 重连                                            //
//*********************************************************************************//

void wifi_service_return_sta_mode(void)
{
#if (WIFI_RUN_MODE == WIFI_RUN_MODE_AP)
    return;
#else
    if (!wifi_is_on()) return;
    if (wifi_svc_configuration_lock() != D_OK) return;

    int ret;
    struct wifi_mode_info info;
    info.mode = STA_MODE;
    ret = wifi_get_mode_stored_info(&info);
    if (ret) {
        info.ssid = (char *)D_WIFI_SSID;
        info.pwd  = (char *)D_WIFI_PWD;
    }

    wifi_clear_scan_result();
    wifi_set_sta_connect_best_ssid(1);
    /* Must arm attempt so DHCP/STA_SUCC are not dropped as stale. */
    wifi_service_arm_connect_attempt(info.ssid);
    wifi_svc_lock();
    s_enter_attempt = s_active_connect_attempt;
    wifi_svc_unlock();
    wifi_enter_sta_mode(info.ssid, info.pwd);
    wifi_svc_connect_seq_unlock();
#endif
}

//*********************************************************************************//
//                              WiFi SDK 事件回调                                   //
//*********************************************************************************//

#if (D_FEATURE_BLE_PROVISION) && (WIFI_RUN_MODE != WIFI_RUN_MODE_AP)
static void wifi_service_apply_ble_default(struct wifi_store_info *parm)
{
    char stored_ssid[WIFI_SSID_MAXLEN];
    char stored_pwd[WIFI_PWD_MAXLEN];

    parm->mode = STA_MODE;
    if (wifi_cred_load(stored_ssid, sizeof(stored_ssid),
                       stored_pwd, sizeof(stored_pwd))) {
        strncpy((char *)parm->ssid[parm->mode - STA_MODE], stored_ssid,
                sizeof(parm->ssid[parm->mode - STA_MODE]) - 1);
        strncpy((char *)parm->pwd[parm->mode - STA_MODE], stored_pwd,
                sizeof(parm->pwd[parm->mode - STA_MODE]) - 1);
        wifi_set_default_mode(parm, 1, 1);
        wifi_set_sta_connect_best_ssid(1);
        /* Boot auto-connect: arm + bind enter so DHCP is accepted. */
        wifi_service_arm_connect_attempt(stored_ssid);
        wifi_svc_lock();
        s_enter_attempt = s_active_connect_attempt;
        wifi_svc_unlock();
        LOGI(TAG, "found stored creds, auto-connect: ssid=%s", stored_ssid);
        return;
    }

    wifi_set_default_mode(parm, 1, 0);
    wifi_set_sta_connect_best_ssid(0);
    wifi_service_status_set(WIFI_SERVICE_CONN_IDLE,
                            WIFI_SERVICE_ERR_NONE, "");
    LOGI(TAG, "no stored creds, wait BLE provisioning");
}
#endif

static void wifi_service_handle_module_init(void)
{
    struct wifi_store_info parm;

    memset(&parm, 0, sizeof(parm));
    wifi_set_sta_connect_timeout(30);
    wifi_set_smp_cfg_timeout(30);

#if (WIFI_RUN_MODE == WIFI_RUN_MODE_AP)
    char ssid[64];
    u8 mac_addr[6];
    wifi_get_mac(mac_addr);
    sprintf(ssid, D_WIFI_AP_SSID "%02x%02x%02x%02x%02x%02x",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);
    wifi_set_lan_setting_info();
    parm.mode = AP_MODE;
    strncpy((char *)parm.ssid[parm.mode - STA_MODE], ssid,
            sizeof(parm.ssid[parm.mode - STA_MODE]) - 1);
    strncpy((char *)parm.pwd[parm.mode - STA_MODE], D_WIFI_AP_PWD,
            sizeof(parm.pwd[parm.mode - STA_MODE]) - 1);
    wifi_set_default_mode(&parm, 1, 1);
#elif (D_FEATURE_BLE_PROVISION)
    /* BLE 配网模式：优先读取已存储凭证自动重连，无则等待 BLE 配网 */
    wifi_service_apply_ble_default(&parm);
#else
    parm.mode = STA_MODE;
    strncpy((char *)parm.ssid[parm.mode - STA_MODE], (const char *)D_WIFI_SSID,
            sizeof(parm.ssid[parm.mode - STA_MODE]) - 1);
    strncpy((char *)parm.pwd[parm.mode - STA_MODE], (const char *)D_WIFI_PWD,
            sizeof(parm.pwd[parm.mode - STA_MODE]) - 1);
    wifi_set_default_mode(&parm, 1, 1);
    wifi_service_arm_connect_attempt((const char *)D_WIFI_SSID);
    wifi_svc_lock();
    s_enter_attempt = s_active_connect_attempt;
    wifi_svc_unlock();
#endif
}

static void wifi_service_notify_dhcp_success(void)
{
    struct net_event net = {0};
    char ssid_local[WIFI_SSID_MAXLEN];
    char pending_ssid[WIFI_SSID_MAXLEN];
    char pending_pwd[WIFI_PWD_MAXLEN];
    char expected_ssid[WIFI_SSID_MAXLEN];
    uint32_t attempt;
    uint32_t cred_epoch;
    uint32_t pending_gen;
    uint8_t save_failed = 0U;
    uint8_t have_pending = 0U;
    const char *ssid = NULL;

    memset(ssid_local, 0, sizeof(ssid_local));
    memset(pending_ssid, 0, sizeof(pending_ssid));
    memset(pending_pwd, 0, sizeof(pending_pwd));
    memset(expected_ssid, 0, sizeof(expected_ssid));

    wifi_svc_lock();
    attempt = s_enter_attempt;
    if ((attempt == 0U) || (wifi_svc_attempt_live_locked(attempt) == 0U)) {
        wifi_svc_unlock();
        LOGW(TAG, "ignore stale DHCP success (enter_attempt=%u)",
             (unsigned)attempt);
        return;
    }
    cred_epoch = s_cred_epoch;
    pending_gen = s_pending_gen;
    strncpy(expected_ssid, s_connect_ssid, sizeof(expected_ssid) - 1U);
    if (s_pending_ssid[0] != '\0') {
        have_pending = 1U;
        strncpy(pending_ssid, s_pending_ssid, sizeof(pending_ssid) - 1U);
        strncpy(pending_pwd, s_pending_pwd, sizeof(pending_pwd) - 1U);
        strncpy(ssid_local, s_pending_ssid, sizeof(ssid_local) - 1U);
    } else if (s_connect_ssid[0] != '\0') {
        strncpy(ssid_local, s_connect_ssid, sizeof(ssid_local) - 1U);
    } else if (s_wifi_status.ssid[0] != '\0') {
        strncpy(ssid_local, s_wifi_status.ssid, sizeof(ssid_local) - 1U);
    }
    wifi_svc_unlock();

    /* Empty expected SSID is a hard reject (forget race / cleared intent). */
    if (wifi_svc_ssid_matches_expected(expected_ssid) == 0U) {
        LOGW(TAG, "ignore DHCP success: SSID mismatch expected=%s (no fail)",
             expected_ssid);
        return;
    }

    if (have_pending != 0U) {
        uint8_t can_save = 0U;
        int save_ret = -1;

        /*
         * Single cred txn: recheck intent under txn, write raw, clear pending
         * only if still live. Stale writer NEVER wifi_cred_clear — would erase
         * a newer connection's credentials.
         */
        wifi_svc_cred_txn_lock();
        wifi_svc_lock();
        can_save = ((wifi_svc_attempt_live_locked(attempt) != 0U) &&
                    (wifi_svc_pending_snapshot_live_locked(
                         cred_epoch, pending_gen, pending_ssid,
                         pending_pwd) != 0U))
                       ? 1U
                       : 0U;
        wifi_svc_unlock();

        if (can_save == 0U) {
            wifi_svc_cred_txn_unlock();
            LOGW(TAG,
                 "skip cred save after race (attempt=%u epoch=%u gen=%u)",
                 (unsigned)attempt, (unsigned)cred_epoch,
                 (unsigned)pending_gen);
        } else {
            save_ret = wifi_cred_save_raw(pending_ssid, pending_pwd);
            if (save_ret != 0) {
                wifi_svc_cred_txn_unlock();
                LOGW(TAG, "DHCP ok but credential save failed; keep pending");
                save_failed = 1U;
            } else {
                wifi_svc_lock();
                if ((wifi_svc_attempt_live_locked(attempt) != 0U) &&
                    (wifi_svc_pending_snapshot_live_locked(
                         cred_epoch, pending_gen, pending_ssid,
                         pending_pwd) != 0U)) {
                    wifi_service_clear_pending();
                } else {
                    LOGW(TAG,
                         "cred save lost race after write; keep VM (no wipe)");
                }
                wifi_svc_unlock();
                wifi_svc_cred_txn_unlock();
            }
        }
    }

    wifi_svc_lock();
    if ((cred_epoch != s_cred_epoch) ||
        (wifi_svc_attempt_live_locked(attempt) == 0U)) {
        wifi_svc_unlock();
        LOGW(TAG, "ignore DHCP success after race (attempt=%u)",
             (unsigned)attempt);
        return;
    }
    s_active_connect_attempt = 0U;
    s_enter_attempt = 0U;
    ssid = (ssid_local[0] != '\0') ? ssid_local : NULL;
    wifi_service_status_set_locked(WIFI_SERVICE_CONN_CONNECTED,
                                   WIFI_SERVICE_ERR_NONE, ssid);
    if (save_failed != 0U) {
        s_wifi_status.cred_save_failed = 1U;
        s_wifi_status_version++;
    }
    wifi_svc_unlock();

    net.arg = "net";
    net.event = NET_EVENT_CONNECTED;
    net_event_notify(NET_EVENT_FROM_USER, &net);
#ifdef CONFIG_MBEDTLS_ENABLE
    wifi_start_ntp_sync();
#endif
}

/*
 * Offline notify only: EVT_WIFI_DISCONNECTED path via app_main.
 * Do NOT send NET_EVENT_DISCONNECTED_AND_REQ_CONNECT (auto rejoin is a
 * separate policy decision after switch timeout / forced offline).
 */
static void wifi_service_notify_offline_only(void)
{
    struct net_event net = {0};

    net.arg = "net";
    net.event = NET_EVENT_DISCONNECTED;
    net_event_notify(NET_EVENT_FROM_USER, &net);
}

static void wifi_service_notify_disconnect(void)
{
    struct net_event net = {0};
    uint8_t do_notify = 0U;

    wifi_svc_lock();
    /* Deferred switch: old link left — enter new SSID (do not go DISCONNECTED). */
    if (s_switch_phase == WIFI_SWITCH_WAIT_OLD_DISCONNECT) {
        wifi_svc_unlock();
        wifi_svc_connect_seq_lock();
        wifi_service_switch_enter_new();
        wifi_svc_connect_seq_unlock();
        return;
    }

    /*
     * Arm-to-enter gap: intentional leave while enter_attempt==0 and new
     * attempt armed — ignore (no suppress counter).
     */
    if ((s_enter_attempt == 0U) &&
        (wifi_svc_attempt_live_locked(s_active_connect_attempt) != 0U) &&
        (s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTING)) {
        wifi_svc_unlock();
        LOGI(TAG, "ignore STA disconnect in arm-to-enter gap");
        return;
    }

    if ((s_wifi_status.state != WIFI_SERVICE_CONN_CONNECTED) &&
        (s_wifi_status.state != WIFI_SERVICE_CONN_CONNECTING) &&
        (s_wifi_status.state != WIFI_SERVICE_CONN_ASSOCIATED)) {
        wifi_svc_unlock();
        LOGI(TAG, "ignore STA disconnect in state=%d",
             (int)s_wifi_status.state);
        return;
    }

    wifi_svc_invalidate_attempt_locked();
    wifi_svc_switch_cancel_locked();
    wifi_service_status_set_locked(WIFI_SERVICE_CONN_DISCONNECTED,
                                   WIFI_SERVICE_ERR_DISCONNECTED, NULL);
    do_notify = 1U;
    wifi_svc_unlock();

    if (do_notify == 0U) {
        return;
    }
    wifi_service_notify_offline_only();

    memset(&net, 0, sizeof(net));
    net.arg = "net";
    net.event = NET_EVENT_DISCONNECTED_AND_REQ_CONNECT;
    net_event_notify(NET_EVENT_FROM_USER, &net);
}

/*
 * Apply connect error only for the enter-bound attempt.
 * SDK callback has no generation in network_ctx — never use a fresh snapshot
 * of s_active_connect_attempt at event entry (that mis-kills a newer arm).
 */
static void wifi_service_set_connect_error_for_enter_attempt(
    wifi_service_conn_error_t error)
{
    uint32_t attempt;

    wifi_svc_lock();
    attempt = s_enter_attempt;
    if ((attempt == 0U) || (wifi_svc_attempt_live_locked(attempt) == 0U)) {
        wifi_svc_unlock();
        LOGW(TAG, "ignore stale connect error (enter_attempt=%u err=%d)",
             (unsigned)attempt, (int)error);
        return;
    }
    if ((s_wifi_status.state != WIFI_SERVICE_CONN_CONNECTING) &&
        (s_wifi_status.state != WIFI_SERVICE_CONN_ASSOCIATED) &&
        (s_wifi_status.state != WIFI_SERVICE_CONN_CONNECTED)) {
        wifi_svc_unlock();
        return;
    }
    wifi_svc_switch_cancel_locked();
    wifi_svc_invalidate_attempt_locked();
    wifi_service_clear_pending();
    wifi_service_status_set_locked(WIFI_SERVICE_CONN_ERROR, error, NULL);
    wifi_svc_unlock();
    /*
     * Outside lock: drop wrong-password SDK default / PMK so a later correct
     * connect is not poisoned (review: claim must not leave bad store).
     */
    wifi_sdk_rollback_sta_default_after_fail();
}

static int wifi_event_callback(void *network_ctx, enum WIFI_EVENT event)
{
    uint32_t attempt;
    char expected_ssid[WIFI_SSID_MAXLEN];

    /*
     * network_ctx: SDK documents only void* + WIFI_EVENT; no generation.
     * Do not invent fields. Terminal fail/DHCP use s_enter_attempt instead.
     */
    (void)network_ctx;
    memset(expected_ssid, 0, sizeof(expected_ssid));

    switch (event) {
    case WIFI_EVENT_MODULE_INIT:
        wifi_service_handle_module_init();
        break;
    case WIFI_EVENT_STA_CONNECT_SUCC:
        wifi_svc_lock();
        attempt = s_enter_attempt;
        strncpy(expected_ssid, s_connect_ssid, sizeof(expected_ssid) - 1U);
        if ((attempt == 0U) || (wifi_svc_attempt_live_locked(attempt) == 0U)) {
            wifi_svc_unlock();
            LOGW(TAG, "ignore stale STA connect success");
            break;
        }
        wifi_svc_unlock();
        if (wifi_svc_ssid_matches_expected(expected_ssid) == 0U) {
            /* Drop only — never fail the live attempt (see DHCP path comment). */
            LOGW(TAG, "ignore STA success: SSID mismatch expected=%s (no fail)",
                 expected_ssid);
            break;
        }
        LOGI(TAG, "STA connect success");
        wifi_svc_lock();
        if (wifi_svc_attempt_live_locked(attempt) != 0U) {
            wifi_service_status_set_locked(WIFI_SERVICE_CONN_ASSOCIATED,
                                           WIFI_SERVICE_ERR_NONE, NULL);
        }
        wifi_svc_unlock();
        break;
    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
        wifi_service_set_connect_error_for_enter_attempt(
            WIFI_SERVICE_ERR_NOT_FOUND);
        LOGW(TAG, "STA SSID not found");
        break;
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
        wifi_service_set_connect_error_for_enter_attempt(
            WIFI_SERVICE_ERR_ASSOC_FAIL);
        LOGW(TAG, "STA associate fail (wrong password?)");
        break;
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT:
        wifi_service_set_connect_error_for_enter_attempt(
            WIFI_SERVICE_ERR_ASSOC_TIMEOUT);
        LOGW(TAG, "STA associate timeout");
        break;
    case WIFI_EVENT_STA_SCAN_COMPLETED:
        s_scan_completed = 1U;
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC:
        LOGI(TAG, "DHCP success, network ready!");
        wifi_service_notify_dhcp_success();
        break;
    case WIFI_EVENT_STA_DISCONNECT:
        LOGW(TAG, "STA disconnected");
        wifi_service_notify_disconnect();
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT:
        wifi_service_set_connect_error_for_enter_attempt(
            WIFI_SERVICE_ERR_DHCP_TIMEOUT);
        LOGW(TAG, "DHCP timeout");
        break;
    default:
        break;
    }
    return 0;
}

//*********************************************************************************//
//                              BLE 配网事件订阅                                    //
//*********************************************************************************//

/* 订阅 BLE 配网请求 */
static void on_ble_wifi_cfg_request(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_BLE_WIFI_CFG_REQUEST) return;
    const wifi_cfg_t *cfg = (const wifi_cfg_t *)evt->data;
    if (!cfg) return;

    LOGI(TAG, "收到 BLE 配网请求: ssid=%s", cfg->ssid);
    wifi_service_sta_connect(cfg->ssid, cfg->pwd);
}

/*
 * Async EVT_WIFI_CONNECTED pending save retry.
 * Gate: cred_epoch + pending_gen + ssid + password must still match.
 */
static void on_wifi_connected_save(const d_event_t *evt, void *ctx)
{
    char pending_ssid[WIFI_SSID_MAXLEN];
    char pending_pwd[WIFI_PWD_MAXLEN];
    uint32_t cred_epoch;
    uint32_t pending_gen;

    (void)ctx;
    if ((evt == NULL) || (evt->id != EVT_WIFI_CONNECTED)) {
        return;
    }

    memset(pending_ssid, 0, sizeof(pending_ssid));
    memset(pending_pwd, 0, sizeof(pending_pwd));

    wifi_svc_cred_txn_lock();
    wifi_svc_lock();
    if ((s_pending_ssid[0] == '\0') ||
        (s_wifi_status.state != WIFI_SERVICE_CONN_CONNECTED)) {
        wifi_svc_unlock();
        wifi_svc_cred_txn_unlock();
        return;
    }
    cred_epoch = s_cred_epoch;
    pending_gen = s_pending_gen;
    strncpy(pending_ssid, s_pending_ssid, sizeof(pending_ssid) - 1U);
    strncpy(pending_pwd, s_pending_pwd, sizeof(pending_pwd) - 1U);
    if (wifi_svc_pending_snapshot_live_locked(
            cred_epoch, pending_gen, pending_ssid, pending_pwd) == 0U) {
        wifi_svc_unlock();
        wifi_svc_cred_txn_unlock();
        LOGW(TAG, "skip async cred save after race (epoch=%u gen=%u)",
             (unsigned)cred_epoch, (unsigned)pending_gen);
        return;
    }
    wifi_svc_unlock();

    if (wifi_cred_save_raw(pending_ssid, pending_pwd) != 0) {
        wifi_svc_cred_txn_unlock();
        LOGW(TAG, "async cred save failed; keep pending");
        return;
    }

    wifi_svc_lock();
    if (wifi_svc_pending_snapshot_live_locked(
            cred_epoch, pending_gen, pending_ssid, pending_pwd) != 0U) {
        wifi_service_clear_pending();
        if (s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED) {
            s_wifi_status.cred_save_failed = 0U;
            s_wifi_status.has_saved_credentials = 1U;
            memset(s_wifi_status.ssid, 0, sizeof(s_wifi_status.ssid));
            strncpy(s_wifi_status.ssid, pending_ssid,
                    sizeof(s_wifi_status.ssid) - 1U);
            s_wifi_status_version++;
        }
    } else {
        /* Stale intent after write: never wipe VM (may hold newer password). */
        LOGW(TAG, "async cred save lost race after write; keep VM (no wipe)");
        if (s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED) {
            s_wifi_status.cred_save_failed = 0U;
            s_wifi_status.has_saved_credentials = 1U;
        }
    }
    wifi_svc_unlock();
    wifi_svc_cred_txn_unlock();
}

//*********************************************************************************//
//                         NTP 校时（加密时间消费者依赖）                           //
//*********************************************************************************//

#ifdef CONFIG_MBEDTLS_ENABLE
static void ntp_sync_task(void *priv)
{
    LOGI(TAG, "NTP 校时开始...");

    struct tm s_tm;
    int ret = ntp_client_get_time_all(&s_tm, 5000);
    if (ret == 0) {
        time_t epoch = mktime(&s_tm);
        d_crypto_set_ntp_time((unsigned long long)epoch);
        LOGI(TAG, "NTP 校时成功: %04d-%02d-%02d %02d:%02d:%02d",
             s_tm.tm_year + 1900, s_tm.tm_mon + 1, s_tm.tm_mday,
             s_tm.tm_hour, s_tm.tm_min, s_tm.tm_sec);
        d_event_post(EVT_NTP_SYNCED, NULL, 0);
    } else {
        LOGW(TAG, "NTP 校时失败 (ret=%d)，加密网络业务将使用兜底时间", ret);
        /* 即使失败也发布事件，让依赖方不会永远等待 */
        d_event_post(EVT_NTP_SYNCED, NULL, 0);
    }
}

static void wifi_start_ntp_sync(void)
{
    int ret;

    /* [根因修复] ntp_sync_task 是一次性任务，执行完毕后 return。
     * os_task_create 要求任务函数永不返回（须内含 while(1)），
     * 否则函数 return 后 PC 跳到无效地址（reti=0x2）→ c0_if_bus_inv。
     * thread_fork 内部有退出包装，支持任务函数正常 return，
     * 与 ws_recv / ws_client / prov_task 等一次性任务的创建方式一致。*/
    ret = thread_fork("ntp_sync", 10, 2048, 0, NULL, ntp_sync_task, NULL);
    if (ret != OS_NO_ERR) {
        LOGE(TAG, "ntp_sync 创建失败: %d", ret);
        d_event_post(EVT_NTP_SYNCED, NULL, 0);
    }
}
#endif /* CONFIG_MBEDTLS_ENABLE */

//*********************************************************************************//
//                              WiFi 任务                                           //
//*********************************************************************************//

static void wifi_service_connect_watchdog_tick(uint32_t elapsed_ms)
{
    uint8_t do_stall_abort = 0U;
    uint8_t force_invalidate = 0U;
    uint32_t stall_enter = 0U;

    wifi_svc_lock();
    /*
     * Switch SM: only STA_DISCONNECT advances enter. Missing event → timeout
     * ERROR. Never poll SDK idle to guess-enter (late event would kill new).
     */
    if (s_switch_phase == WIFI_SWITCH_WAIT_OLD_DISCONNECT) {
        s_switch_wait_ms += elapsed_ms;
        if (s_switch_wait_ms < WIFI_SWITCH_WAIT_MS) {
            wifi_svc_unlock();
            return;
        }
        wifi_svc_switch_cancel_locked();
        wifi_service_clear_pending();
        wifi_svc_invalidate_attempt_locked();
        wifi_service_status_set_locked(
            WIFI_SERVICE_CONN_ERROR,
            WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT, NULL);
        wifi_svc_unlock();

        /*
         * Destructive SDK under connect_seq; recheck no newer live attempt
         * (BLE override may have won after we invalidated).
         */
        wifi_svc_connect_seq_lock();
        wifi_svc_lock();
        if ((wifi_svc_attempt_live_locked(s_active_connect_attempt) != 0U) ||
            (wifi_svc_attempt_live_locked(s_enter_attempt) != 0U) ||
            (s_switch_phase != WIFI_SWITCH_NONE)) {
            wifi_svc_unlock();
            wifi_svc_connect_seq_unlock();
            LOGW(TAG, "switch timeout abort skipped (newer connect live)");
            return;
        }
        wifi_svc_unlock();
        LOGW(TAG, "switch wait old disconnect timeout");
        wifi_service_abort_sdk_connect();
        wifi_sdk_rollback_sta_default_after_fail();
        /*
         * Status is ERROR; late STA_DISCONNECT is ignored. Publish offline
         * once so BLE/Qwen/BRTC drop "online" without auto rejoin.
         */
        wifi_service_notify_offline_only();
        wifi_svc_connect_seq_unlock();
        return;
    }

    if ((s_wifi_status.state != WIFI_SERVICE_CONN_CONNECTING) &&
        (s_wifi_status.state != WIFI_SERVICE_CONN_ASSOCIATED)) {
        s_connect_watch_ms = 0U;
        wifi_svc_unlock();
        return;
    }
    s_connect_watch_ms += elapsed_ms;
    if (s_connect_watch_ms < WIFI_CONNECT_STALL_MS) {
        wifi_svc_unlock();
        return;
    }
    s_connect_watch_ms = 0U;
    stall_enter = s_enter_attempt;
    /*
     * enter_attempt==0 means arm completed but enter bind never happened.
     * set_connect_error_for_enter would no-op — force invalidate here.
     */
    if (s_enter_attempt == 0U) {
        force_invalidate = 1U;
        wifi_svc_switch_cancel_locked();
        wifi_service_clear_pending();
        wifi_svc_invalidate_attempt_locked();
        wifi_service_status_set_locked(WIFI_SERVICE_CONN_ERROR,
                                       WIFI_SERVICE_ERR_ASSOC_TIMEOUT, NULL);
    }
    do_stall_abort = 1U;
    wifi_svc_unlock();

    if (do_stall_abort == 0U) {
        return;
    }

    wifi_svc_connect_seq_lock();
    wifi_svc_lock();
    if (force_invalidate != 0U) {
        /* We already killed our arm; skip if a newer session is live. */
        if ((wifi_svc_attempt_live_locked(s_active_connect_attempt) != 0U) ||
            (wifi_svc_attempt_live_locked(s_enter_attempt) != 0U) ||
            (s_switch_phase != WIFI_SWITCH_NONE)) {
            wifi_svc_unlock();
            wifi_svc_connect_seq_unlock();
            LOGW(TAG, "connect stall abort skipped (newer connect live)");
            return;
        }
    } else {
        /*
         * Still the stalled session: enter token must still be live. Skip if
         * superseded by a newer arm/enter/switch.
         */
        if ((stall_enter == 0U) ||
            (wifi_svc_attempt_live_locked(stall_enter) == 0U) ||
            (s_switch_phase != WIFI_SWITCH_NONE)) {
            wifi_svc_unlock();
            wifi_svc_connect_seq_unlock();
            LOGW(TAG, "connect stall abort skipped (session gone/switched)");
            return;
        }
    }
    wifi_svc_unlock();

    LOGW(TAG, "STA connect stall: abort SDK + invalidate attempt");
    wifi_service_abort_sdk_connect();
    if (force_invalidate != 0U) {
        wifi_sdk_rollback_sta_default_after_fail();
        wifi_svc_connect_seq_unlock();
        return;
    }
    wifi_service_set_connect_error_for_enter_attempt(
        WIFI_SERVICE_ERR_ASSOC_TIMEOUT);
    wifi_svc_connect_seq_unlock();
}

static void wifi_task(void *priv)
{
    wifi_set_event_callback(wifi_event_callback);
    wifi_on();
    LOGI(TAG, "WiFi ON");

    while (1) {
        /* ~1s on 10ms OS tick; advances connect stall watchdog. */
        os_time_dly(100);
        wifi_service_connect_watchdog_tick(1000U);
    }
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

d_err_t wifi_service_connect_ui(const char *ssid, const char *pwd)
{
    return wifi_service_start_connect(ssid, pwd, 1U, 0U);
}

d_err_t wifi_service_reconnect_saved(void)
{
    char ssid[WIFI_SSID_MAXLEN];
    char pwd[WIFI_PWD_MAXLEN];
    wifi_service_conn_state_t state;

    if (!wifi_cred_load(ssid, sizeof(ssid), pwd, sizeof(pwd))) {
        /*
         * Do not stomp a live CONNECTED status. Network may still be up
         * from an in-session connect while VM read fails / was wiped.
         */
        wifi_svc_lock();
        state = s_wifi_status.state;
        wifi_svc_unlock();
        if (state != WIFI_SERVICE_CONN_CONNECTED) {
            wifi_service_status_set(WIFI_SERVICE_CONN_ERROR,
                                    WIFI_SERVICE_ERR_NO_SAVED, "");
        }
        return D_ERR_NOT_FOUND;
    }

    wifi_svc_lock();
    state = s_wifi_status.state;
    wifi_svc_unlock();
    if (state == WIFI_SERVICE_CONN_CONNECTED) {
        /* Already online with saved creds — refresh status only. */
        wifi_service_status_set(WIFI_SERVICE_CONN_CONNECTED,
                                WIFI_SERVICE_ERR_NONE, ssid);
        return D_OK;
    }

    /*
     * No override while associating: UI + service both reject double-tap.
     * save_on_success=0: VM already holds creds.
     */
    return wifi_service_start_connect(ssid, pwd, 0U, 0U);
}

d_err_t wifi_service_forget_saved(void)
{
    d_err_t err;

    /*
     * One connect-seq critical section for clear+leave so a concurrent
     * start_connect cannot arm/enter between the two public helpers.
     */
    if (wifi_svc_configuration_lock() != D_OK) return D_ERR_BUSY;
    err = wifi_service_clear_saved_credentials_inner(0U);
    if (err != D_OK) {
        /*
         * VM wipe failed: still best-effort drop radio so UI/service are not
         * left "soft disconnected" while STA stays online.
         */
        (void)wifi_service_disconnect_for_reconfig_inner(0U);
        wifi_svc_connect_seq_unlock();
        return err;
    }
    err = wifi_service_disconnect_for_reconfig_inner(0U);
    wifi_svc_connect_seq_unlock();
    if (err != D_OK) {
        return err;
    }
    /*
     * Forget leaves status DISCONNECTED before radio leave, so STA_DISCONNECT
     * is ignored (no suppress counter). Re-open BLE provisioning adv.
     */
#if D_FEATURE_BLE_PROVISION
    (void)ble_service_enter_reconfig_mode();
#endif
    return D_OK;
}

d_err_t wifi_service_scan_start(void)
{
    wifi_service_conn_state_t conn;
    wifi_service_scan_state_t scan;

    wifi_svc_lock();
    scan = s_scan_state;
    conn = s_wifi_status.state;
    wifi_svc_unlock();

    if (scan == WIFI_SERVICE_SCAN_SCANNING) {
        return D_ERR_BUSY;
    }
    if ((conn == WIFI_SERVICE_CONN_CONNECTING) ||
        (conn == WIFI_SERVICE_CONN_ASSOCIATED)) {
        LOGW(TAG, "scan rejected while connecting");
        return D_ERR_BUSY;
    }

    return wifi_scan_request_start();
}

void wifi_service_scan_update(uint32_t elapsed_ms)
{
    if (s_scan_completed != 0U) {
        if ((s_scan_state == WIFI_SERVICE_SCAN_SCANNING) ||
            (s_scan_state == WIFI_SERVICE_SCAN_BUSY)) {
            wifi_scan_complete_current();
            return;
        }
        s_scan_completed = 0;
    }

    if ((s_scan_state != WIFI_SERVICE_SCAN_SCANNING) &&
        (s_scan_state != WIFI_SERVICE_SCAN_BUSY) &&
        (s_scan_state != WIFI_SERVICE_SCAN_NOT_READY)) {
        return;
    }

    s_scan_elapsed_ms += elapsed_ms;
    if ((s_scan_state == WIFI_SERVICE_SCAN_BUSY) ||
        (s_scan_state == WIFI_SERVICE_SCAN_NOT_READY)) {
        if (s_scan_elapsed_ms >= WIFI_SCAN_RETRY_INTERVAL_MS) {
            (void)wifi_scan_request_start();
        }
        return;
    }

    if (s_scan_state != WIFI_SERVICE_SCAN_SCANNING) {
        return;
    }

    if (s_scan_elapsed_ms >= WIFI_SCAN_TIMEOUT_MS) {
        s_scan_elapsed_ms = 0;
        wifi_scan_set_state(WIFI_SERVICE_SCAN_TIMEOUT);
        return;
    }
}

wifi_service_scan_state_t wifi_service_scan_get_state(void)
{
    wifi_service_scan_state_t state;

    wifi_svc_lock();
    state = s_scan_state;
    wifi_svc_unlock();
    return state;
}

uint8_t wifi_service_scan_copy_results(wifi_service_scan_result_t *out, uint8_t max_count)
{
    uint8_t copy_count;

    if ((out == NULL) || (max_count == 0U)) {
        return 0;
    }

    wifi_svc_lock();
    copy_count = s_scan_result_count;
    if (copy_count > max_count) {
        copy_count = max_count;
    }
    if (copy_count > 0U) {
        memcpy(out, s_scan_results,
               (size_t)copy_count * sizeof(wifi_service_scan_result_t));
    }
    wifi_svc_unlock();
    return copy_count;
}

uint32_t wifi_service_scan_get_version(void)
{
    uint32_t version;

    wifi_svc_lock();
    version = s_scan_version;
    wifi_svc_unlock();
    return version;
}

void wifi_service_get_status(wifi_service_status_t *status)
{
    wifi_service_status_t snap;
    uint8_t connected;

    if (status == NULL) {
        return;
    }

    wifi_svc_lock();
    snap = s_wifi_status;
    wifi_svc_unlock();

    /* VM + RSSI outside lock (may block / call SDK). */
    snap.has_saved_credentials = wifi_service_has_saved_credentials();
    connected = (snap.state == WIFI_SERVICE_CONN_CONNECTED) ? 1U : 0U;
    if ((connected != 0U) && wifi_is_on()) {
        snap.rssi = (int8_t)wifi_get_rssi();
        snap.signal_strength = wifi_scan_rssi_to_signal(snap.rssi);
    }
    *status = snap;
}

uint32_t wifi_service_get_status_version(void)
{
    uint32_t version;

    wifi_svc_lock();
    version = s_wifi_status_version;
    wifi_svc_unlock();
    return version;
}

uint8_t wifi_service_auth_requires_password(uint8_t auth_mode)
{
    return (auth_mode == (uint8_t)WIFI_SERVICE_AUTH_OPEN) ? 0U : 1U;
}

const char *wifi_service_auth_name(uint8_t auth_mode)
{
    switch (auth_mode) {
    case WIFI_SERVICE_AUTH_OPEN:
        return "OPEN";
    case WIFI_SERVICE_AUTH_WEP:
        return "WEP";
    case WIFI_SERVICE_AUTH_WAPI:
        return "WAPI";
    case WIFI_SERVICE_AUTH_WPA:
        return "WPA";
    case WIFI_SERVICE_AUTH_WPA2PSK:
        return "WPA2";
    case WIFI_SERVICE_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_SERVICE_AUTH_WPA3_SAE:
        return "WPA3";
    case WIFI_SERVICE_AUTH_WPA2_WPA3_SAE:
        return "WPA2/WPA3";
    case WIFI_SERVICE_AUTH_WPA3_H2E:
        return "WPA3-H2E";
    default:
        return "SEC";
    }
}

static void wifi_service_cleanup_init_failure(void)
{
    d_event_unsubscribe(EVT_BLE_WIFI_CFG_REQUEST, on_ble_wifi_cfg_request);
    d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_connected_save);
    if (s_wifi_cred_txn_ready != 0U) {
        os_mutex_del(&s_wifi_cred_txn_mutex, OS_DEL_ALWAYS);
        s_wifi_cred_txn_ready = 0U;
    }
    if (s_wifi_connect_seq_ready != 0U) {
        os_mutex_del(&s_wifi_connect_seq_mutex, OS_DEL_ALWAYS);
        s_wifi_connect_seq_ready = 0U;
    }
    if (s_wifi_svc_mutex_ready != 0U) {
        os_mutex_del(&s_wifi_svc_mutex, OS_DEL_ALWAYS);
        s_wifi_svc_mutex_ready = 0U;
    }
}

d_err_t wifi_service_init(void)
{
    LOGI(TAG, "WiFi 服务初始化");

    if (s_wifi_svc_mutex_ready == 0U) {
        if (os_mutex_create(&s_wifi_svc_mutex) != 0) {
            LOGE(TAG, "wifi service mutex create failed");
            return D_ERR_IO;
        }
        s_wifi_svc_mutex_ready = 1U;
    }
    if (s_wifi_connect_seq_ready == 0U) {
        if (os_mutex_create(&s_wifi_connect_seq_mutex) != 0) {
            LOGE(TAG, "wifi connect seq mutex create failed");
            wifi_service_cleanup_init_failure();
            return D_ERR_IO;
        }
        s_wifi_connect_seq_ready = 1U;
    }
    if (s_wifi_cred_txn_ready == 0U) {
        if (os_mutex_create(&s_wifi_cred_txn_mutex) != 0) {
            LOGE(TAG, "wifi cred txn mutex create failed");
            wifi_service_cleanup_init_failure();
            return D_ERR_IO;
        }
        s_wifi_cred_txn_ready = 1U;
    }

    /* 订阅 BLE 配网请求事件 */
    if (d_event_subscribe(EVT_BLE_WIFI_CFG_REQUEST, on_ble_wifi_cfg_request, NULL) != 0) {
        LOGE(TAG, "BLE 配网事件订阅失败");
        wifi_service_cleanup_init_failure();
        return D_ERR_IO;
    }

    /* 订阅连接成功事件，用于持久化 BLE 配网凭证 / 存盘重试 */
    if (d_event_subscribe(EVT_WIFI_CONNECTED, on_wifi_connected_save, NULL) != 0) {
        LOGE(TAG, "WiFi 连接事件订阅失败");
        wifi_service_cleanup_init_failure();
        return D_ERR_IO;
    }

    if (os_task_create(wifi_task, NULL, 10, 1024, 0, "wifi_task") != 0) {
        LOGE(TAG, "wifi_task 创建失败");
        wifi_service_cleanup_init_failure();
        return D_ERR_IO;
    }

    return D_OK;
}

void wifi_service_deinit(void)
{
    d_event_unsubscribe(EVT_BLE_WIFI_CFG_REQUEST, on_ble_wifi_cfg_request);
    d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_connected_save);
}

#endif /* D_FEATURE_WIFI */
