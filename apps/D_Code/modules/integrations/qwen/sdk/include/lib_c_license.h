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

#ifndef __LIB_C_LICENSE_H__
#define __LIB_C_LICENSE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STORAGE_FLAG_APP_ID         (1 << 0)
#define STORAGE_FLAG_APP_SECRET     (1 << 1)
#define STORAGE_FLAG_DEVICE_NAME    (1 << 2)
#define STORAGE_FLAG_NONCE          (1 << 3)
#define STORAGE_FLAG_DEVICE_SECRET  (1 << 4)
#define STORAGE_FLAG_DIALOG_ID      (1 << 5)
#define STORAGE_FLAG_WS_ID          (1 << 6)
#define STORAGE_FLAG_API_KEY        (1 << 7)

#define C_LICENSE_ID_ARRAY_LEN          (16)
#define C_LICENSE_APP_ID_STR_LEN        (C_LICENSE_ID_ARRAY_LEN * 2)            // 32
#define C_LICENSE_APP_SECRET_LEN        (C_LICENSE_ID_ARRAY_LEN)
#define C_LICENSE_DN_STR_MAX            (32)
#define C_LICENSE_DEVICE_SECRET_LEN     (C_LICENSE_ID_ARRAY_LEN)
#define C_LICENSE_DIALOG_ID_LEN         (32 + 4)                                // 32+4
#define C_LICENSE_IP_LEN                (16)                                    // max 15+1

enum {
    LICENSE_SUCCESS = 0,
    LICENSE_ERR_FAIL,
    LICENSE_ERR_UNEXPECT,
    LICENSE_ERR_UNDEFINE,
    LICENSE_ERR_INVALID_PARAM,
    LICENSE_ERR_NOT_FOUND,
    LICENSE_ERR_NO_MEMORY,
    LICENSE_ERR_ALREADY,
    LICENSE_ERR_TIMEOUT,
    LICENSE_ERR_NO_PERMISSION,
    LICENSE_ERR_IO,
    LICENSE_ERR_INIT_FAIL,
    LICENSE_ERR_UNSUPPORT,
    LICENSE_ERR_NO_INIT,
    LICENSE_ERR_NO_READY,
    LICENSE_ERR_BUSY,
};

uint8_t c_license_is_enable(void);

int32_t c_license_init(uint8_t *data, uint32_t size);
int32_t c_license_reset(void);

int32_t c_license_set_app_id_str(char *app_id_str);

/**
 * @brief 设置AppSecret
 * 此函数用于设置AppSecret，完成设置后需要调用c_mmi_storage_save进行保存
 * 
 * @param app_secret AppSecret，由阿里云颁发，字符串格式
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_license_set_app_secret_str(char *app_secret);
int32_t c_license_set_device_name(char *device_name);
int32_t c_license_set_device_secret_str(char *device_secret_str);
int32_t c_license_set_dialog_id(char *dialog_id);

int32_t c_license_get_app_id_str(char *app_id_str);
int32_t c_license_get_app_secret(uint8_t *app_secret);
int32_t c_license_get_device_name(char *device_name);
int32_t c_license_get_dialog_id(char *dialog_id);
uint32_t c_license_get_storage_data(uint8_t *data, uint32_t size);

/**
 * @brief 检查设备是否已注册
 * 
 * @return uint8_t 设备注册状态
 *         - 0: 设备未注册
 *         - 1: 设备已注册
 */
uint8_t c_license_device_is_registered(void);

/**
 * @brief 生成注册字符串
 * 
 * @param buffer 用于存储生成的注册字符串的缓冲区
 * @param buffer_size 缓冲区大小
 * @param time_ms_str 时间戳字符串，单位为毫秒
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_license_gen_register_str(char *buffer, uint32_t buffer_size, char *time_ms_str);

/**
 * @brief 解析云端的设备注册响应信息。
 * 
 * @param rsp_str 指向设备注册响应字符串的指针。
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_license_analyze_register_rsp(char *rsp_str);

/**
 * @brief 检查token是否已过期。
 * 
 * @param now_ms 时间戳数值，单位为毫秒。
 * @return uint8_t 返回1表示token有效，0表示token已过期。
 */
uint8_t c_license_is_token_expire(int64_t now_ms);

/**
 * @brief 生成获取token的请求数据。
 *
 * 此函数根据提供的参数生成获取token的请求字符串，用于向服务器请求访问令牌。
 *
 * @param buffer 用于存储生成的请求字符串的缓冲区
 * @param buffer_size 缓冲区的大小
 * @param time_ms_str 时间戳字符串，单位为毫秒
 * @param api_key API密钥字符串，半托管模式api_key设置为NULL
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_license_gen_get_token_str(char *buffer, uint32_t buffer_size, char *time_ms_str, char *api_key);

/**
 * @brief 解析登录响应数据。
 *
 * 解析登录响应字符串，并触发事件：
 * - C_MMI_EVENT_USER_CONFIG: 用户配置初始化
 * - C_MMI_EVENT_DATA_INIT: 数据初始化完成
 *
 * @param rsp_str 包含登录响应的字符串
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_license_analyze_get_token_rsp(char *rsp_str);

/**
 * @brief 检查token是否已过期。
 * 
 * @return uint8_t 返回1表示token有效，0表示token已过期。
 */
uint8_t c_license_is_token_expire(int64_t now_ms);

int32_t c_license_mmi_gen_signature(char *buffer, uint32_t size, char *timestamp, uint8_t *taskid);

#ifdef __cplusplus
}
#endif

#endif
