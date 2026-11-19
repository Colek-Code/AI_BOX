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

#ifndef __C_STORAGE_H__
#define __C_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"

/**
 * @brief 设置系统可存储的设备信息数上限
 *
 * 必须在 c_mmi_storage_init 之前调用，设置后不可更改。
 *
 * @param count 设备数上限，必须 > 0
 * @return int32_t 0成功，UTIL_ERR_UNSUPPORT 已设置，UTIL_ERR_INVALID_PARAM 参数无效
 */
int32_t c_mmi_storage_set_count(uint8_t count);

/**
 * @brief 指定当前使用的设备信息 ID 及 license 模式
 *
 * 必须在 c_mmi_storage_set_count 之后、c_mmi_storage_init 之前调用。
 * 后续 init / load / save 均作用于该 ID 对应的存储槽位。
 *
 * @param id 设备 ID，取值范围 [0, count)
 * @param license_enable 是否启用 license 模式
 * @return int32_t 0成功，UTIL_ERR_NO_READY 未调用 set_count，UTIL_ERR_INVALID_PARAM 参数越界
 */
int32_t c_mmi_storage_active(uint8_t id, uint8_t license_enable);

/**
 * @brief 初始化配置存储模块
 *
 * 加载持久化的设备配置（AppId / ApiKey / WorkSpaceId / DeviceName 等）到内存缓存，
 * 供后续 c_mmi_storage_get_xxx 系列接口读取。
 * 在调用任何其他 c_mmi_storage_xxx 接口之前必须先调用此函数。
 *
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_init(void);

/**
 * @brief 重置配置
 * 
 * 此函数用于清除所有已保存的设备配置信息
 * 调用此函数后，配置将恢复为默认状态，需要重新设置相关参数
 * 
 * @note 该接口仅清除内存中的配置数据，不会自动持久化到 flash；
 *       如需重启后仍保持重置状态，调用方必须显式调用 c_mmi_storage_save()。
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_reset(void);

/**
 * @brief 保存配置
 * 
 * 将内存中的设备配置持久化存储。
 * 调用 c_mmi_storage_set_xxx 系列接口设置参数后，必须调用此函数才能保存生效。
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

/**
 * @brief 设置对话 ID
 * 
 * @param dialog_id 对话 ID 字符串，长度不超过 C_LICENSE_DIALOG_ID_LEN
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_set_dialog_id(char *dialog_id);

/**
 * @brief 获取 AppId 字符串
 * 
 * @param app_id_str 用于接收 AppId 的缓冲区指针，大小不小于 C_MMI_APP_ID_STR_LEN + 1
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_get_app_id_str(char *app_id_str);

/**
 * @brief 获取 ApiKey
 * 
 * @param api_key 用于接收 ApiKey 的缓冲区指针
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_get_api_key(char *api_key);

/**
 * @brief 获取 WorkSpaceId
 * 
 * @param ws_id 用于接收 WorkSpaceId 的缓冲区指针，大小不小于 C_MMI_WORKSPACE_ID_LEN + 1
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_get_ws_id(char *ws_id);

/**
 * @brief 获取设备名称
 * 
 * @param device_name 用于接收设备名称的缓冲区指针，大小不小于 C_MMI_DN_STR_MAX + 1
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_get_device_name(char *device_name);

/**
 * @brief 获取对话 ID
 * 
 * @param dialog_id 用于接收对话 ID 的缓冲区指针，大小不小于 C_MMI_DIALOG_ID_LEN + 1
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_storage_get_dialog_id(char *dialog_id);

/**
 * @brief 检查 AppId 是否有效
 * 
 * @param app_id AppId 字符串指针
 * @return uint8_t 1表示有效，0表示无效
 */
uint8_t c_mmi_storage_check_app_id(char *app_id);

/**
 * @brief 检查设备名称是否有效
 * 
 * @param device_name 设备名称字符串指针
 * @return uint8_t 1表示有效，0表示无效
 */
uint8_t c_mmi_storage_check_device_name(char *device_name);

#ifdef __cplusplus
}
#endif

#endif
