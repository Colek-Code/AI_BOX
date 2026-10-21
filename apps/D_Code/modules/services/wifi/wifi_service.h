/**
 * @file    wifi_service.h
 * @brief   WiFi 服务模块接口
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
     * Switch wait: old STA never delivered DISCONNECT before timeout.
     * Not association failure — new SSID has not started associate yet.
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
    /* 1: link up but VM credential write failed (reboot may lose network). */
    uint8_t cred_save_failed;
} wifi_service_status_t;

/**
 * @brief WiFi 服务初始化
 */
d_err_t wifi_service_init(void);

/**
 * @brief WiFi 服务销毁
 */
void wifi_service_deinit(void);

/** Freeze configuration changes while keeping the existing network usable.
 * Single coordinator: only a successful begin owns the matching end.
 * Returns BUSY for in-flight configuration/persistence, a deferred switch,
 * an unconnected link, or another holder. Does not wait for SDK/VM work.
 * No radio guarantee: physical disconnection can still occur while held. */
d_err_t wifi_service_configuration_hold_begin(void);
void wifi_service_configuration_hold_end(void);
/** Informational rejection for frontends; mutation APIs recheck atomically. */
uint8_t wifi_service_configuration_is_held(void);

/**
 * @brief WiFi STA 连接（供 BLE 配网事件回调使用）
 */
void wifi_service_sta_connect(const char *ssid, const char *pwd);

/**
 * @brief UI 发起 WiFi 连接；成功 DHCP 后才保存凭证
 */
d_err_t wifi_service_connect_ui(const char *ssid, const char *pwd);

/**
 * @brief 用已保存凭证重新连接
 */
d_err_t wifi_service_reconnect_saved(void);

/**
 * @brief 忘记当前已保存网络并退出自动回连
 */
d_err_t wifi_service_forget_saved(void);

/**
 * @brief 清除已保存的 WiFi 凭证
 */
/**
 * Clear app VM WiFi creds.
 * On success: also invalidate attempt, suppress intentional disconnect noise,
 * and set DISCONNECTED.
 * On D_ERR_IO: cred epoch still advanced (blocks stale DHCP pending save) and
 * pending cleared, but status/suppress are NOT changed and VM may still hold
 * old SSID/PWD. Caller must treat as failure; forget_saved best-effort
 * disconnects radio.
 */
d_err_t wifi_service_clear_saved_credentials(void);

/**
 * @brief 为重新配网断开当前 STA 连接
 */
d_err_t wifi_service_disconnect_for_reconfig(void);

/**
 * @brief WiFi STA 重连
 */
void wifi_service_return_sta_mode(void);

/**
 * @brief 启动一次 WiFi 扫描
 */
d_err_t wifi_service_scan_start(void);

/**
 * @brief 推进 WiFi 扫描状态机
 */
void wifi_service_scan_update(uint32_t elapsed_ms);

/**
 * @brief 获取当前扫描状态
 */
wifi_service_scan_state_t wifi_service_scan_get_state(void);

/**
 * @brief 拷贝最近一次扫描结果缓存
 */
uint8_t wifi_service_scan_copy_results(wifi_service_scan_result_t *out, uint8_t max_count);

/**
 * @brief 获取扫描结果版本号
 */
uint32_t wifi_service_scan_get_version(void);

/**
 * @brief 获取连接状态快照
 */
void wifi_service_get_status(wifi_service_status_t *status);

/**
 * @brief 获取连接状态版本号
 */
uint32_t wifi_service_get_status_version(void);

/**
 * @brief 判断 auth_mode 是否需要密码
 */
uint8_t wifi_service_auth_requires_password(uint8_t auth_mode);

/**
 * @brief auth_mode 显示名
 */
const char *wifi_service_auth_name(uint8_t auth_mode);

#endif /* WIFI_SERVICE_H */
