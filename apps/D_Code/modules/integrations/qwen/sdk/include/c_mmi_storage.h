/*
 * Copyright 2025 Alibaba Group Holding Ltd.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http: *www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __C_STORAGE_H__
#define __C_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"

int32_t c_mmi_storage_init(void);

/**
 * @brief 重置配置
 * 
 * 此函数用于清除所有已保存的设备配置信息
 * 调用此函数后，配置将恢复为默认状态，需要重新设置相关参数
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_reset(void);

/**
 * @brief 保存配置
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_save(void);

/**
 * @brief 设置AppId
 * 
 * 此函数用于设置AppId，完成设置后需要调用c_mmi_storage_save进行保存
 * 
 * @param app_id_str AppId，由阿里云颁发，字符串格式
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_set_app_id_str(char *app_id_str);

/**
 * @brief 设置ApiKey
 * 
 * 此函数用于设置ApiKey，完成设置后需要调用c_mmi_storage_save进行保存
 * 
 * @param api_key 通过百炼平台获取
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_set_api_key(char *api_key);

/**
 * @brief 设置WorkSpaceId
 * 
 * 此函数用于设置WorkSpaceId，完成设置后需要调用c_mmi_storage_save进行保存
 * 
 * @param ws_id WorkSpaceId，通过百炼平台获取
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_set_ws_id(char *ws_id);

/**
 * @brief 设置设备名称DeviceName
 * 
 * 此函数用于设置设备名称DeviceName，完成设置后需要调用c_mmi_storage_save进行保存
 * 
 * @param dn 设备名称DeviceName，用户可自行设定，长度不超过32字符
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_set_device_name(char *device_name);
int32_t c_mmi_storage_set_dialog_id(char *dialog_id);

int32_t c_mmi_storage_get_app_id_str(char *app_id_str);
int32_t c_mmi_storage_get_api_key(char *api_key);
int32_t c_mmi_storage_get_ws_id(char *ws_id);
int32_t c_mmi_storage_get_device_name(char *device_name);
int32_t c_mmi_storage_get_dialog_id(char *dialog_id);

uint8_t c_mmi_storage_check_app_id(char *app_id);
uint8_t c_mmi_storage_check_device_name(char *device_name);

#ifdef __cplusplus
}
#endif

#endif
