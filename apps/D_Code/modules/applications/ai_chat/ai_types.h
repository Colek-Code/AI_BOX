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
    const char *api_key;
    const char *api_host;
    const char *api_path;
    const char *model;
    int         timeout_ms;
    int         max_retries;
} ai_service_config_t;

#endif /* AI_TYPES_H */
