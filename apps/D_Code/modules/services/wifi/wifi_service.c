/**
 * @file    wifi_service.c
 * @brief   序列化连接、凭据事务与前台请求，隔离迟到 SDK 事件。
 */

#include "config/app_config.h"

#if D_FEATURE_WIFI

#include "wifi_service.h"
#include "system/includes.h"
#include "system/timer.h"
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

static void wifi_service_refresh_cached_status(void);

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
 * 24/25 由本服务持有；完整分配见项目 VM 索引表） */
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
/* 待保存内容设置或清空时推进代次，阻止异步 DHCP 或 EVT_WIFI_CONNECTED 把同一 SSID 的旧密码写回。 */
static uint32_t s_pending_gen = 1U;
/* SDK STA 连接默认异步，参数缓冲必须在调用者返回后继续有效。 */
static char s_connect_ssid[WIFI_SSID_MAXLEN];
static char s_connect_pwd[WIFI_PWD_MAXLEN];
static wifi_service_scan_result_t s_scan_results[WIFI_SERVICE_SCAN_MAX_RESULTS];
static uint8_t s_scan_result_count = 0;
static volatile uint8_t s_scan_completed = 0;
static wifi_service_scan_state_t s_scan_state = WIFI_SERVICE_SCAN_IDLE;
static uint32_t s_scan_elapsed_ms = 0;
/* 另一 SDK 序列持有 connect_seq 时累计的 UI 扫描等待预算。 */
static uint32_t s_scan_deferred_elapsed = 0U;
static uint32_t s_scan_version = 0;
static wifi_service_status_t s_wifi_status = {
    .state = WIFI_SERVICE_CONN_IDLE,
    .error = WIFI_SERVICE_ERR_NONE,
};
static uint32_t s_wifi_status_version = 0;
/* SDK 停留 CONNECTING 或 ASSOCIATED 且无终态事件时的软监督期限。 */
#define WIFI_CONNECT_STALL_MS  45000U
static uint32_t s_connect_watch_ms = 0;
/* 每次开始或撤销都推进编号，避免迟到 SDK 事件作用于新尝试。 */
static uint32_t s_connect_attempt_id = 0;
static uint32_t s_active_connect_attempt = 0;
/* 尝试编号在 wifi_enter_sta_mode 时绑定，而非回调入口时绑定。
 * 失败与 DHCP 副作用据此拒绝迟到事件，不能让旧 ASSOC_FAIL 采用新分配编号。
 * SDK 不提供代次；0 表示尚未提交 enter，分配到进入之间忽略终止失败。 */
static uint32_t s_enter_attempt = 0;
/* 忘记、清空及新待存密码均推进凭据代次，使同 SSID 换密码也能失效旧异步保存。 */
static uint32_t s_cred_epoch = 1U;
/* 共享状态锁：SDK、wifi_task、LVGL 与 BLE 均可能访问状态、尝试编号及 pending。 */
static OS_MUTEX s_wifi_svc_mutex;
static uint8_t s_wifi_svc_mutex_ready = 0;
/* 串行化连接撤销、目标认领、进入及忘记、断连，避免进入编号混用或忘记间隙重连。
 * 锁顺序为 connect_seq → cred_txn → svc，不能反向。 */
static OS_MUTEX s_wifi_connect_seq_mutex;
static uint8_t s_wifi_connect_seq_ready = 0;
/* 产品协调可冻结配置而保持当前链路；写者持有 connect_seq，读写该标志也持有 svc。 */
static uint8_t s_configuration_held;
/* VM 24/25 的读、存、清由单一事务所有者串行执行；旧异步写者不能在新连接提交凭据后擦除 VM。 */
static OS_MUTEX s_wifi_cred_txn_mutex;
static uint8_t s_wifi_cred_txn_ready = 0;

/* 信号量表示执行占用，不是在 UI 与工作任务间转交互斥量。
 * 顺序为 operation gate → connect_seq → cred_txn → svc。SDK 回调保留现有序列规则；
 * 不在短 svc 锁内执行 I/O。 */
static OS_SEM s_wifi_operation_gate;
static OS_SEM s_wifi_worker_wake;
static uint8_t s_wifi_operation_gate_ready;
static uint8_t s_wifi_worker_wake_ready;
typedef struct {
    wifi_service_request_t payload;
    wifi_service_request_snapshot_t snapshot;
    uint32_t next_id;
    uint8_t enabled;
    uint8_t worker_started;
    uint8_t radio_ready;
} wifi_request_context_t;
static wifi_request_context_t s_request;

static d_err_t wifi_service_reconnect_saved_execute(void);
static d_err_t wifi_service_forget_saved_execute(void);
static d_err_t wifi_service_scan_start_execute(void);
static void wifi_service_request_process(void);
/* 私有切网状态机对外保持 CONNECTING；新 enter_sta_mode 前等待真实旧 STA 退出。
 * 退出以 DISCONNECT 或符合条件的 STOP 确认，不用计数压制断连，也不用空闲快照推断退出。 */
typedef enum {
    WIFI_SWITCH_NONE = 0,
    WIFI_SWITCH_WAIT_OLD_DISCONNECT,
    WIFI_SWITCH_OLD_DISCONNECTED,
} wifi_switch_phase_t;
static wifi_switch_phase_t s_switch_phase = WIFI_SWITCH_NONE;
static char s_switch_ssid[WIFI_SSID_MAXLEN];
static char s_switch_pwd[WIFI_PWD_MAXLEN];
static uint32_t s_switch_arm_token = 0U;
static uint32_t s_switch_wait_ms = 0U;
/* 未关联退出可能只有 STOP；已关联旧 STA 必须跨替换保留 DISCONNECT 屏障，即使中间已空 STA 重启。 */
static uint8_t s_switch_stop_eligible = 0U;
#define WIFI_SWITCH_WAIT_MS  12000U

/**
 * @brief 同步对象已建立时取得短状态互斥量。
 */
static void wifi_svc_lock(void)
{
    if (s_wifi_svc_mutex_ready != 0U) {
        os_mutex_pend(&s_wifi_svc_mutex, 0);
    }
}

/**
 * @brief 同步对象已建立时释放短状态互斥量。
 */
static void wifi_svc_unlock(void)
{
    if (s_wifi_svc_mutex_ready != 0U) {
        os_mutex_post(&s_wifi_svc_mutex);
    }
}

/**
 * @brief 取得连接、切网与忘记操作的顺序互斥量。
 * @note 锁顺序为 operation gate → connect_seq → cred_txn → svc。
 */
static void wifi_svc_connect_seq_lock(void)
{
    if (s_wifi_connect_seq_ready != 0U) {
        os_mutex_pend(&s_wifi_connect_seq_mutex, 0);
    }
}

/**
 * @brief 释放连接操作顺序互斥量。
 */
static void wifi_svc_connect_seq_unlock(void)
{
    if (s_wifi_connect_seq_ready != 0U) {
        os_mutex_post(&s_wifi_connect_seq_mutex);
    }
}

/**
 * @brief 等待并取得同步配置操作的独占执行占用。
 * @return 成功返回 D_OK；工作任务未就绪或被关闭返回 D_ERR_NOT_READY；等待失败返回 D_ERR_IO。
 * @note 等待前后均检查 enabled；未进入启动生命周期前不接触可能被清理的临时句柄。
 */
static d_err_t wifi_svc_operation_begin(void)
{
    uint8_t enabled;
    /* 首次成功初始化前，失败清理仍可能删除临时句柄；对象进入启动生命周期前不要访问。 */
    if (!s_request.worker_started || !s_wifi_operation_gate_ready) return D_ERR_NOT_READY;
    wifi_svc_lock();
    enabled = s_request.enabled;
    wifi_svc_unlock();
    if (!enabled) return D_ERR_NOT_READY;
    if (os_sem_pend(&s_wifi_operation_gate, 0) != 0) return D_ERR_IO;
    wifi_svc_lock();
    enabled = s_request.enabled;
    wifi_svc_unlock();
    if (!enabled) {
        os_sem_post(&s_wifi_operation_gate);
        return D_ERR_NOT_READY;
    }
    return D_OK;
}

/**
 * @brief 归还已取得的配置操作信号量占用。
 */
static void wifi_svc_operation_end(void)
{
    os_sem_post(&s_wifi_operation_gate);
}

/**
 * @brief 检查前台请求是否仍排队或执行中。
 * @return 活动返回非零。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static uint8_t wifi_service_request_active_locked(void)
{
    return s_request.snapshot.phase == WIFI_SERVICE_REQUEST_QUEUED ||
        s_request.snapshot.phase == WIFI_SERVICE_REQUEST_RUNNING;
}

/**
 * @brief 非阻塞受理前台配置请求，复制意图后交由 wifi_task 执行；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_request_submit(const wifi_service_request_t *request,
                                  uint32_t *out_id)
{
    if (!request || !out_id || request->kind <= WIFI_SERVICE_REQUEST_NONE ||
        request->kind > WIFI_SERVICE_REQUEST_FORGET) return D_ERR_INVALID_ARG;
    if (request->kind == WIFI_SERVICE_REQUEST_CONNECT &&
        (!request->ssid[0] || !memchr(request->ssid, 0, sizeof(request->ssid)) ||
         !memchr(request->pwd, 0, sizeof(request->pwd)))) return D_ERR_INVALID_ARG;
    if (!s_request.worker_started) return D_ERR_NOT_READY;
    wifi_svc_lock();
    if (!s_request.enabled || !s_request.worker_started || !s_request.radio_ready) {
        wifi_svc_unlock();
        return D_ERR_NOT_READY;
    }
    if (s_configuration_held) {
        wifi_svc_unlock();
        return D_ERR_BUSY;
    }
    if (request->kind == WIFI_SERVICE_REQUEST_SCAN &&
        s_request.snapshot.kind == WIFI_SERVICE_REQUEST_SCAN &&
        wifi_service_request_active_locked()) {
        *out_id = s_request.snapshot.request_id;
        wifi_svc_unlock();
        return D_OK;
    }
    /* 这里只尝试非阻塞取得执行占用；尝试期间持有者不会等待 svc，因此不引入反向阻塞锁顺序。 */
    if (wifi_service_request_active_locked() ||
        os_sem_accept(&s_wifi_operation_gate) != 0) {
        wifi_svc_unlock();
        return D_ERR_BUSY;
    }
    memset(&s_request.payload, 0, sizeof(s_request.payload));
    s_request.payload.kind = request->kind;
    if (request->kind == WIFI_SERVICE_REQUEST_CONNECT) {
        memcpy(s_request.payload.ssid, request->ssid, sizeof(request->ssid));
        memcpy(s_request.payload.pwd, request->pwd, sizeof(request->pwd));
    }
    if (++s_request.next_id == 0U) ++s_request.next_id;
    if (s_request.snapshot.completed_id == s_request.next_id) {
        s_request.snapshot.completed_id = 0U;
        s_request.snapshot.completed_kind = WIFI_SERVICE_REQUEST_NONE;
        s_request.snapshot.completed_ssid[0] = '\0';
    }
    s_request.snapshot.request_id = s_request.next_id;
    s_request.snapshot.kind = request->kind;
    s_request.snapshot.phase = WIFI_SERVICE_REQUEST_QUEUED;
    s_request.snapshot.revision++;
    *out_id = s_request.next_id;
    wifi_svc_unlock();
    /* 通知失败不撤销已经受理的请求；工作任务有限等待后仍会发现自有请求槽。 */
    if (os_sem_post(&s_wifi_worker_wake) != 0) LOGW(TAG, "request wake deferred");
    return D_OK;
}

/**
 * @brief 读取仅含值、不含密码的配置请求快照；完整合同见 wifi_service.h。
 */
void wifi_service_get_request_snapshot(wifi_service_request_snapshot_t *snapshot)
{
    if (!snapshot) return;
    if (!s_request.worker_started) {
        memset(snapshot, 0, sizeof(*snapshot));
        return;
    }
    wifi_svc_lock();
    *snapshot = s_request.snapshot;
    wifi_svc_unlock();
}

/**
 * @brief 发布当前请求完成编号、种类与结果并清空密码载荷。
 * @param[in] result 执行器返回结果，不等同 DHCP 结果。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static void wifi_service_request_complete_locked(d_err_t result)
{
    s_request.snapshot.completed_id = s_request.snapshot.request_id;
    s_request.snapshot.completed_kind = s_request.snapshot.kind;
    s_request.snapshot.completed_result = result;
    s_request.snapshot.completed_ssid[0] = '\0';
    if (s_request.snapshot.kind == WIFI_SERVICE_REQUEST_CONNECT) {
        memcpy(s_request.snapshot.completed_ssid, s_request.payload.ssid,
               sizeof(s_request.snapshot.completed_ssid));
    } else if (s_request.snapshot.kind == WIFI_SERVICE_REQUEST_RECONNECT && result == D_OK) {
        memcpy(s_request.snapshot.completed_ssid, s_wifi_status.ssid,
               sizeof(s_request.snapshot.completed_ssid));
    }
    memset(&s_request.payload, 0, sizeof(s_request.payload));
    s_request.snapshot.phase = WIFI_SERVICE_REQUEST_COMPLETE;
    s_request.snapshot.revision++;
}

/**
 * @brief 只结算仍匹配且执行中的请求，并归还执行占用。
 * @param[in] id 当前工作任务捕获的请求编号。
 * @param[in] result 执行器结果。
 */
static void wifi_service_request_finish(uint32_t id, d_err_t result)
{
    uint8_t release = 0U;
    wifi_svc_lock();
    if (s_request.snapshot.request_id == id &&
        s_request.snapshot.phase == WIFI_SERVICE_REQUEST_RUNNING) {
        wifi_service_request_complete_locked(result);
        release = 1U;
    }
    wifi_svc_unlock();
    if (release) wifi_svc_operation_end();
}

/**
 * @brief 关闭前台受理并以 NOT_READY 结算排队请求。
 * @note 执行中请求保留占用至真实返回，不销毁同步对象。
 */
static void wifi_service_request_close(void)
{
    uint8_t release = 0U;
    if (!s_request.worker_started) return;
    wifi_svc_lock();
    s_request.enabled = 0U;
    if (s_request.snapshot.phase == WIFI_SERVICE_REQUEST_QUEUED) {
        wifi_service_request_complete_locked(D_ERR_NOT_READY);
        release = 1U;
    }
    wifi_svc_unlock();
    if (release) wifi_svc_operation_end();
}

/**
 * @brief 读取用于前端快速拒绝的配置屏障视图；完整合同见 wifi_service.h。
 */
uint8_t wifi_service_configuration_is_held(void)
{
    uint8_t held;
    wifi_svc_lock();
    held = s_configuration_held;
    wifi_svc_unlock();
    return held;
}

/**
 * @brief 取得连接序列锁并拒绝已经冻结的配置。
 * @return 成功返回 D_OK 且持有 connect_seq；冻结返回 D_ERR_BUSY 并已解锁。
 */
static d_err_t wifi_svc_configuration_lock(void)
{
    wifi_svc_connect_seq_lock();
    if (wifi_service_configuration_is_held()) {
        wifi_svc_connect_seq_unlock();
        return D_ERR_BUSY;
    }
    return D_OK;
}

/**
 * @brief 非阻塞取得操作、连接与存盘屏障后冻结配置。
 * @param[in] require_connected 非零还要求已发布 CONNECTED。
 * @return 取得返回 D_OK；基础对象未就绪返回 D_ERR_NOT_READY；任一占用或待存凭据冲突返回 D_ERR_BUSY。
 * @note 所有尝试锁按既定顺序执行，失败释放已取得的临时占用。
 */
static d_err_t wifi_service_hold_configuration(uint8_t require_connected)
{
    d_err_t result = D_ERR_BUSY;
    if (!s_request.worker_started || !s_wifi_connect_seq_ready || !s_wifi_cred_txn_ready || !s_wifi_svc_mutex_ready ||
        !s_wifi_operation_gate_ready)
        return D_ERR_NOT_READY;
    /* 维护准入不等待 SDK 调用或 VM 写入。 */
    if (os_sem_accept(&s_wifi_operation_gate) != 0) return D_ERR_BUSY;
    if (os_mutex_accept(&s_wifi_connect_seq_mutex) != 0) {
        wifi_svc_operation_end();
        return D_ERR_BUSY;
    }
    if (os_mutex_accept(&s_wifi_cred_txn_mutex) != 0) {
        wifi_svc_connect_seq_unlock();
        wifi_svc_operation_end();
        return D_ERR_BUSY;
    }
    wifi_svc_lock();
    /* CONNECTED 在 DHCP 凭据事务完成后发布；保留 pending 也会阻止仍可能写 VM 的失败重试与维护并行。 */
    if (!s_configuration_held && s_switch_phase == WIFI_SWITCH_NONE &&
        (!require_connected || s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED) &&
        !s_pending_ssid[0]) {
        s_configuration_held = 1U;
        result = D_OK;
    }
    wifi_svc_unlock();
    os_mutex_post(&s_wifi_cred_txn_mutex);
    wifi_svc_connect_seq_unlock();
    wifi_svc_operation_end();
    return result;
}

/**
 * @brief 在不停止现有网络的前提下，非阻塞冻结配置和凭据写入；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_configuration_hold_begin(void)
{
    return wifi_service_hold_configuration(1U);
}


/**
 * @brief 解除由成功 begin 取得的单协调者配置屏障；完整合同见 wifi_service.h。
 */
void wifi_service_configuration_hold_end(void)
{
    wifi_svc_connect_seq_lock();
    wifi_svc_lock();
    s_configuration_held = 0U;
    wifi_svc_unlock();
    wifi_svc_connect_seq_unlock();
}

/**
 * @brief 取得 VM 24/25 凭据事务互斥量。
 */
static void wifi_svc_cred_txn_lock(void)
{
    if (s_wifi_cred_txn_ready != 0U) {
        os_mutex_pend(&s_wifi_cred_txn_mutex, 0);
    }
}

/**
 * @brief 释放凭据事务互斥量。
 */
static void wifi_svc_cred_txn_unlock(void)
{
    if (s_wifi_cred_txn_ready != 0U) {
        os_mutex_post(&s_wifi_cred_txn_mutex);
    }
}

/**
 * @brief 清除延迟切网阶段、票据与目标凭据。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static void wifi_svc_switch_cancel_locked(void)
{
    s_switch_phase = WIFI_SWITCH_NONE;
    s_switch_arm_token = 0U;
    s_switch_wait_ms = 0U;
    s_switch_stop_eligible = 0U;
    memset(s_switch_ssid, 0, sizeof(s_switch_ssid));
    memset(s_switch_pwd, 0, sizeof(s_switch_pwd));
}

/**
 * @brief 核对非零尝试编号同时匹配活动编号与最新代次。
 * @param[in] token 待核对的尝试编号。
 * @return 仍有效返回 1，否则为 0。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static uint8_t wifi_svc_attempt_live_locked(uint32_t token)
{
    return ((token != 0U) &&
            (token == s_active_connect_attempt) &&
            (token == s_connect_attempt_id)) ? 1U : 0U;
}

/**
 * @brief 按 SDK syscfg 合同判断读写结果是否大于零。
 * @param[in] ret syscfg_read 或 syscfg_write 返回值。
 * @return 大于零返回 1，否则为 0。
 */
static int wifi_syscfg_ok(int ret)
{
    return (ret > 0) ? 1 : 0;
}

/**
 * @brief 推进非零凭据代次，使旧异步保存失效。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static void wifi_svc_bump_cred_epoch_locked(void)
{
    s_cred_epoch++;
    if (s_cred_epoch == 0U) {
        s_cred_epoch = 1U;
    }
}

/**
 * @brief 推进非零待保存内容代次。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static void wifi_svc_bump_pending_gen_locked(void)
{
    s_pending_gen++;
    if (s_pending_gen == 0U) {
        s_pending_gen = 1U;
    }
}

/**
 * @brief 撤销活动及已进入 SDK 的尝试编号，并推进非零尝试代次。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
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

/**
 * @brief 核对凭据代次、待保存代次、SSID 与密码均未被替换。
 * @param[in] cred_epoch 捕获的凭据代次。
 * @param[in] pending_gen 捕获的待保存代次。
 * @param[in] ssid 捕获的 SSID。
 * @param[in] pwd 捕获的密码，为空按空串比较。
 * @return 全部匹配且 SSID 非空返回 1，否则为 0。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
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

/**
 * @brief 核对 SDK 当前 SSID 与非空预期目标。
 * @param[in] expected 预期目标 SSID。
 * @return 明确相同或 SDK 尚未知名称返回 1；预期为空或明确不同返回 0。
 * @note 未知不等同已证明匹配，但只对明确不匹配做拒绝。
 */
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
        /* SDK 尚未知 SSID 时允许继续，仅对明确不匹配做拒绝。 */
        return 1U;
    }
    return (strncmp(expected, info.ssid, WIFI_SSID_MAXLEN) == 0) ? 1U : 0U;
}

/**
 * @brief 状态变化时更新扫描阶段及版本。
 * @param[in] state 新扫描阶段。
 */
static void wifi_scan_set_state(wifi_service_scan_state_t state)
{
    wifi_svc_lock();
    if (s_scan_state != state) {
        s_scan_state = state;
        s_scan_version++;
    }
    wifi_svc_unlock();
}

/**
 * @brief 根据首字节判断 UTF-8 序列长度。
 * @param[in] ch 待检查字节。
 * @return 合法首字节返回 1～4，否则返回 0。
 */
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

/**
 * @brief 核对多字节 UTF-8 续字节及码点边界。
 * @param[in] src 输入 SSID 字节。
 * @param[in] src_len 有效输入字节数。
 * @param[in] pos 序列起点。
 * @param[in] seq_len 由首字节确定的多字节长度。
 * @return 合法完整序列返回 1，否则为 0。
 */
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

/**
 * @brief 有界生成可显示 SSID，保留有效 UTF-8 并替换不可解码字节。
 * @param[out] dst 零结尾显示名输出。
 * @param[in] dst_size 缓冲容量，包含末尾零。
 * @param[in] src 原始 SSID 字节。
 * @param[in] src_len 有效输入长度。
 */
static void wifi_scan_copy_ssid(char *dst, uint8_t dst_size,
                                const char *src, uint8_t src_len)
{
    uint8_t in_pos = 0;
    uint8_t out_pos = 0;

    if ((dst == NULL) || (dst_size == 0U) || (src == NULL)) {
        return;
    }

    /* SSID 是原始字节；保留有效 UTF-8，替换 LVGL 无法解码的字节。 */
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

/**
 * @brief 把 -90 至 -50 dBm 线性映射到信号百分比，并截断端点。
 * @param[in] rssi SDK RSSI dBm。
 * @return 0～100 的显示信号强度。
 */
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

/**
 * @brief 只保留 1～14 的有效显示信道。
 * @param[in] channel SDK 原始信道。
 * @return 有效信道值或未知值 0。
 */
static uint8_t wifi_scan_clamp_channel(char channel)
{
    if ((channel <= 0) || ((uint8_t)channel > WIFI_SCAN_CHANNEL_MAX)) {
        return 0U;
    }
    return (uint8_t)channel;
}

/**
 * @brief 先按显示信号强度，再按 RSSI 比较两个扫描项。
 * @param[in] left 待比较项。
 * @param[in] right 参考项。
 * @return left 更强返回非零。
 */
static int wifi_scan_better(const wifi_service_scan_result_t *left,
                            const wifi_service_scan_result_t *right)
{
    if (left->signal_strength != right->signal_strength) {
        return left->signal_strength > right->signal_strength;
    }
    return left->rssi > right->rssi;
}

/**
 * @brief 按显示 SSID 去重并保留容量内最强扫描项。
 * @param[in] src SDK 扫描项，空项或空 SSID 不保存。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
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

/**
 * @brief 按信号强度降序排列当前缓存。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
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

/**
 * @brief 复制 SDK 扫描列表到本地缓存，再按 SDK 合同清理并释放原列表。
 */
static void wifi_scan_cache_finish(void)
{
    struct wifi_scan_ssid_info *ssid_info;
    u32 ssid_count = 0;
    u32 i;

    ssid_info = wifi_get_scan_result(&ssid_count);
    wifi_svc_lock();
    memset(s_scan_results, 0, sizeof(s_scan_results));
    s_scan_result_count = 0;
    if (ssid_info != NULL) {
        for (i = 0; i < ssid_count; i++) {
            wifi_scan_cache_store(&ssid_info[i]);
        }
        wifi_scan_cache_sort();
    }
    wifi_svc_unlock();

    /* SDK 要求取完扫描结果后清理内部缓存，再释放返回 buffer。 */
    wifi_clear_scan_result();
    if (ssid_info != NULL) {
        free(ssid_info);
    }
}

/**
 * @brief 在无线开启时请求或观察 SDK 当前扫描。
 * @return 无线关闭返回 D_ERR_NOT_READY；请求或已有 SDK 扫描均返回 D_OK。
 * @note 调用者持有 connect_seq；SDK 扫描忙仍允许后续观察结果。
 */
static d_err_t wifi_scan_request_start(void)
{
    /* 调用者持有 connect_seq。SDK 返回 -1 时可能已有扫描或关联可提供结果，连接元数据不能阻止观察它。 */
    if (!wifi_is_on()) {
        wifi_svc_lock();
        s_scan_completed = 0;
        wifi_svc_unlock();
        wifi_scan_set_state(WIFI_SERVICE_SCAN_NOT_READY);
        return D_ERR_NOT_READY;
    }
    wifi_svc_lock();
    if (s_scan_completed != 0U) { /* 观察已经完成的 SDK 扫描。 */
        wifi_svc_unlock();
        wifi_scan_set_state(WIFI_SERVICE_SCAN_BUSY);
        return D_OK;
    }
    wifi_svc_unlock();
    if (wifi_scan_req() != 0) {
        wifi_scan_set_state(WIFI_SERVICE_SCAN_BUSY);
        return D_OK;
    }

    wifi_scan_set_state(WIFI_SERVICE_SCAN_SCANNING);
    return D_OK;
}

/**
 * @brief 收集扫描缓存、清除本轮预算并发布 DONE。
 */
static void wifi_scan_complete_current(void)
{
    wifi_scan_cache_finish();
    wifi_svc_lock();
    s_scan_elapsed_ms = 0;
    s_scan_deferred_elapsed = 0U;
    wifi_svc_unlock();
    wifi_scan_set_state(WIFI_SERVICE_SCAN_DONE);
    LOGI(TAG, "scan results ready");
}

/**
 * @brief 读取 VM 24/25，并验证 SSID 与密码槽可用。
 * @param[out] ssid SSID 输出，失败时清为空串。
 * @param[in] ssid_sz 正数 SSID 缓冲容量。
 * @param[out] pwd 密码输出，失败时清为空串。
 * @param[in] pwd_sz 正数密码缓冲容量。
 * @return 有效凭据返回 1，否则返回 0 并清空两个输出。
 * @note 调用者持有 cred_txn；输出容量须大于零。
 */
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

/**
 * @brief 顺序保存 SSID 与密码，密码写失败时尽力恢复旧内容。
 * @param[in] ssid 非空且非空串 SSID。
 * @param[in] pwd 可空密码，为空按空串保存。
 * @return 两槽完整写入返回 0；任一步失败返回 -1。
 * @note 调用者持有 cred_txn；失败或回滚尝试不证明 VM 已恢复。
 */
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
    if (r_ssid != (int)ssid_len) {
        LOGE(TAG, "WiFi 凭证写入失败: ssid_ret=%d", r_ssid);
        return -1;
    }
    r_pwd = syscfg_write(WIFI_VM_PWD, (void *)(pwd ? pwd : ""), pwd_len);
    if (r_pwd != (int)pwd_len) {
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

/**
 * @brief 清空应用凭据槽，第二槽失败时尽力恢复旧内容。
 * @return 两槽写入满足 SDK 成功条件返回 0；失败返回 -1。
 * @note 调用者持有 cred_txn；失败不能证明凭据已不存在。
 */
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

/**
 * @brief 按事务结果发布已保存凭据存在性，失败时重新读取确认。
 * @param[in] result 原始保存或清除结果，0 为成功。
 * @param[in] success_presence 成功时应发布的存在性。
 * @note 调用者持有 cred_txn；内部短暂取得 svc，不在 svc 内执行 VM。
 */
static void wifi_cred_publish_result_locked(int result, uint8_t success_presence)
{
    uint8_t presence = success_presence != 0U;

    if (result != 0) {
        char ssid[WIFI_SSID_MAXLEN];
        char pwd[WIFI_PWD_MAXLEN];
        presence = wifi_cred_load_raw(ssid, sizeof(ssid), pwd, sizeof(pwd)) != 0;
    }
    wifi_svc_lock();
    if (s_wifi_status.has_saved_credentials != presence) {
        s_wifi_status.has_saved_credentials = presence;
        s_wifi_status_version++;
    }
    wifi_svc_unlock();
}

/**
 * @brief 在凭据事务锁内读取两槽。
 * @param[out] ssid SSID 输出，失败时清为空串。
 * @param[in] ssid_sz 正数 SSID 缓冲容量。
 * @param[out] pwd 密码输出，失败时清为空串。
 * @param[in] pwd_sz 正数密码缓冲容量。
 * @return 有效凭据返回 1，否则返回 0。
 */
static int wifi_cred_load(char *ssid, int ssid_sz, char *pwd, int pwd_sz)
{
    int ret;

    wifi_svc_cred_txn_lock();
    ret = wifi_cred_load_raw(ssid, ssid_sz, pwd, pwd_sz);
    wifi_svc_cred_txn_unlock();
    return ret;
}

/**
 * @brief 在事务锁内保存两槽并发布存在性。
 * @param[in] ssid 非空目标 SSID。
 * @param[in] pwd 可空密码。
 * @return 成功返回 0，失败返回 -1。
 */
static int wifi_cred_save(const char *ssid, const char *pwd)
{
    int ret;

    wifi_svc_cred_txn_lock();
    ret = wifi_cred_save_raw(ssid, pwd);
    wifi_cred_publish_result_locked(ret, 1U);
    wifi_svc_cred_txn_unlock();
    return ret;
}

/**
 * @brief 在事务锁内清空两槽并发布真实存在性。
 * @return 成功返回 0，失败返回 -1。
 */
static int wifi_cred_clear(void)
{
    int ret;

    wifi_svc_cred_txn_lock();
    ret = wifi_cred_clear_raw();
    wifi_cred_publish_result_locked(ret, 0U);
    wifi_svc_cred_txn_unlock();
    return ret;
}

/**
 * @brief 关闭最佳网络选择，清理扫描并以空 STA 请求撤销当前关联。
 * @note 调用者按 connect_seq 序列化；退出事实仍由 SDK 事件确认。
 */
static void wifi_service_abort_sdk_connect(void)
{
    wifi_set_sta_connect_best_ssid(0);
    wifi_clear_scan_result();
    wifi_enter_sta_mode("", "");
}

/* 前向声明，供目标认领及失败回退路径使用。 */
static void wifi_sdk_apply_empty_sta_default(void);
static void wifi_sdk_clear_fast_connect_cache(void);
static void wifi_sdk_clear_pmk_cache(void);

/**
 * @brief 将本次显式目标写入 SDK 默认与最后模式，避免遗留空默认打断新连接。
 * @param[in] ssid 非空、非空串目标。
 * @param[in] pwd 可空密码。
 * @note 每次连接只调用一次；应用 VM 凭据仍只在 DHCP 成功后保存。
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
    /* 索引与启动路径一致：ssid[mode - STA_MODE]。 */
    slot = (u8)(def.mode - STA_MODE);
    strncpy((char *)def.ssid[slot], ssid_buf, sizeof(def.ssid[slot]) - 1U);
    strncpy((char *)def.pwd[slot], pwd_buf, sizeof(def.pwd[slot]) - 1U);
    /* force=1、store=1 替换忘记后遗留的空默认，防止 OID connect default 再进入空 STA；store_mode_info 同步最后模式。 */
    (void)wifi_set_default_mode(&def, 1, 1);
    (void)wifi_store_mode_info(STA_MODE, ssid_buf, pwd_buf);
    wifi_set_sta_connect_best_ssid(0);
    LOGI(TAG, "SDK STA target claimed: ssid=%s", ssid_buf);
}

/**
 * @brief 连接失败后清理快速缓存，并从应用 VM 恢复 SDK 默认；无凭据则置空。
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

/**
 * @brief 清空待保存凭据并推进待保存代次。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static void wifi_service_clear_pending(void)
{
    memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
    memset(s_pending_pwd, 0, sizeof(s_pending_pwd));
    wifi_svc_bump_pending_gen_locked();
}

/**
 * @brief 通过受序列化的 VM 读取检查应用凭据。
 * @return 有效凭据存在返回 1，否则为 0。
 */
static uint8_t wifi_service_has_saved_credentials(void)
{
    char ssid[WIFI_SSID_MAXLEN];
    char pwd[WIFI_PWD_MAXLEN];

    return wifi_cred_load(ssid, sizeof(ssid), pwd, sizeof(pwd)) ? 1U : 0U;
}

/**
 * @brief 只在内存中更新连接元数据及完整状态版本。
 * @param[in] state 目标连接阶段。
 * @param[in] error 业务错误。
 * @param[in] ssid 可空；非空时替换快照中的 SSID。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
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

/**
 * @brief 自行加锁更新连接元数据。
 * @param[in] state 目标连接阶段。
 * @param[in] error 业务错误。
 * @param[in] ssid 可空；非空时替换快照中的 SSID。
 */
static void wifi_service_status_set(wifi_service_conn_state_t state,
                                    wifi_service_conn_error_t error,
                                    const char *ssid)
{
    wifi_svc_lock();
    wifi_service_status_set_locked(state, error, ssid);
    wifi_svc_unlock();
}

/**
 * @brief 分配新非零尝试编号并发布 CONNECTING，等待真正进入 SDK 后绑定。
 * @param[in] ssid 可空；非空时复制到持久连接参数。
 * @return 新活动尝试编号。
 * @note 调用者持有 svc 状态锁；只更新内存元数据。
 */
static uint32_t wifi_service_arm_connect_attempt_locked(const char *ssid)
{
    s_connect_attempt_id++;
    if (s_connect_attempt_id == 0U) {
        s_connect_attempt_id = 1U;
    }
    s_active_connect_attempt = s_connect_attempt_id;
    /* 失败与 DHCP 终态等待 enter_sta_mode 绑定 s_enter_attempt 后才处理。 */
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

/**
 * @brief 在状态锁内为预期接收 STA 或 DHCP 事件的路径建立尝试。
 * @param[in] ssid 预期目标 SSID。
 * @return 新非零尝试编号。
 */
static uint32_t wifi_service_arm_connect_attempt(const char *ssid)
{
    uint32_t token;

    wifi_svc_lock();
    token = wifi_service_arm_connect_attempt_locked(ssid);
    wifi_svc_unlock();
    return token;
}

/**
 * @brief 只对真实观察到的保留目标关联接纳 SDK 自主重连。
 * @param[in] require_dhcp 非零要求 DHCP 已成功；0 也接受仅关联成功。
 * @return 接纳并建立进入编号返回 1；不满足或竞争失效返回 0。
 * @note 不主动启动 SDK 新连接，重查目标与尝试代次后才发布。
 */
static uint8_t wifi_service_bind_sdk_reconnect(uint8_t require_dhcp)
{
    struct wifi_mode_info info;
    enum wifi_sta_connect_state sdk_state;
    char expected[WIFI_SSID_MAXLEN];
    uint32_t observed_attempt;
    uint32_t token;

    wifi_svc_lock();
    if (s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED ||
        s_active_connect_attempt != 0U || s_enter_attempt != 0U ||
        s_switch_phase != WIFI_SWITCH_NONE || s_connect_ssid[0] == '\0' ||
        (s_pending_ssid[0] != '\0' &&
         strncmp(s_pending_ssid, s_connect_ssid, sizeof(s_pending_ssid)) != 0)) {
        wifi_svc_unlock();
        return 0U;
    }
    observed_attempt = s_connect_attempt_id;
    memcpy(expected, s_connect_ssid, sizeof(expected));
    wifi_svc_unlock();

    if (!wifi_is_on()) return 0U;
    sdk_state = wifi_get_sta_connect_state();
    if (sdk_state != WIFI_STA_NETWORK_STACK_DHCP_SUCC &&
        (require_dhcp != 0U || sdk_state != WIFI_STA_CONNECT_SUCC)) return 0U;
    memset(&info, 0, sizeof(info));
    info.mode = STA_MODE;
    wifi_get_mode_cur_info(&info);
    if (info.ssid == NULL || info.ssid[0] == '\0' ||
        strncmp(info.ssid, expected, sizeof(expected)) != 0) return 0U;

    wifi_svc_lock();
    if (observed_attempt != s_connect_attempt_id ||
        s_active_connect_attempt != 0U || s_enter_attempt != 0U ||
        s_switch_phase != WIFI_SWITCH_NONE ||
        s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED ||
        strncmp(s_connect_ssid, expected, sizeof(expected)) != 0) {
        wifi_svc_unlock();
        return 0U;
    }
    token = wifi_service_arm_connect_attempt_locked(expected);
    s_enter_attempt = token;
    wifi_svc_unlock();
    LOGI(TAG, "SDK reconnect adopted: attempt=%u", (unsigned)token);
    return 1U;
}

/**
 * @brief 旧 STA 真实退出后核对切网票据，再进入新目标。
 * @note 调用者持有 connect_seq；成功或失效均清除私有切网阶段，不用 SDK 空闲快照猜测退出。
 */
static void wifi_service_switch_enter_new(void)
{
    uint32_t arm_token;
    char local_ssid[WIFI_SSID_MAXLEN];
    char local_pwd[WIFI_PWD_MAXLEN];

    memset(local_ssid, 0, sizeof(local_ssid));
    memset(local_pwd, 0, sizeof(local_pwd));

    wifi_svc_lock();
    if (s_switch_phase != WIFI_SWITCH_OLD_DISCONNECTED) {
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

/**
 * @brief 受检提交连接目标，必要时先等待旧关联退出再进入。
 * @param[in] ssid 非空、非空串目标。
 * @param[in] pwd 可空密码。
 * @param[in] save_on_success 非零在 DHCP 成功后保存到应用 VM。
 * @param[in] allow_override 非零允许替换旧尝试。
 * @return 目标受理返回 D_OK；无效目标、无线未就绪或配置冲突返回相应错误。
 * @note 内部取得 connect_seq；D_OK 不代表关联或 DHCP 完成。
 */
static d_err_t wifi_service_start_connect(const char *ssid, const char *pwd,
                                          uint8_t save_on_success,
                                          uint8_t allow_override)
{
    uint8_t need_leave_old = 0U;
    uint8_t defer_enter = 0U;
    uint8_t stop_eligible;
    uint32_t arm_token = 0U;
    enum wifi_sta_connect_state sdk_state;
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

    /* 服务元数据可能晚于 SDK 自主重连；重新进入前，现有切网握手也必须接管该真实旧连接。 */
    sdk_state = wifi_get_sta_connect_state();
    wifi_svc_lock();
    stop_eligible = (s_wifi_status.state != WIFI_SERVICE_CONN_ASSOCIATED &&
                     s_wifi_status.state != WIFI_SERVICE_CONN_CONNECTED &&
                     sdk_state != WIFI_STA_CONNECT_SUCC &&
                     sdk_state != WIFI_STA_NETWORK_STACK_DHCP_SUCC &&
                     sdk_state != WIFI_STA_NETWORK_STACK_DHCP_TIMEOUT);
    if (s_switch_phase == WIFI_SWITCH_WAIT_OLD_DISCONNECT) {
        stop_eligible = s_switch_stop_eligible;
    }
    /* 单飞连接：覆盖时先失效旧进入编号，使退出中的迟到 ASSOC_FAIL 看到 enter_attempt==0。
 * CONNECTED 或忙碌退出均异步等待，不在 UI 路径 os_time_dly。 */
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
        /* 工作任务等待 connect_seq 时回调可能已确认旧链路退出，不能再等待第二次退出。 */
        need_leave_old = s_switch_phase != WIFI_SWITCH_OLD_DISCONNECTED;
        defer_enter = need_leave_old;
        wifi_svc_switch_cancel_locked();
        wifi_svc_invalidate_attempt_locked();
    } else if (s_wifi_status.state == WIFI_SERVICE_CONN_CONNECTED ||
               sdk_state == WIFI_STA_CONNECT_SUCC ||
               sdk_state == WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        LOGW(TAG, "WiFi connect switch from CONNECTED: ssid=%s", ssid);
        need_leave_old = 1U;
        defer_enter = 1U;
    }

    /* 在同一状态临界区提交连接参数、pending 与新尝试编号。 */
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
        s_switch_stop_eligible = stop_eligible;
        s_switch_phase = WIFI_SWITCH_WAIT_OLD_DISCONNECT;
    }
    wifi_svc_unlock();

    if (need_leave_old != 0U) {
        /* 这里只撤销旧 STA，不轮询 SDK 状态后直接进入新目标。SDK 可能先报告 DISCONNECT，
 * 但 STA_DISCONNECT 事件仍在途；过早进入会清掉切网状态，使迟到事件取消新尝试。
 * 真实 STA_DISCONNECT 才推进；仅未关联且符合条件的尝试可接受 STA_STOP。
 * 已关联旧 STA 保留断连屏障，因为 STOP 不排空 WPA 异步事件。 */
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

/**
 * @brief 最多三轮删除 SDK 记忆中的 SSID，避免删除后列表重排遗留项。
 */
static void wifi_sdk_clear_stored_sta_info(void)
{
    struct wifi_stored_sta_info stored_info[NETWORK_SSID_INFO_CNT];
    int count;
    int i;
    int pass;

    /* SDK 可能保存多个 SSID；分几轮删除，避免列表重排后留下自动回连项。 */
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

/**
 * @brief 持久化空 STA 默认与最后模式，关闭最佳网络自动回连。
 */
static void wifi_sdk_apply_empty_sta_default(void)
{
    struct wifi_store_info default_info;

    memset(&default_info, 0, sizeof(default_info));
    default_info.mode = STA_MODE;
    default_info.connect_best_network = 0;
    /* force=1、store=1 将空配置持久化为默认与最后模式。 */
    wifi_set_default_mode(&default_info, 1, 1);
    (void)wifi_store_mode_info(STA_MODE, "", "");
    wifi_set_sta_connect_best_ssid(0);
    LOGI(TAG, "SDK empty STA default applied (stored)");
}

/**
 * @brief 按 SDK pmk_info 布局清空 PMK 缓存槽。
 */
static void wifi_sdk_clear_pmk_cache(void)
{
    u8 empty[WIFI_SDK_PMK_CACHE_LEN] = {0};

    /* WIFI_PMK_INFO 长度来自 SDK wifi_conf.c:
     * struct pmk_info { u32 crc; char pmk[32]; }。 */
    syscfg_write(WIFI_PMK_INFO, empty, (u16)sizeof(empty));
}

/**
 * @brief 清理 SDK 扫描与 PMK 快速连接缓存。
 */
static void wifi_sdk_clear_fast_connect_cache(void)
{
    wifi_clear_scan_result();
    wifi_sdk_clear_pmk_cache();
    LOGI(TAG, "SDK PMK cache cleared");
}

/**
 * @brief 必要时以空 STA 撤销关联，并持久化空默认以保持空闲。
 * @note 已空闲时不重复重启 STA，避免空 SSID 扫描抖动。
 */
static void wifi_sdk_leave_network_idle(void)
{
    enum wifi_sta_connect_state sta_st;

    wifi_set_sta_connect_best_ssid(0);
    wifi_clear_scan_result();
    /* 空 enter 会重启 STA 并清除关联；已空闲时跳过，避免忘记后反复 SCAN-FAILED 或空 SSID 扫描。 */
    sta_st = wifi_get_sta_connect_state();
    if (sta_st != WIFI_STA_DISCONNECT) {
        wifi_enter_sta_mode("", "");
    }
    wifi_sdk_apply_empty_sta_default();
    wifi_clear_scan_result();
}

/**
 * @brief 先失效意图，再清空 VM，成功后清理 SDK 凭据并发布断开。
 * @param[in] take_seq 非零自行取得配置序列锁；0 表示调用者已持有。
 * @return 成功返回 D_OK；屏障冲突或 VM 清除失败返回对应错误。
 * @note VM 失败仍推进凭据代次并清空 pending，但不伪造 DISCONNECTED。
 */
static d_err_t wifi_service_clear_saved_credentials_inner(uint8_t take_seq)
{
    /* 1）取消切网、推进凭据代次、清 pending、失效尝试并清目标。
 * 2）在 cred_txn 中清 VM。
 * 3）仅 VM 成功后发布 DISCONNECTED，使退出事件不再改变状态。
 * 第二步失败不伪造断开，调用者需重试忘记。 */
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

/**
 * @brief 清除应用 VM 24/25 凭据，并清理 SDK 记忆与快速连接缓存；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_clear_saved_credentials(void)
{
    d_err_t result = wifi_svc_operation_begin();
    if (result != D_OK) return result;
    result = wifi_service_clear_saved_credentials_inner(1U);
    wifi_svc_operation_end();
    return result;
}

/**
 * @brief 撤销连接意图，关闭自动回连并请求无线空闲。
 * @param[in] take_seq 非零自行取得配置序列锁；0 表示调用者已持有。
 * @return 请求处理成功返回 D_OK；自行取锁时可能返回 D_ERR_BUSY。
 * @note 不删除应用 VM 凭据；先发布断开以忽略本次主动退出的迟到事件。
 */
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
    /* clear_saved 已移除意图时不重复推进代次。先发布 DISCONNECTED，
 * 让主动退出的 STA_DISCONNECT 被忽略，无需压制计数。 */
    if (already_down == 0U || s_connect_ssid[0] != '\0' || s_pending_ssid[0] != '\0') {
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
        /* 对活动关联请求一次空 STA 退出。 */
        wifi_sdk_leave_network_idle();
    } else {
        /* 服务已经 DISCONNECTED，避免重复重启模块造成抖动。 */
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

/**
 * @brief 撤销当前连接意图并请求 STA 离网，保留应用 VM 凭据；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_disconnect_for_reconfig(void)
{
    d_err_t result = wifi_svc_operation_begin();
    if (result != D_OK) return result;
    result = wifi_service_disconnect_for_reconfig_inner(1U);
    wifi_svc_operation_end();
    return result;
}

//*********************************************************************************//
//                              STA 连接（供外部事件调用）                          //
//*********************************************************************************//

/**
 * @brief 取得操作占用后同步提交连接序列，再归还占用。
 * @param[in] ssid 目标 SSID。
 * @param[in] pwd 可空密码。
 * @param[in] save 是否在 DHCP 后保存。
 * @param[in] override 是否允许替换尝试。
 * @return 连接提交或操作准入的实际结果。
 */
static d_err_t wifi_service_connect_sync(const char *ssid, const char *pwd,
                                       uint8_t save, uint8_t override)
{
    d_err_t result = wifi_svc_operation_begin();
    if (result != D_OK) return result;
    result = wifi_service_start_connect(ssid, pwd, save, override);
    wifi_svc_operation_end();
    return result;
}

/**
 * @brief 兼容入口提交可替换旧尝试的 STA 连接，DHCP 成功后才保存凭据；完整合同见 wifi_service.h。
 */
void wifi_service_sta_connect(const char *ssid, const char *pwd)
{
    LOGI(TAG, "STA connect: ssid=%s", ssid ? ssid : "");
    (void)wifi_service_connect_sync(ssid, pwd, 1U, 1U);
}

/**
 * @brief 为 SDK config_network 等保留的兼容连接入口。
 * @param[in] ssid 目标 SSID。
 * @param[in] pwd 可空密码。
 * @param[in] save 非零在成功 DHCP 后保存凭据。
 * @note 忽略提交错误；状态仍经服务事件发布。
 */
void wifi_sta_connect(char *ssid, char *pwd, char save)
{
    LOGI(TAG, "wifi_sta_connect: ssid=%s, save=%d", ssid ? ssid : "", save);
    (void)wifi_service_connect_sync(ssid, pwd, (save != 0) ? 1U : 0U, 1U);
}

//*********************************************************************************//
//                              LAN 配置                                            //
//*********************************************************************************//

/**
 * @brief 为 AP 模式配置编译固定的局域网地址及 DHCP 地址。
 */
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

/**
 * @brief 在 STA 构建中按 SDK 记忆或编译默认配置重新入网。
 * @note 调用者已经持有操作占用；内部序列化连接并绑定进入编号。
 */
static void wifi_service_return_sta_mode_execute(void)
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
    /* 必须先分配尝试编号，避免 DHCP 或 STA_SUCC 被当作旧事件丢弃。 */
    wifi_service_arm_connect_attempt(info.ssid);
    wifi_svc_lock();
    s_enter_attempt = s_active_connect_attempt;
    wifi_svc_unlock();
    wifi_enter_sta_mode(info.ssid, info.pwd);
    wifi_svc_connect_seq_unlock();
#endif
}

/**
 * @brief 兼容入口按 SDK 已存模式或编译默认值请求 STA 重连；完整合同见 wifi_service.h。
 */
void wifi_service_return_sta_mode(void)
{
    if (wifi_svc_operation_begin() != D_OK) return;
    wifi_service_return_sta_mode_execute();
    wifi_svc_operation_end();
}

//*********************************************************************************//
//                              WiFi SDK 事件回调                                   //
//*********************************************************************************//

#if (D_FEATURE_BLE_PROVISION) && (WIFI_RUN_MODE != WIFI_RUN_MODE_AP)
/**
 * @brief BLE 配网构建启动时优先采用应用 VM 凭据，无凭据则等待配网。
 * @param[in,out] parm 已清零的 SDK 模式配置。
 */
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
        /* 启动自动连接同时分配并绑定进入编号，使 DHCP 可被接受。 */
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

/**
 * @brief 依据 AP、BLE 配网或固定 STA 构建设置模块默认模式。
 */
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

/**
 * @brief 核对进入编号、目标与凭据代次后保存 pending 并发布在线。
 * @note 凭据写失败保留 pending 供异步重试；旧保存者绝不清空可能已属于新连接的 VM。
 */
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

    (void)wifi_service_bind_sdk_reconnect(1U);
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

    /* 预期 SSID 为空时明确拒绝，防止忘记竞争或意图已清除。 */
    if (wifi_svc_ssid_matches_expected(expected_ssid) == 0U) {
        LOGW(TAG, "ignore DHCP success: SSID mismatch expected=%s (no fail)",
             expected_ssid);
        return;
    }

    if (have_pending != 0U) {
        uint8_t can_save = 0U;
        int save_ret = -1;

        /* 在单个凭据事务内复核意图、原始写入，并仅在仍有效时清 pending。
 * 旧写者绝不能调用 wifi_cred_clear，否则可能擦掉新连接凭据。 */
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
            wifi_cred_publish_result_locked(save_ret, 1U);
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

    LOGI(TAG, "DHCP success, network ready!");
    net.arg = "net";
    net.event = NET_EVENT_CONNECTED;
    net_event_notify(NET_EVENT_FROM_USER, &net);
#ifdef CONFIG_MBEDTLS_ENABLE
    wifi_start_ntp_sync();
#endif
}

/**
 * @brief 仅发布离线网络事件，不请求 SDK 自动重连。
 */
static void wifi_service_notify_offline_only(void)
{
    struct net_event net = {0};

    net.arg = "net";
    net.event = NET_EVENT_DISCONNECTED;
    net_event_notify(NET_EVENT_FROM_USER, &net);
}

/**
 * @brief 记录旧 STA 退出事实，或失效真实连接后发布离线及重连请求。
 * @note SDK HSM 回调不等待 connect_seq，避免与同步 SDK 操作互相等待；切网事实交现有 WiFi 任务消费。
 */
static void wifi_service_notify_disconnect(void)
{
    struct net_event net = {0};
    uint8_t do_notify = 0U;

    wifi_svc_lock();
    /* SDK 在自身 HSM 序列中调用此处；不可等待 connect_seq，其所有者可能正同步等待同一 HSM。
 * 只保存退出事实，由现有 WiFi 工作任务消费。 */
    if (s_switch_phase != WIFI_SWITCH_NONE) {
        s_switch_phase = WIFI_SWITCH_OLD_DISCONNECTED;
        wifi_svc_unlock();
        return;
    }

    /* 分配到进入之间，enter_attempt==0 且新尝试已分配时的主动退出事件忽略，无需压制计数。 */
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

/**
 * @brief 只对已绑定进入编号的有效尝试发布连接错误并回退 SDK 默认。
 * @param[in] error 连接错误分类。
 * @note SDK 回调不携代次，不能使用刚捕获的新活动编号替代旧进入编号。
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
    /* 锁外清理错误密码对应的 SDK 默认和 PMK，避免影响之后的正确连接；目标认领不能遗留错误存储。 */
    wifi_sdk_rollback_sta_default_after_fail();
}

/**
 * @brief 按实际 SDK 事件推进连接、扫描和旧 STA 退出事实。
 * @param[in] network_ctx SDK 只声明为 void 指针，本实现不解引用或假设代次字段。
 * @param[in] event WiFi 模块、关联、扫描、DHCP 或停止事件。
 * @return 当前实现固定返回 0。
 */
static int wifi_event_callback(void *network_ctx, enum WIFI_EVENT event)
{
    uint32_t attempt;
    enum wifi_sta_connect_state sdk_state;
    char expected_ssid[WIFI_SSID_MAXLEN];

    /* SDK 对 network_ctx 只声明 void* 与 WIFI_EVENT，不含代次合同。
 * 不虚构字段，失败及 DHCP 使用 s_enter_attempt。 */
    (void)network_ctx;
    memset(expected_ssid, 0, sizeof(expected_ssid));

    switch (event) {
    case WIFI_EVENT_MODULE_INIT:
        wifi_service_handle_module_init();
        break;
    case WIFI_EVENT_STA_CONNECT_SUCC:
        (void)wifi_service_bind_sdk_reconnect(0U);
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
            /* 只丢弃不匹配事件，不把当前有效尝试改为失败；原因同 DHCP 路径。 */
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
        wifi_svc_lock();
        s_scan_completed = 1U;
        wifi_svc_unlock();
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC:
        LOGI(TAG, "SDK DHCP success event");
        wifi_service_notify_dhcp_success();
        break;
    case WIFI_EVENT_STA_DISCONNECT:
        LOGW(TAG, "STA disconnected");
        wifi_service_notify_disconnect();
        break;
    case WIFI_EVENT_STA_STOP:
        /* SDK 在 wifi_module_exit_sta_mode 后发 STOP，未关联 STA 也可能如此。
 * 只有待切网事务接纳此退出事实，因为之后 enter(new) 也会发 STOP，必须保留新编号。
 * STOP 不排空已关联旧 STA 的异步 DISCONNECT。此 getter 为普通状态读取，
 * 非 HSM 请求；需复核早先快照后才赢得关联的情况。 */
        sdk_state = wifi_get_sta_connect_state();
        wifi_svc_lock();
        if (s_switch_phase == WIFI_SWITCH_WAIT_OLD_DISCONNECT) {
            if (sdk_state == WIFI_STA_CONNECT_SUCC ||
                sdk_state == WIFI_STA_NETWORK_STACK_DHCP_SUCC ||
                sdk_state == WIFI_STA_NETWORK_STACK_DHCP_TIMEOUT) {
                s_switch_stop_eligible = 0U;
            } else if (s_switch_stop_eligible != 0U) {
                s_switch_phase = WIFI_SWITCH_OLD_DISCONNECTED;
            }
        }
        wifi_svc_unlock();
        LOGI(TAG, "STA stopped");
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

/**
 * @brief 兼容事件入口将 BLE 配网值提交给 STA 服务。
 * @param[in] evt 非空配网事件；有效载荷为 wifi_cfg_t。
 * @param[in] ctx 订阅上下文，本实现不使用。
 */
static void on_ble_wifi_cfg_request(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_BLE_WIFI_CFG_REQUEST) return;
    const wifi_cfg_t *cfg = (const wifi_cfg_t *)evt->data;
    if (!cfg) return;

    LOGI(TAG, "收到 BLE 配网请求: ssid=%s", cfg->ssid);
    wifi_service_sta_connect(cfg->ssid, cfg->pwd);
}

/**
 * @brief 联网事件触发待保存凭据重试，并逐项核对内容与代次。
 * @param[in] evt 联网事件，其他事件不处理。
 * @param[in] ctx 订阅上下文，本实现不使用。
 * @note 旧意图写入后竞争失效也不擦 VM，避免清掉更新的密码。
 */
static void on_wifi_connected_save(const d_event_t *evt, void *ctx)
{
    char pending_ssid[WIFI_SSID_MAXLEN];
    char pending_pwd[WIFI_PWD_MAXLEN];
    uint32_t cred_epoch;
    uint32_t pending_gen;
    int save_result;

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

    save_result = wifi_cred_save_raw(pending_ssid, pending_pwd);
    wifi_cred_publish_result_locked(save_result, 1U);
    if (save_result != 0) {
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
        /* 写后意图已旧也不擦 VM，其中可能保存了更新密码。 */
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
/**
 * @brief 执行一次 NTP 校时，成功后更新加密时间并发布完成提示。
 * @param[in] priv 任务兼容上下文，本实现不使用。
 * @note 失败仍发布 EVT_NTP_SYNCED 以结束依赖方等待，事件本身不证明时间可信。
 */
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

/**
 * @brief 以允许任务函数返回的 thread_fork 启动一次性 NTP 同步。
 * @note 创建失败也发布完成提示；不能以该提示代替 NTP 成功状态。
 */
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

/**
 * @brief 推进旧 STA 退出握手及连接停滞期限，并受检撤销过期尝试。
 * @param[in] elapsed_ms 本轮经过的毫秒数。
 * @note SDK 撤销前重新取得 connect_seq 并核对代次，避免取消已替换的新尝试。
 */
static void wifi_service_connect_watchdog_tick(uint32_t elapsed_ms)
{
    uint8_t do_stall_abort = 0U;
    uint8_t force_invalidate = 0U;
    uint32_t stall_enter = 0U;
    uint32_t cancelled_attempt = 0U;

    wifi_svc_lock();
    if (s_switch_phase == WIFI_SWITCH_OLD_DISCONNECTED) {
        wifi_svc_unlock();
        wifi_svc_connect_seq_lock();
        /* 取得序列锁后复核阶段与活动编号；忘记、覆盖或失败可能已替换先前观察到的事实。 */
        wifi_service_switch_enter_new();
        wifi_svc_connect_seq_unlock();
        return;
    }
    /* 切网仅在确认 STA_DISCONNECT 或合格 STA_STOP 后进入；退出事件缺失则超时，不轮询空闲来猜测。 */
    if (s_switch_phase == WIFI_SWITCH_WAIT_OLD_DISCONNECT) {
        s_switch_wait_ms += elapsed_ms;
        if (s_switch_wait_ms < WIFI_SWITCH_WAIT_MS) {
            wifi_svc_unlock();
            return;
        }
        wifi_svc_switch_cancel_locked();
        wifi_service_clear_pending();
        wifi_svc_invalidate_attempt_locked();
        cancelled_attempt = s_connect_attempt_id;
        wifi_service_status_set_locked(
            WIFI_SERVICE_CONN_ERROR,
            WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT, NULL);
        wifi_svc_unlock();

        /* 破坏性 SDK 撤销由 connect_seq 序列化，并复核没有新活动尝试；BLE 覆盖可能在失效后先完成。 */
        wifi_svc_connect_seq_lock();
        wifi_svc_lock();
        if ((s_connect_attempt_id != cancelled_attempt) ||
            (wifi_svc_attempt_live_locked(s_active_connect_attempt) != 0U) ||
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
        /* 状态已为 ERROR，迟到 STA_DISCONNECT 会被忽略；发布一次离线，
 * 使 BLE、Qwen、BRTC 清除在线视图，不自动回连。 */
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
    /* enter_attempt==0 表示已分配但未绑定进入；正常进入错误处理会无操作，因此这里强制失效。 */
    if (s_enter_attempt == 0U) {
        force_invalidate = 1U;
        wifi_svc_switch_cancel_locked();
        wifi_service_clear_pending();
        wifi_svc_invalidate_attempt_locked();
        cancelled_attempt = s_connect_attempt_id;
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
        /* 本次分配已失效；若有更新会话则跳过撤销。 */
        if ((s_connect_attempt_id != cancelled_attempt) ||
            (wifi_svc_attempt_live_locked(s_active_connect_attempt) != 0U) ||
            (wifi_svc_attempt_live_locked(s_enter_attempt) != 0U) ||
            (s_switch_phase != WIFI_SWITCH_NONE)) {
            wifi_svc_unlock();
            wifi_svc_connect_seq_unlock();
            LOGW(TAG, "connect stall abort skipped (newer connect live)");
            return;
        }
    } else {
        /* 只有停滞会话的进入编号仍有效才继续；被新分配、进入或切网替换时跳过。 */
        if ((stall_enter == 0U) ||
            (wifi_svc_attempt_live_locked(stall_enter) == 0U) ||
            (s_switch_phase != WIFI_SWITCH_NONE)) {
            wifi_svc_unlock();
            wifi_svc_connect_seq_unlock();
            LOGW(TAG, "connect stall abort skipped (session gone/switched)");
            return;
        }
        /* 先失效再撤销 SDK，防止迟到 DHCP 回调发布在线。 */
        wifi_svc_switch_cancel_locked();
        wifi_svc_invalidate_attempt_locked();
        wifi_service_clear_pending();
        wifi_service_status_set_locked(WIFI_SERVICE_CONN_ERROR,
                                       WIFI_SERVICE_ERR_ASSOC_TIMEOUT, NULL);
    }
    wifi_svc_unlock();

    LOGW(TAG, "STA connect stall: abort SDK + invalidate attempt");
    wifi_service_abort_sdk_connect();
    wifi_sdk_rollback_sta_default_after_fail();
    wifi_svc_connect_seq_unlock();
}

/**
 * @brief 持久 WiFi 工作任务，消费请求并约每秒核对状态与连接期限。
 * @param[in] priv 任务兼容上下文，本实现不使用。
 * @note SDK 自主重连只做事实接纳，不因此启动第二次连接。
 */
static void wifi_task(void *priv)
{
    uint32_t periodic_at;
    wifi_set_event_callback(wifi_event_callback);
    wifi_on();
    wifi_svc_lock();
    s_request.radio_ready = 1U;
    wifi_svc_unlock();
    LOGI(TAG, "WiFi ON");
    periodic_at = sys_timer_get_ms();
    while (1) {
        uint32_t elapsed = sys_timer_get_ms() - periodic_at;
        uint32_t ticks = elapsed >= 1000U ? 1U : (1000U - elapsed + 9U) / 10U;
        int wait_result = os_sem_pend(&s_wifi_worker_wake, ticks);
        /* 无效句柄不能把通知失败变为忙循环。 */
        if (wait_result != 0 && wait_result != OS_TIMEOUT) os_time_dly(1);
        wifi_service_request_process();
        if ((uint32_t)(sys_timer_get_ms() - periodic_at) < 1000U) continue;
        periodic_at = sys_timer_get_ms();
        /* 回调可能先于 SDK 可见状态更新。只核对未被活动尝试占用的保留目标，不启动新的 SDK 连接。 */
        if (wifi_service_bind_sdk_reconnect(1U) != 0U) {
            wifi_service_notify_dhcp_success();
        }
        wifi_service_connect_watchdog_tick(1000U);
        wifi_service_refresh_cached_status();
    }
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

/**
 * @brief 提交可替换旧尝试的选网意图，复用先断后连流程；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_connect_ui(const char *ssid, const char *pwd)
{
    /* 显式选网与配网共用延迟断连握手以替换后台连接，保存的凭据继续保留。 */
    return wifi_service_connect_sync(ssid, pwd, 1U, 1U);
}

/**
 * @brief 读取保存凭据并在未关联时提交不覆盖的重连。
 * @return 无凭据返回 D_ERR_NOT_FOUND；已在线返回 D_OK；其余为提交结果。
 * @note VM 读取失败不覆盖真实在线状态。
 */
static d_err_t wifi_service_reconnect_saved_execute(void)
{
    char ssid[WIFI_SSID_MAXLEN];
    char pwd[WIFI_PWD_MAXLEN];
    wifi_service_conn_state_t state;

    if (!wifi_cred_load(ssid, sizeof(ssid), pwd, sizeof(pwd))) {
        /* VM 读取失败或已清除时，本次运行内建立的网络仍可能在线，不覆盖真实 CONNECTED。 */
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
        /* 已使用保存凭据在线，只刷新状态。 */
        wifi_service_status_set(WIFI_SERVICE_CONN_CONNECTED,
                                WIFI_SERVICE_ERR_NONE, ssid);
        return D_OK;
    }

    /* 关联中不覆盖，UI 与服务共同拒绝重复点击；save_on_success=0，因为 VM 已持有凭据。 */
    return wifi_service_start_connect(ssid, pwd, 0U, 0U);
}

/**
 * @brief 在一个 connect_seq 中完成清除与离网，避免重连从间隙插入。
 * @return 完整请求成功返回 D_OK；清除失败仍尽力离网并返回原错误。
 */
static d_err_t wifi_service_forget_saved_execute(void)
{
    d_err_t err;

    /* 清空与离网共用一个 connect_seq 临界段，防止并发连接在两个辅助阶段间分配或进入。 */
    if (wifi_svc_configuration_lock() != D_OK) return D_ERR_BUSY;
    err = wifi_service_clear_saved_credentials_inner(0U);
    if (err != D_OK) {
        /* VM 清空失败仍尽力退出无线，避免 UI 或服务已显示断开而真实 STA 仍在线。 */
        (void)wifi_service_disconnect_for_reconfig_inner(0U);
        wifi_svc_connect_seq_unlock();
        return err;
    }
    err = wifi_service_disconnect_for_reconfig_inner(0U);
    wifi_svc_connect_seq_unlock();
    if (err != D_OK) {
        return err;
    }
    /* 忘记先发布 DISCONNECTED 再退出无线，因此迟到 STA_DISCONNECT 被忽略；随后重开 BLE 配网广播。 */
#if D_FEATURE_BLE_PROVISION
    (void)ble_service_enter_reconfig_mode();
#endif
    return D_OK;
}

/**
 * @brief 在连接序列中受理或合并扫描，不重置已有活动扫描期限。
 * @return 扫描请求或已有扫描返回 D_OK；无线未就绪返回相应错误。
 */
static d_err_t wifi_service_scan_start_execute(void)
{
    d_err_t result;
    wifi_svc_connect_seq_lock();
    wifi_svc_lock();
    if (s_scan_state == WIFI_SERVICE_SCAN_SCANNING ||
        s_scan_state == WIFI_SERVICE_SCAN_BUSY) {
        wifi_svc_unlock();
        wifi_svc_connect_seq_unlock();
        return D_OK;
    }
    s_scan_elapsed_ms = 0U;
    s_scan_deferred_elapsed = 0U;
    wifi_svc_unlock();
    result = wifi_scan_request_start();
    wifi_svc_connect_seq_unlock();
    LOGI(TAG, "scan requested result=%d", result);
    return result;
}

/**
 * @brief 读取保存凭据并提交重连，不替换进行中的关联；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_reconnect_saved(void)
{
    d_err_t result = wifi_svc_operation_begin();
    if (result != D_OK) return result;
    result = wifi_service_reconnect_saved_execute();
    wifi_svc_operation_end();
    return result;
}

/**
 * @brief 在同一连接序列内清除应用及 SDK 凭据并退出自动回连；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_forget_saved(void)
{
    d_err_t result = wifi_svc_operation_begin();
    if (result != D_OK) return result;
    result = wifi_service_forget_saved_execute();
    wifi_svc_operation_end();
    return result;
}

/**
 * @brief 请求扫描或观察已在进行的 SDK 扫描；完整合同见 wifi_service.h。
 */
d_err_t wifi_service_scan_start(void)
{
    /* 兼容同步扫描保留既有维护准入语义。 */
    d_err_t result = wifi_svc_operation_begin();
    if (result != D_OK) return result;
    result = wifi_service_scan_start_execute();
    wifi_svc_operation_end();
    return result;
}

/**
 * @brief 在持久 WiFi 任务中执行一条自有请求并发布真实提交结果。
 * @note 操作占用跨 SDK、VM 与 BLE 清理保留，payload 为静态自有副本。
 */
static void wifi_service_request_process(void)
{
    uint32_t id;
    d_err_t result;
    wifi_service_request_kind_e kind;
    wifi_svc_lock();
    if (!s_request.enabled || s_request.snapshot.phase != WIFI_SERVICE_REQUEST_QUEUED) {
        wifi_svc_unlock();
        return;
    }
    id = s_request.snapshot.request_id;
    kind = s_request.snapshot.kind;
    s_request.snapshot.phase = WIFI_SERVICE_REQUEST_RUNNING;
    s_request.snapshot.revision++;
    wifi_svc_unlock();
    /* 执行占用跨 SDK、VM 及 BLE 清理保留；载荷为静态自有副本，不借用 UI 指针，也不增加工作栈副本。 */
    switch (kind) {
    case WIFI_SERVICE_REQUEST_CONNECT:
        result = wifi_service_start_connect(s_request.payload.ssid,
                                           s_request.payload.pwd, 1U, 1U);
        break;
    case WIFI_SERVICE_REQUEST_SCAN:
        result = wifi_service_scan_start_execute();
        break;
    case WIFI_SERVICE_REQUEST_RECONNECT:
        result = wifi_service_reconnect_saved_execute();
        break;
    case WIFI_SERVICE_REQUEST_FORGET:
        result = wifi_service_forget_saved_execute();
        break;
    default:
        result = D_ERR_INVALID_ARG;
        break;
    }
    LOGI(TAG, "request done id=%u kind=%u result=%d", id, kind, result);
    wifi_service_request_finish(id, result);
}

/**
 * @brief 按经过时间推进扫描等待、重试、超时与结果收集；完整合同见 wifi_service.h。
 */
void wifi_service_scan_update(uint32_t elapsed_ms)
{
    uint8_t retry = 0U;
    uint8_t timed_out = 0U;
    uint32_t previous_ms;
    uint8_t active;
    uint8_t collectible;

    /* UI 扫描观察者不能等待连接或监督中的 SDK 操作。 */
    wifi_svc_lock();
    active = s_scan_state == WIFI_SERVICE_SCAN_SCANNING ||
        s_scan_state == WIFI_SERVICE_SCAN_BUSY || s_scan_state == WIFI_SERVICE_SCAN_NOT_READY;
    collectible = s_scan_completed && (s_scan_state == WIFI_SERVICE_SCAN_SCANNING ||
        s_scan_state == WIFI_SERVICE_SCAN_BUSY || s_scan_state == WIFI_SERVICE_SCAN_TIMEOUT);
    if (!active && !collectible) {
        s_scan_deferred_elapsed = 0U;
        wifi_svc_unlock();
        return;
    }
    wifi_svc_unlock();
    if (s_wifi_connect_seq_ready != 0U && os_mutex_accept(&s_wifi_connect_seq_mutex) != 0) {
        wifi_svc_lock();
        /* 重新检查，所有者可能在本次取得状态锁前改变扫描。 */
        active = s_scan_state == WIFI_SERVICE_SCAN_SCANNING ||
            s_scan_state == WIFI_SERVICE_SCAN_BUSY || s_scan_state == WIFI_SERVICE_SCAN_NOT_READY;
        collectible = s_scan_completed && (s_scan_state == WIFI_SERVICE_SCAN_SCANNING ||
            s_scan_state == WIFI_SERVICE_SCAN_BUSY || s_scan_state == WIFI_SERVICE_SCAN_TIMEOUT);
        if (active && !collectible) {
            if (s_scan_deferred_elapsed >= WIFI_SCAN_TIMEOUT_MS ||
                elapsed_ms >= WIFI_SCAN_TIMEOUT_MS - s_scan_deferred_elapsed) {
                s_scan_deferred_elapsed = WIFI_SCAN_TIMEOUT_MS;
            } else {
                s_scan_deferred_elapsed += elapsed_ms;
            }
        }
        wifi_svc_unlock();
        return;
    }
    wifi_svc_lock();
    if (s_scan_deferred_elapsed >= WIFI_SCAN_TIMEOUT_MS ||
        elapsed_ms >= WIFI_SCAN_TIMEOUT_MS - s_scan_deferred_elapsed) {
        elapsed_ms = WIFI_SCAN_TIMEOUT_MS;
    } else {
        elapsed_ms += s_scan_deferred_elapsed;
    }
    s_scan_deferred_elapsed = 0U;
    if (s_scan_completed != 0U) {
        if ((s_scan_state == WIFI_SERVICE_SCAN_SCANNING) ||
            (s_scan_state == WIFI_SERVICE_SCAN_BUSY) ||
            (s_scan_state == WIFI_SERVICE_SCAN_TIMEOUT)) {
            /* UI 期限不取消 SDK 扫描；迟到结果仍发布一次，避免重连扫描每次错过窗口。
 * 非活动阶段保留完成事实至下次刷新。 */
            s_scan_completed = 0;
            wifi_svc_unlock();
            if (wifi_is_on()) {
                wifi_scan_complete_current();
            } else {
                /* 旧完成事件不能把不可用无线变成 DONE，也不能清掉最后可用的本地列表。 */
                wifi_scan_set_state(WIFI_SERVICE_SCAN_NOT_READY);
            }
            wifi_svc_connect_seq_unlock();
            return;
        }
    }

    if ((s_scan_state != WIFI_SERVICE_SCAN_SCANNING) &&
        (s_scan_state != WIFI_SERVICE_SCAN_BUSY) &&
        (s_scan_state != WIFI_SERVICE_SCAN_NOT_READY)) {
        wifi_svc_unlock();
        wifi_svc_connect_seq_unlock();
        return;
    }

    previous_ms = s_scan_elapsed_ms;
    if (s_scan_elapsed_ms >= WIFI_SCAN_TIMEOUT_MS ||
        elapsed_ms >= WIFI_SCAN_TIMEOUT_MS - s_scan_elapsed_ms) {
        s_scan_elapsed_ms = WIFI_SCAN_TIMEOUT_MS;
    } else {
        s_scan_elapsed_ms += elapsed_ms;
    }
    if (s_scan_elapsed_ms >= WIFI_SCAN_TIMEOUT_MS) {
        s_scan_elapsed_ms = 0;
        timed_out = 1U;
    } else if ((s_scan_state == WIFI_SERVICE_SCAN_BUSY) ||
               (s_scan_state == WIFI_SERVICE_SCAN_NOT_READY)) {
        retry = s_scan_elapsed_ms / WIFI_SCAN_RETRY_INTERVAL_MS >
                previous_ms / WIFI_SCAN_RETRY_INTERVAL_MS;
    }
    wifi_svc_unlock();
    if (retry != 0U) {
        (void)wifi_scan_request_start();
    } else if (timed_out != 0U) {
        wifi_scan_set_state(WIFI_SERVICE_SCAN_TIMEOUT);
        LOGW(TAG, "scan wait timed out; cached results retained");
    }
    wifi_svc_connect_seq_unlock();
}

/**
 * @brief 读取当前扫描状态；完整合同见 wifi_service.h。
 */
wifi_service_scan_state_t wifi_service_scan_get_state(void)
{
    wifi_service_scan_state_t state;

    wifi_svc_lock();
    state = s_scan_state;
    wifi_svc_unlock();
    return state;
}

/**
 * @brief 在状态锁内复制最近缓存的有界扫描列表；完整合同见 wifi_service.h。
 */
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

/**
 * @brief 读取扫描状态与结果版本；完整合同见 wifi_service.h。
 */
uint32_t wifi_service_scan_get_version(void)
{
    uint32_t version;

    wifi_svc_lock();
    version = s_scan_version;
    wifi_svc_unlock();
    return version;
}

/**
 * @brief 复制连接状态，并在锁外实时读取 VM 凭据与 RSSI；完整合同见 wifi_service.h。
 */
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

    /* VM 与 RSSI 在锁外读取，可能阻塞或调用 SDK。 */
    snap.has_saved_credentials = wifi_service_has_saved_credentials();
    connected = (snap.state == WIFI_SERVICE_CONN_CONNECTED) ? 1U : 0U;
    if ((connected != 0U) && wifi_is_on()) {
        snap.rssi = (int8_t)wifi_get_rssi();
        snap.signal_strength = wifi_scan_rssi_to_signal(snap.rssi);
    } else {
        snap.rssi = 0;
        snap.signal_strength = 0U;
    }
    *status = snap;
}

/**
 * @brief 在短状态锁内复制最后发布的完整状态；完整合同见 wifi_service.h。
 */
void wifi_service_get_cached_status(wifi_service_status_t *status)
{
    if (status == NULL) return;
    wifi_svc_lock();
    *status = s_wifi_status;
    wifi_svc_unlock();
}

/**
 * @brief 在锁外采样 VM 与 RSSI，仅原版本未变化时回写缓存。
 */
static void wifi_service_refresh_cached_status(void)
{
    wifi_service_status_t sampled;
    uint32_t revision = wifi_service_get_status_version();

    /* 慢 I/O 留在启动或现有工作任务中，位于状态锁外。 */
    wifi_service_get_status(&sampled);
    wifi_svc_lock();
    if (s_wifi_status_version == revision &&
        (s_wifi_status.has_saved_credentials != sampled.has_saved_credentials ||
         s_wifi_status.rssi != sampled.rssi ||
         s_wifi_status.signal_strength != sampled.signal_strength)) {
        s_wifi_status.has_saved_credentials = sampled.has_saved_credentials;
        s_wifi_status.rssi = sampled.rssi;
        s_wifi_status.signal_strength = sampled.signal_strength;
        s_wifi_status_version++;
    }
    wifi_svc_unlock();
}

/**
 * @brief 在提供者互斥量内复制连接状态与完整状态版本；完整合同见 wifi_service.h。
 */
void wifi_service_get_connection_snapshot(wifi_service_connection_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    wifi_svc_lock();
    snapshot->state = s_wifi_status.state;
    snapshot->revision = s_wifi_status_version;
    wifi_svc_unlock();
}

/**
 * @brief 读取完整连接状态的发布版本；完整合同见 wifi_service.h。
 */
uint32_t wifi_service_get_status_version(void)
{
    uint32_t version;

    wifi_svc_lock();
    version = s_wifi_status_version;
    wifi_svc_unlock();
    return version;
}

/**
 * @brief 判断认证类型是否按需密码网络处理；完整合同见 wifi_service.h。
 */
uint8_t wifi_service_auth_requires_password(uint8_t auth_mode)
{
    return (auth_mode == (uint8_t)WIFI_SERVICE_AUTH_OPEN) ? 0U : 1U;
}

/**
 * @brief 借用认证类型的静态显示名称；完整合同见 wifi_service.h。
 */
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

/**
 * @brief 撤销订阅，并只在工作任务尚未启动时销毁临时同步对象。
 * @note 已有工作任务具有启动生命周期，反初始化不销毁其仍可能使用的同步对象。
 */
static void wifi_service_cleanup_init_failure(void)
{
    d_event_unsubscribe(EVT_BLE_WIFI_CFG_REQUEST, on_ble_wifi_cfg_request);
    d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_connected_save);
    /* 反初始化或重新初始化不销毁启动生命周期的现有工作任务。 */
    if (s_request.worker_started) {
        wifi_service_request_close();
        return;
    }
    if (s_wifi_worker_wake_ready) {
        os_sem_del(&s_wifi_worker_wake, OS_DEL_ALWAYS);
        s_wifi_worker_wake_ready = 0U;
    }
    if (s_wifi_operation_gate_ready) {
        os_sem_del(&s_wifi_operation_gate, OS_DEL_ALWAYS);
        s_wifi_operation_gate_ready = 0U;
    }
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

/**
 * @brief 建立同步对象、订阅与启动生命周期的 WiFi 工作任务；完整合同见 wifi_service.h。
 */
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
    if (!s_wifi_operation_gate_ready) {
        if (os_sem_create(&s_wifi_operation_gate, 1) != 0) {
            wifi_service_cleanup_init_failure();
            return D_ERR_IO;
        }
        s_wifi_operation_gate_ready = 1U;
    }
    if (!s_wifi_worker_wake_ready) {
        if (os_sem_create(&s_wifi_worker_wake, 0) != 0) {
            wifi_service_cleanup_init_failure();
            return D_ERR_IO;
        }
        s_wifi_worker_wake_ready = 1U;
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

    /* UI 或 WiFi 工作任务读取前先发布初始 VM 凭据存在性。 */
    wifi_service_refresh_cached_status();
    if (!s_request.worker_started &&
        os_task_create(wifi_task, NULL, 10, 1024, 0, "wifi_task") != 0) {
        LOGE(TAG, "wifi_task 创建失败");
        wifi_service_cleanup_init_failure();
        return D_ERR_IO;
    }
    wifi_svc_lock();
    s_request.worker_started = 1U;
    s_request.enabled = 1U;
    wifi_svc_unlock();
    return D_OK;
}

/**
 * @brief 停止受理并解除本服务事件订阅，保留工作任务与同步对象；完整合同见 wifi_service.h。
 */
void wifi_service_deinit(void)
{
    /* 排队请求以 NOT_READY 终结；执行中的调用保留占用直到真实返回，不能在其下销毁同步对象。 */
    wifi_service_request_close();
    d_event_unsubscribe(EVT_BLE_WIFI_CFG_REQUEST, on_ble_wifi_cfg_request);
    d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_connected_save);
}

#endif /* D_FEATURE_WIFI */
