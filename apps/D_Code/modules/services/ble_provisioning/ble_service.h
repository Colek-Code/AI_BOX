/**
 * @file    ble_service.h
 * @brief   BLE 配网服务模块接口
 * @note    GATT 操作保留在 D_ble_net_cfg.c（SDK 深度绑定）
 *          此模块提供事件驱动的桥接层
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "framework/d_error.h"

/**
 * @brief BLE 服务初始化（订阅 WiFi 事件，注册 BLE 配网回调）
 */
d_err_t ble_service_init(void);

/**
 * @brief BLE 收到配网请求后，发布 EVT_BLE_WIFI_CFG_REQUEST 事件
 * @note  由 D_ble_net_cfg.c 中 JSON 处理回调调用
 * @param ssid WiFi SSID
 * @param pwd  WiFi 密码
 * @return 0=成功
 */
int ble_service_on_wifi_cfg(const char *ssid, const char *pwd);

/**
 * @brief WiFi 忘记/断网后重新开启 BLE 配网广播，便于手机重新搜索
 */
d_err_t ble_service_enter_reconfig_mode(void);

/**
 * @brief BLE 服务销毁
 */
void ble_service_deinit(void);

#endif /* BLE_SERVICE_H */
