/**
 * @file    provision_service.h
 * @brief   设备首配服务 — HTTP 四步流程
 * @note    依赖：d_crypto, device_credentials, httpcli(SDK), d_event_bus
 *          不依赖：ws_service, ai_service
 *
 * 流程：init → claim → poll status → finalize → 写 VM → 发布 EVT_PROVISION_OK
 */

#ifndef PROVISION_SERVICE_H
#define PROVISION_SERVICE_H

#include "framework/d_error.h"

/**
 * @brief  初始化首配服务并订阅事件
 *         会等待 EVT_NTP_SYNCED 后自动启动首配流程
 * @return D_OK 成功
 */
d_err_t provision_service_init(void);

/**
 * @brief  手动触发首配流程（调试用）
 * @return D_OK 已启动，D_ERR_BUSY 正在进行
 */
d_err_t provision_start(void);

#endif /* PROVISION_SERVICE_H */
