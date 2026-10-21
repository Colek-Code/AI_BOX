/**
 * @file    ai_service.h
 * @brief   AI 大模型服务接口（纯业务逻辑，禁止 LVGL）
 */

#ifndef AI_SERVICE_H
#define AI_SERVICE_H

#include "config/app_config.h"
#include "framework/d_error.h"
#include "ai_types.h"

/**
 * @brief AI 服务初始化（注入配置，订阅事件）
 */
d_err_t ai_service_init(const ai_service_config_t *config);

/**
 * @brief AI 服务销毁
 */
void ai_service_deinit(void);

#if D_DEVICE_CLOUD_ENABLE
/** @brief Register the WebSocket-to-AI event bridge. */
d_err_t ai_service_ws_bridge_init(void);
#endif

#endif /* AI_SERVICE_H */
