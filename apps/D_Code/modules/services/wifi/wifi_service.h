/**
 * @file    wifi_service.h
 * @brief   WiFi 请求、凭据、连接状态与有界扫描服务接口。
 * @note    通过事件总线通信，不直接调用 BLE 模块
 */

#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "framework/d_error.h"
#include <stdint.h>

#define WIFI_SERVICE_SCAN_MAX_RESULTS  12
#define WIFI_SERVICE_SCAN_SSID_MAX     33

typedef enum {
    WIFI_SERVICE_SCAN_IDLE = 0,
    WIFI_SERVICE_SCAN_SCANNING,
    WIFI_SERVICE_SCAN_DONE,
    WIFI_SERVICE_SCAN_BUSY,
    WIFI_SERVICE_SCAN_NOT_READY,
    WIFI_SERVICE_SCAN_TIMEOUT,
    WIFI_SERVICE_SCAN_ERROR,
} wifi_service_scan_state_t;

typedef enum {
    WIFI_SERVICE_AUTH_OPEN = 0,
    WIFI_SERVICE_AUTH_WEP,
    WIFI_SERVICE_AUTH_WAPI,
    WIFI_SERVICE_AUTH_WPA,
    WIFI_SERVICE_AUTH_WPA2PSK,
    WIFI_SERVICE_AUTH_WPA_WPA2_PSK,
    WIFI_SERVICE_AUTH_WPA3_SAE,
    WIFI_SERVICE_AUTH_WPA2_WPA3_SAE,
    WIFI_SERVICE_AUTH_WPA3_H2E,
} wifi_service_auth_mode_t;

typedef enum {
    WIFI_SERVICE_CONN_IDLE = 0,
    WIFI_SERVICE_CONN_CONNECTING,
    WIFI_SERVICE_CONN_ASSOCIATED,
    WIFI_SERVICE_CONN_CONNECTED,
    WIFI_SERVICE_CONN_DISCONNECTED,
    WIFI_SERVICE_CONN_ERROR,
} wifi_service_conn_state_t;

typedef enum {
    WIFI_SERVICE_ERR_NONE = 0,
    WIFI_SERVICE_ERR_NOT_READY,
    WIFI_SERVICE_ERR_BUSY,
    WIFI_SERVICE_ERR_NO_SAVED,
    WIFI_SERVICE_ERR_NOT_FOUND,
    WIFI_SERVICE_ERR_ASSOC_FAIL,
    WIFI_SERVICE_ERR_ASSOC_TIMEOUT,
    /*
     * 切网等待期未及时收到可接受的旧 STA 退出确认：
     * 已关联连接要求 DISCONNECT，未关联且符合条件的尝试可接受 STOP。
     * 此时新 SSID 尚未开始关联，不能当作关联失败。
     */
    WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT,
    WIFI_SERVICE_ERR_DHCP_TIMEOUT,
    WIFI_SERVICE_ERR_DISCONNECTED,
} wifi_service_conn_error_t;

typedef struct {
    char ssid[WIFI_SERVICE_SCAN_SSID_MAX];
    int8_t rssi;
    uint8_t signal_strength;
    uint8_t channel;
    uint8_t auth_mode;
} wifi_service_scan_result_t;

typedef struct {
    wifi_service_conn_state_t state;
    wifi_service_conn_error_t error;
    char ssid[WIFI_SERVICE_SCAN_SSID_MAX];
    int8_t rssi;
    uint8_t signal_strength;
    uint8_t has_saved_credentials;
    /* 1 表示链路在线但 VM 凭据写入失败，重启后可能无法恢复网络。 */
    uint8_t cred_save_failed;
} wifi_service_status_t;

/* 仅含连接元数据；revision 对应完整提供者状态的版本。 */
typedef struct {
    wifi_service_conn_state_t state;
    uint32_t revision;
} wifi_service_connection_snapshot_t;

typedef enum {
    WIFI_SERVICE_REQUEST_NONE = 0,
    WIFI_SERVICE_REQUEST_CONNECT,
    WIFI_SERVICE_REQUEST_SCAN,
    WIFI_SERVICE_REQUEST_RECONNECT,
    WIFI_SERVICE_REQUEST_FORGET,
} wifi_service_request_kind_e;

typedef enum {
    WIFI_SERVICE_REQUEST_IDLE = 0,
    WIFI_SERVICE_REQUEST_QUEUED,
    WIFI_SERVICE_REQUEST_RUNNING,
    WIFI_SERVICE_REQUEST_COMPLETE,
} wifi_service_request_phase_e;

typedef struct {
    wifi_service_request_kind_e kind;
    char ssid[33];
    char pwd[65];
} wifi_service_request_t;

/* 不含密码或借用指针；新提交不覆盖上次完成，直到下一次完成，
 * uint32_t 编号回绕撞号时除外。 */
typedef struct {
    uint32_t revision;
    uint32_t request_id;
    uint32_t completed_id;
    wifi_service_request_kind_e kind;
    wifi_service_request_kind_e completed_kind;
    wifi_service_request_phase_e phase;
    d_err_t completed_result;
    char completed_ssid[33];
} wifi_service_request_snapshot_t;

/**
 * @brief 非阻塞受理前台配置请求，复制意图后交由 wifi_task 执行。
 * @param[in] request 非空连接、扫描、重连或忘记请求；连接字符串必须完整零结尾。
 * @param[out] out_id 非空输出，仅受理成功写入请求编号；重复活动扫描复用原编号。
 * @return 受理返回 D_OK；参数、就绪或占用冲突返回对应错误。
 * @note D_OK 不代表 SDK、VM 或 DHCP 完成；完成按编号与种类共同核对。不可在 ISR 或持消费方状态锁时调用。
 */
d_err_t wifi_service_request_submit(const wifi_service_request_t *request,
                                  uint32_t *out_id);
/**
 * @brief 读取仅含值、不含密码的配置请求快照。
 * @param[out] snapshot 输出快照；为空时不处理。
 * @note 上次完成记录保留到下一次完成，uint32_t 编号回绕撞号除外。
 */
void wifi_service_get_request_snapshot(wifi_service_request_snapshot_t *snapshot);

/**
 * @brief 建立同步对象、订阅与启动生命周期的 WiFi 工作任务。
 * @return 成功返回 D_OK；同步对象、订阅或任务创建失败返回 D_ERR_IO。
 * @note 工作任务已存在时复用，不因重新初始化销毁在途同步对象。
 */
d_err_t wifi_service_init(void);

/**
 * @brief 停止受理并解除本服务事件订阅，保留工作任务与同步对象。
 * @note 排队请求以 NOT_READY 完成；执行中请求保留占用直到真实返回。
 */
void wifi_service_deinit(void);

/**
 * @brief 在不停止现有网络的前提下，非阻塞冻结配置和凭据写入。
 * @return 取得返回 D_OK；基础对象未就绪返回 D_ERR_NOT_READY；配置、存盘、切网、未连接或已被持有返回 D_ERR_BUSY。
 * @note 单协调者，仅成功 begin 的调用方拥有 end；不保证无线链路不会物理掉线。
 */
d_err_t wifi_service_configuration_hold_begin(void);
/**
 * @brief 解除由成功 begin 取得的单协调者配置屏障。
 */
void wifi_service_configuration_hold_end(void);
/**
 * @brief 读取用于前端快速拒绝的配置屏障视图。
 * @return 正在持有返回非零，否则为 0。
 * @note 仅供提示；每个修改入口仍会原子复核，不能据此跳过准入。
 */
uint8_t wifi_service_configuration_is_held(void);

/**
 * @brief 兼容入口提交可替换旧尝试的 STA 连接，DHCP 成功后才保存凭据。
 * @param[in] ssid 非空、非空串目标 SSID。
 * @param[in] pwd 密码字符串；允许为空以使用空密码。
 * @note 忽略受理错误，需通过状态事件观察；调用可能等待配置序列中的 SDK 操作。
 */
void wifi_service_sta_connect(const char *ssid, const char *pwd);

/**
 * @brief 提交可替换旧尝试的选网意图，复用先断后连流程。
 * @param[in] ssid 非空、非空串目标 SSID。
 * @param[in] pwd 可空密码。
 * @return 受理连接返回 D_OK；参数、无线未就绪、屏障或执行占用失败返回相应错误。
 * @note 同步完成的是命令提交阶段，不是 DHCP；成功 DHCP 后才保存应用凭据。旧关联的退出由真实事件确认。
 */
d_err_t wifi_service_connect_ui(const char *ssid, const char *pwd);

/**
 * @brief 读取保存凭据并提交重连，不替换进行中的关联。
 * @return 无凭据返回 D_ERR_NOT_FOUND；已在线或连接受理返回 D_OK；其余为就绪或占用错误。
 * @note 可能等待 VM 和 SDK；D_OK 不等于新的 DHCP 已完成。
 */
d_err_t wifi_service_reconnect_saved(void);

/**
 * @brief 在同一连接序列内清除应用及 SDK 凭据并退出自动回连。
 * @return 清除与离网请求成功返回 D_OK；屏障、VM 或操作占用失败返回相应错误。
 * @note VM 清除失败仍尽力断开无线，错误必须视为未忘记成功；成功后可重开 BLE 配网广播。
 */
d_err_t wifi_service_forget_saved(void);

/**
 * @brief 清除已保存的 WiFi 凭证
 */
/**
 * @brief 清除应用 VM 24/25 凭据，并清理 SDK 记忆与快速连接缓存。
 * @return 成功返回 D_OK；受理冲突或 VM 写入失败返回相应错误。
 * @note 即使 D_ERR_IO 也会推进凭据代次并清空 pending，阻止旧 DHCP 保存；VM 可能保留旧凭据，且不伪造断开状态。此接口不保证无线已退出。
 */
d_err_t wifi_service_clear_saved_credentials(void);

/**
 * @brief 撤销当前连接意图并请求 STA 离网，保留应用 VM 凭据。
 * @return 执行请求成功或无线本来关闭返回 D_OK；配置屏障或操作准入失败返回对应错误。
 * @note 取消默认自动回连，不等于擦除应用凭据；SDK 断连仍可能异步完成。
 */
d_err_t wifi_service_disconnect_for_reconfig(void);

/**
 * @brief 兼容入口按 SDK 已存模式或编译默认值请求 STA 重连。
 * @note AP 构建不处理；失败受理无返回值，状态由事件确认。
 */
void wifi_service_return_sta_mode(void);

/**
 * @brief 请求扫描或观察已在进行的 SDK 扫描。
 * @return 受理返回 D_OK；无线或操作未就绪、占用失败返回相应错误。
 * @note D_OK 不代表完成；连接中也可请求，重复扫描不延长等待期限，不清除已保存凭据。
 */
d_err_t wifi_service_scan_start(void);

/**
 * @brief 按经过时间推进扫描等待、重试、超时与结果收集。
 * @param[in] elapsed_ms 自上次推进以来经过的毫秒数。
 * @note 不会等待连接序列锁；锁忙时累计等待预算。UI 期限不取消 SDK 扫描，迟到结果仍可发布。
 */
void wifi_service_scan_update(uint32_t elapsed_ms);

/**
 * @brief 读取当前扫描状态。
 * @return WIFI_SERVICE_SCAN_* 当前状态。
 */
wifi_service_scan_state_t wifi_service_scan_get_state(void);

/**
 * @brief 在状态锁内复制最近缓存的有界扫描列表。
 * @param[out] out 至少 max_count 项容量的输出数组。
 * @param[in] max_count 最多复制项数。
 * @return 实际复制项数；空输出或容量为 0 时返回 0。
 */
uint8_t wifi_service_scan_copy_results(wifi_service_scan_result_t *out, uint8_t max_count);

/**
 * @brief 读取扫描状态与结果版本。
 * @return 当前版本值，允许为 0。
 */
uint32_t wifi_service_scan_get_version(void);

/**
 * @brief 复制连接状态，并在锁外实时读取 VM 凭据与 RSSI。
 * @param[out] status 状态输出；为空时不处理。
 * @note 可能等待 VM 或调用无线 SDK；面向有界前台读取时使用 cached_status。
 */
void wifi_service_get_status(wifi_service_status_t *status);

/**
 * @brief 在短状态锁内复制最后发布的完整状态。
 * @param[out] status 状态输出；为空时不处理。
 * @note 不访问 VM 或无线 I/O；凭据在事务完成时发布，RSSI 等由现有工作任务约每秒采样。
 */
void wifi_service_get_cached_status(wifi_service_status_t *status);

/**
 * @brief 在提供者互斥量内复制连接状态与完整状态版本。
 * @param[out] snapshot 连接元数据输出；为空时不处理。
 * @note 不执行 VM、RSSI 或无线 I/O；在消费方状态锁外调用，版本为 0 仍有效。
 */
void wifi_service_get_connection_snapshot(wifi_service_connection_snapshot_t *snapshot);

/**
 * @brief 读取完整连接状态的发布版本。
 * @return 当前版本值，允许为 0。
 */
uint32_t wifi_service_get_status_version(void);

/**
 * @brief 判断认证类型是否按需密码网络处理。
 * @param[in] auth_mode WIFI_SERVICE_AUTH_* 值或 SDK 映射值。
 * @return OPEN 返回 0，其余包括未知值返回 1。
 */
uint8_t wifi_service_auth_requires_password(uint8_t auth_mode);

/**
 * @brief 借用认证类型的静态显示名称。
 * @param[in] auth_mode 认证类型。
 * @return 已知类型的静态名称，未知类型返回 SEC。
 */
const char *wifi_service_auth_name(uint8_t auth_mode);

#endif /* WIFI_SERVICE_H */
