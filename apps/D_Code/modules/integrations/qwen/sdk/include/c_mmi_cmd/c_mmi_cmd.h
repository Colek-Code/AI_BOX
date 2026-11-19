/*
 * Copyright 2025 Alibaba Group Holding Ltd.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __C_MMI_CMD_H__
#define __C_MMI_CMD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"
#include "c_mmi_cmd_string.h"

#include "cJSON.h"

#define C_MMI_CMD_VALUE_UNDEFINED       0x7FFFFFFF

typedef int32_t(*c_mmi_cmd_func)(char *param, char *req_id);
typedef int32_t(*c_mmi_cmd_event_callback)(uint32_t event, char *req_id, void *params);

/**
 * @brief Bypass 模式回调函数类型
 * 
 * @param cmd 指令名称（多模态："domain/cmd_name"，语音应用："cmd_name"）
 * @param param 原始参数字符串（JSON 格式）
 * @return int32_t 处理结果
 */
typedef int32_t(*c_mmi_cmd_bypass_func)(char *cmd, char *param, char *req_id);

/**
 * @brief 注册 bypass 回调函数
 * 
 * @param func bypass 回调函数指针
 * @return int32_t UTIL_SUCCESS 成功，其他 失败
 */
int32_t c_mmi_cmd_bypass_register(c_mmi_cmd_bypass_func func);

/**
 * @brief 注销 bypass 回调函数
 * 
 * @return int32_t UTIL_SUCCESS 成功，其他 失败
 */
int32_t c_mmi_cmd_bypass_unregister(void);

/**
 * @brief 解析云端下发的指令参数
 * 
 * @param extra_info cJSON 格式的额外信息对象
 * @return int32_t UTIL_SUCCESS 成功，其他失败
 */
int32_t c_mmi_cmd_analyze(cJSON *extra_info);

/**
 * @brief 注册指令回调函数
 * 
 * @param cmd 指令名称字符串
 * @param func 指令处理回调函数指针
 * @return int32_t UTIL_SUCCESS 成功，其他失败
 */
int32_t c_mmi_cmd_register(char *cmd, c_mmi_cmd_func func);

/**
 * @brief 注销指令回调函数
 * 
 * @param cmd 指令名称字符串
 * @return int32_t UTIL_SUCCESS 成功，其他失败
 */
int32_t c_mmi_cmd_unregister(char *cmd);

/**
 * @brief 注册多模态应用指令回调函数
 * 
 * @param domain_name 域名（默认使用 C_MM_DOMAIN_GEN_CMD_STR）
 * @param cmd 多模态应用指令名称字符串
 * @param func 指令处理回调函数指针
 * @return int32_t UTIL_SUCCESS 成功，其他失败
 */
int32_t c_mm_cmd_register(char *domain_name, char *cmd, c_mmi_cmd_func func);

/**
 * @brief 注销多模态应用指令回调函数
 * 
 * @param domain_name 域名字符串
 * @param cmd 多模态应用名称字符串
 * @return int32_t UTIL_SUCCESS 成功，其他失败
 */
int32_t c_mm_cmd_unregister(char *domain_name, char *cmd);

/**
 * @brief 注册纯语音应用指令回调函数
 * 
 * @param name 纯语音应用指令名称字符串
 * @param func 指令处理回调函数指针
 * @return int32_t UTIL_SUCCESS 成功，其他失败
 */
int32_t c_ao_cmd_register(char *name, c_mmi_cmd_func func);

/**
 * @brief 注销纯语音应用指令回调函数
 * 
 * @param name 纯语音应用指令名称字符串
 * @return int32_t UTIL_SUCCESS 成功，其他失败
 */
int32_t c_ao_cmd_unregister(char *name);

#ifdef __cplusplus
}
#endif

#endif
