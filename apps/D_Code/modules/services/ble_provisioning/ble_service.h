/**
 * @file    ble_service.h
 * @brief   以 WiFi 执行器受理与事件通知桥接 BLE 配网。
 * @note    GATT 操作保留在 D_ble_net_cfg.c（SDK 深度绑定）
 *          此模块提供事件驱动的桥接层
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "framework/d_error.h"

/**
 * @brief 订阅 WiFi 状态事件，建立 BLE 配网的事件桥接。
 * @return 订阅成功返回 D_OK；任一订阅失败时撤销本服务订阅并返回 D_ERR_IO。
 * @note 不在此开启广播；必须等待 BT_STATUS_INIT_OK，由协议栈事件启动。
 */
d_err_t ble_service_init(void);

/**
 * @brief 向 WiFi 执行器提交收到的 BLE 配网意图。
 * @param[in] ssid 非空 SSID，复制到 WiFi 配网缓冲。
 * @param[in] pwd 非空密码字符串，允许空串表示开放网络。
 * @return 成功受理返回 D_OK；参数、维护占用或 WiFi 受理错误返回实际错误。
 * @note D_OK 仅表示请求受理，连接结果仍由 WiFi 状态及 BLE 通知确认。
 */
int ble_service_on_wifi_cfg(const char *ssid, const char *pwd);

/**
 * @brief 显式请求断开手机并重新开启配网广播。
 * @return 当前实现返回 D_OK，不确认广播命令最终生效。
 * @note 此显式入口绕过 WiFi 忙碌状态检查；仍有 GATT 连接时由断连完成事件续开广播。
 */
d_err_t ble_service_enter_reconfig_mode(void);

/**
 * @brief 撤销本服务持有的四类 WiFi 事件订阅。
 */
void ble_service_deinit(void);

#endif /* BLE_SERVICE_H */
