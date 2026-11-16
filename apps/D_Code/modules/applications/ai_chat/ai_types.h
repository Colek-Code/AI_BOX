/**
 * @file    ai_types.h
 * @brief   AI Chat 模块内共享类型定义
 */

#ifndef AI_TYPES_H
#define AI_TYPES_H

#include "framework/d_event_payloads.h"

//*********************************************************************************//
//                              AI 服务配置                                         //
//*********************************************************************************//

typedef struct {
    const char *api_key; /**< 借用的 API Key 字符串，必须覆盖后台任务寿命。 */
    const char *api_host; /**< 借用的 HTTPS 主机名，必须覆盖后台任务寿命。 */
    const char *api_path; /**< 借用的请求路径，必须覆盖后台任务寿命。 */
    const char *model; /**< 借用的平台模型名，必须覆盖后台任务寿命。 */
    int         timeout_ms; /**< 传给 SDK 的请求超时毫秒数，由调用方提供有效值。 */
    int         max_retries; /**< 保留配置字段，当前实现未读取；HTTP 路径固定最多重连重试一次。 */
} ai_service_config_t;

#endif /* AI_TYPES_H */
