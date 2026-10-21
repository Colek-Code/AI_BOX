/**
 * @file    ble_service.c
 * @brief   BLE 配网服务 — 事件驱动桥接层
 */

#include "config/app_config.h"

#if D_FEATURE_BLE_PROVISION

#include "ble_service.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "framework/d_event_payloads.h"
#include "d_ble_net_cfg.h"
#include "modules/services/wifi/wifi_service.h"
#include <string.h>

#define TAG "BLE_SVC"

//*********************************************************************************//
//                              WiFi 事件订阅                                        //
//*********************************************************************************//

static volatile int s_wifi_notified = 0;  /* 防止重复通知 BLE 客户端 */

/*
 * Defense-in-depth for SSID switch: wifi_service may suppress STA_DISCONNECT,
 * but if a suppress count is missed, DISCONNECTED still fires while a new
 * attempt is already CONNECTING/ASSOCIATED (or already CONNECTED).
 * Never re-open prov adv while the live wifi_service state is busy/online.
 * Explicit ble_service_enter_reconfig_mode() bypasses this gate.
 */
static void ble_svc_open_prov_adv(const char *reason)
{
    wifi_service_status_t st;

    memset(&st, 0, sizeof(st));
    wifi_service_get_status(&st);
    if ((st.state == WIFI_SERVICE_CONN_CONNECTING) ||
        (st.state == WIFI_SERVICE_CONN_ASSOCIATED) ||
        (st.state == WIFI_SERVICE_CONN_CONNECTED)) {
        LOGI(TAG, "skip BLE prov adv (%s): wifi state=%d",
             (reason != NULL) ? reason : "?", (int)st.state);
        return;
    }
    s_wifi_notified = 0;
    /*
     * 若手机仍挂着 GATT，set_adv 会失败；先断开再开广播，
     * 保证“忘记网络 / 掉线”后可被重新搜索。
     */
    ble_app_disconnect();
    d_ble_net_cfg_set_adv(1);
    LOGI(TAG, "BLE prov adv on (%s)", (reason != NULL) ? reason : "?");
}

static void on_wifi_connected(const d_event_t *evt, void *ctx)
{
    if ((evt == NULL) || (evt->id != EVT_WIFI_CONNECTED)) {
        return;
    }
    /* 始终关广播；notify 只做一次，避免切 SSID 重复刷 JSON */
    if (s_wifi_notified == 0) {
        s_wifi_notified = 1;
        LOGI(TAG, "WiFi 已连接，通知 BLE 客户端");
        ble_connected_net_notify();
    } else {
        LOGI(TAG, "WiFi 已连接，关闭 BLE 配网广播");
    }
    /* WiFi 就绪：关闭配网广播，避免被手机一直搜到 */
    d_ble_net_cfg_set_adv(0);
}

static void on_wifi_disconnected(const d_event_t *evt, void *ctx)
{
    if ((evt == NULL) || (evt->id != EVT_WIFI_DISCONNECTED)) {
        return;
    }
    LOGI(TAG, "WiFi 已断开，评估是否开启 BLE 配网广播");
    ble_svc_open_prov_adv("disconnected");
}

static void on_wifi_ssid_not_found(const d_event_t *evt, void *ctx)
{
    if ((evt == NULL) || (evt->id != EVT_WIFI_SSID_NOT_FOUND)) {
        return;
    }
    LOGW(TAG, "WiFi SSID 未找到，通知 BLE 客户端");
    d_ble_send_json_response("{\"status\":3,\"msg\":\"ssid_not_found\"}");
    ble_svc_open_prov_adv("ssid_not_found");
}

static void on_wifi_connect_fail(const d_event_t *evt, void *ctx)
{
    if ((evt == NULL) || (evt->id != EVT_WIFI_CONNECT_FAIL)) {
        return;
    }
    LOGW(TAG, "WiFi 连接失败，通知 BLE 客户端");
    d_ble_send_json_response("{\"status\":4,\"msg\":\"wrong_pwd\"}");
    ble_svc_open_prov_adv("connect_fail");
}

//*********************************************************************************//
//                              BLE → WiFi 配网请求                                 //
//*********************************************************************************//

/**
 * @brief 由 D_ble_net_cfg.c 中 JSON 处理回调调用
 * @param ssid WiFi SSID
 * @param pwd  WiFi 密码
 * @return 0=成功
 */
int ble_service_on_wifi_cfg(const char *ssid, const char *pwd)
{
    if (wifi_service_configuration_is_held()) return D_ERR_BUSY;
    wifi_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.pwd,  pwd,  sizeof(cfg.pwd)  - 1);

    LOGI(TAG, "发布 BLE 配网请求: ssid=%s", ssid);
    /* 发布配网请求事件 */
    d_event_publish(EVT_BLE_WIFI_CFG_REQUEST, &cfg, sizeof(cfg));
    return 0;
}

d_err_t ble_service_enter_reconfig_mode(void)
{
    s_wifi_notified = 0;
    ble_app_disconnect();
    d_ble_net_cfg_set_adv(1);
    LOGI(TAG, "BLE reconfig adv enabled");
    return D_OK;
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

d_err_t ble_service_init(void)
{
    LOGI(TAG, "BLE 服务初始化");

    /* 订阅 WiFi 状态事件：连上关广播，断开/失败开广播 */
    if ((d_event_subscribe(EVT_WIFI_CONNECTED, on_wifi_connected, NULL) != 0) ||
        (d_event_subscribe(EVT_WIFI_DISCONNECTED, on_wifi_disconnected, NULL) != 0) ||
        (d_event_subscribe(EVT_WIFI_SSID_NOT_FOUND, on_wifi_ssid_not_found, NULL) != 0) ||
        (d_event_subscribe(EVT_WIFI_CONNECT_FAIL, on_wifi_connect_fail, NULL) != 0)) {
        ble_service_deinit();
        LOGE(TAG, "WiFi 状态事件订阅失败");
        return D_ERR_IO;
    }

    /*
     * 注意：不能在这里调用 config_network_start()！
     * 因为 btctrler/btstack 在 Phase 5 之后才完成初始化。
     * BLE 广播必须在 BT_STATUS_INIT_OK 事件之后才能启动。
     * → 由 D_ble_net_cfg.c 中 bt_connction_status_event_handler() 处理
     */

    return D_OK;
}

void ble_service_deinit(void)
{
    d_event_unsubscribe(EVT_WIFI_CONNECTED,     on_wifi_connected);
    d_event_unsubscribe(EVT_WIFI_DISCONNECTED,  on_wifi_disconnected);
    d_event_unsubscribe(EVT_WIFI_SSID_NOT_FOUND, on_wifi_ssid_not_found);
    d_event_unsubscribe(EVT_WIFI_CONNECT_FAIL,   on_wifi_connect_fail);
}

#endif /* D_FEATURE_BLE_PROVISION */
