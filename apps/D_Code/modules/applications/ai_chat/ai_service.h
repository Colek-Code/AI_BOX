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
 * @brief 复制配置描述、订阅请求及 WiFi 事件并创建长期后台任务。
 * @param[in] config 借用配置；结构被复制，但其中字符串仍借用，host/path/model 须为有效文本。
 * @return 空配置或 API Key 指针为 INVALID_ARG；订阅或任务创建失败为 IO；启动受理成功为 D_OK。
 * @note 字符串须覆盖后台任务寿命；函数不保证重复初始化幂等，也未在此验证全部字段或等待网络就绪。
 */
d_err_t ai_service_init(const ai_service_config_t *config);

/**
 * @brief 解除三类事件订阅并关闭当前 Keep-Alive 连接。
 * @note 当前实现不停止已经创建的 ai_svc 任务，也不排空在途请求；调用后仍须保留静态任务及配置字符串，不宣称服务完整卸载。
 */
void ai_service_deinit(void);

#endif /* AI_SERVICE_H */
