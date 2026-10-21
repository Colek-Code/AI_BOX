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

// 该头文件为兼容旧版本接口，推荐使用新头文件c_mmi.h

#ifndef __LIB_C_SDK_H__
#define __LIB_C_SDK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_mmi.h"

#define C_SDK_REQ_LEN_REGISTER      500
#define C_SDK_REQ_LEN_GET_TOKEN     500

/********************** 下列接口将在2026-12-31弃用 **********************/
// 推荐使用 c_license_gen_register_str 代替
int32_t c_device_gen_register_req(char req_str[C_SDK_REQ_LEN_REGISTER], char *time_ms_str);

// 推荐使用 c_license_analyze_register_rsp + c_mmi_storage_save 代替
int32_t c_device_analyze_register_rsp(char *rsp_str);

// 推荐使用 c_license_is_token_expire 代替
uint8_t c_mmi_is_token_expire(void);

// 推荐使用 c_license_gen_get_token_str 代替
int32_t c_mmi_gen_get_token_req(char req_str[C_SDK_REQ_LEN_GET_TOKEN], char *time_ms_str);

// 推荐使用 c_license_analyze_get_token_rsp 代替
int32_t c_mmi_analyze_get_token_rsp(char *rsp_str);

// 推荐使用 c_license_device_is_registered 代替
uint8_t c_storage_device_is_registered(void);

// 推荐使用 c_mmi_storage_save 代替
int32_t c_storage_save(void);

// 推荐使用 c_mmi_storage_reset 代替
int32_t c_storage_reset(void);

// 推荐使用 c_mmi_storage_set_app_id_str 代替
int32_t c_storage_set_app_id_str(char *app_id_str);

// 推荐使用 c_license_set_app_secret_str 代替
int32_t c_storage_set_app_secret_str(char *app_secret_str);

// 推荐使用 c_mmi_set_device_name 代替
int32_t c_storage_set_device_name(char *device_name);

// 推荐使用 c_mmi_storage_get_app_id_str 代替
int32_t c_storage_get_app_id_str(char *app_id_str);

// 推荐使用 c_license_get_app_secret 代替
int32_t c_storage_get_app_secret(uint8_t *app_secret);

// 推荐使用 c_mmi_storage_get_device_name 代替
int32_t c_storage_get_device_name(char *deviceName);

// 已移除该接口，通过是否加载libc_license.a自动判断是否启用license模式
int32_t c_license_disable(void);

/********************** 上述接口将在2026-12-31弃用 **********************/

/********************** 下列接口将在2025-12-31弃用 **********************/
enum {
    WS_DATA_TYPE_TEXT = 0x01,       // 使用 WEBSOCKET_OPCODE_TEXT 替代
    WS_DATA_TYPE_BINARY = 0x02,     // 使用 WEBSOCKET_OPCODE_BINARY 替代
    WS_DATA_TYPE_DISCONNECT = 0x08, // 使用 WEBSOCKET_OPCODE_DISCONNECT 替代
    WS_DATA_TYPE_PING = 0x09,       // 使用 WEBSOCKET_OPCODE_PING 替代
    WS_DATA_TYPE_PONG = 0x0A,       // 使用 WEBSOCKET_OPCODE_PONG 替代
};

// 推荐使用 c_mmi_deinit 代替
int32_t c_mmi_data_deinit(void);
/********************** 上述接口将在2025-12-31弃用 **********************/

#ifdef __cplusplus
}
#endif

#endif
