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

#ifndef __C_MMI_H__
#define __C_MMI_H__

#ifdef __cplusplus
extern "C" {
#endif

// websocket opcode
enum {
    WEBSOCKET_OPCODE_TEXT = 0x01,
    WEBSOCKET_OPCODE_BINARY = 0x02,
    WEBSOCKET_OPCODE_DISCONNECT = 0x08,
    WEBSOCKET_OPCODE_PING = 0x09,
    WEBSOCKET_OPCODE_PONG = 0x0A,
};

#include "c_utils.h"

#include "c_mmi_ringbuffer.h"
#include "lib_c_license.h"
#include "c_mmi_storage.h"

#include "cJSON.h"

#define C_MMI_ID_ARRAY_LEN          (C_LICENSE_ID_ARRAY_LEN)
#define C_MMI_APP_ID_STR_LEN        (C_LICENSE_APP_ID_STR_LEN)
#define C_MMI_DN_STR_MAX            (C_LICENSE_DN_STR_MAX)
#define C_MMI_TOKEN_LEN             (C_LICENSE_TOKEN_LEN)
#define C_MMI_DIALOG_ID_LEN         (C_LICENSE_DIALOG_ID_LEN)
#define C_MMI_IP_LEN                (C_LICENSE_IP_LEN)

#define C_MMI_ID_STRING_LEN         (C_MMI_ID_ARRAY_LEN * 2)

#define C_MMI_WORKSPACE_ID_LEN      (20)                            // workspace id 20B
#define C_MMI_API_KEY_LEN           (3 + C_MMI_ID_STRING_LEN)       // 35

#define C_MMI_CITY_LEN              (32)    // max 31+1

enum {
    C_MMI_EVENT_USER_CONFIG,        // 用户对于sdk的配置应该在该事件回调中实现，如音频缓冲区大小、工作模式、音色等
    C_MMI_EVENT_DATA_INIT,	        // 当SDK完成初始化后触发该事件，可在该事件回调中开始建立业务连接
    C_MMI_EVENT_SPEECH_READY,	    // 当正确建立WSS连接后触发该事件，在push和tap模式下仅在该事件后才可以调用speech start
    C_MMI_EVENT_SPEECH_PREPARE,     // 当SDK已准备好可以开始新一轮对话时触发此事件
    C_MMI_EVENT_SPEECH_START,       // 当SDK开始进行音频上行时触发此事件
    C_MMI_EVENT_SPEECH_RESTART,     // 当SDK重新开始进行音频上行时触发此事件
    C_MMI_EVENT_DATA_DEINIT,	    // 当SDK注销后触发此事件

    C_MMI_EVENT_ASR_START,	        // 当ASR开始返回数据时触发此事件
    C_MMI_EVENT_ASR_INCOMPLETE,	    // 此事件返回尚未完成ASR的文本数据（全量）
    C_MMI_EVENT_ASR_COMPLETE,	    // 此事件返回完成ASR的全部文本数据（全量）
    C_MMI_EVENT_ASR_END,		    // 当ASR结束时触发此事件

    C_MMI_EVENT_LLM_INCOMPLETE,	    // 此事件返回尚未处理完成的LLM文本数据（全量）
    C_MMI_EVENT_LLM_COMPLETE,	    // 此事件返回处理完成的LLM全部文本数据（全量）

    C_MMI_EVENT_TTS_START,	        // 当开始音频下行时触发此事件
    C_MMI_EVENT_TTS_END,	        // 当音频完成下行时触发此事件

    C_MMI_EVENT_HEARTBEAT,	        // 当SDK收到云端心跳回复时触发此事件
};

enum {
    C_MMI_STATE_NO_INIT,
    C_MMI_STATE_INIT,
    C_MMI_STATE_WAIT_START,
    C_MMI_STATE_STARTED,
    C_MMI_STATE_LISTEN_PREPARE,
    C_MMI_STATE_LISTENING,
    C_MMI_STATE_WAIT_THINK,
    C_MMI_STATE_THINKING,
    C_MMI_STATE_RESPONDING,
    C_MMI_STATE_PLAYING,
    C_MMI_STATE_IDLE,
    C_MMI_STATE_WAIT_RESTART,
    C_MMI_STATE_WAIT_STOP,
    C_MMI_STATE_WAIT_DEINIT,
};

typedef struct _c_mmi_prompt_pram_t { 
    char *key;
    char *value;
} c_mmi_prompt_pram_t;

typedef struct _mmi_info_t {
    uint8_t sdk_enable;

    c_mmi_ringbuffer_t *recorder_rb;
    c_mmi_ringbuffer_t *player_rb;

    // 云端获取
    char *header;
    char ws_id[C_MMI_WORKSPACE_ID_LEN + 1];
    char app_id[C_MMI_APP_ID_STR_LEN + 1];

    // 设备信息
    char ip[C_MMI_IP_LEN];
    double longitude;
    double latitude;
    char city[C_MMI_CITY_LEN];

    // 随机生成
    uint8_t task_id[C_MMI_ID_ARRAY_LEN];

    uint8_t state;
    uint32_t cmd_flag;
    uint32_t last_cmd;

    int64_t last_hb_time;
    int64_t last_hb_recv_time;
    uint8_t speech_work;

    uint8_t audio_recv_all;
    uint32_t audio_recv_data_size;

    uint8_t takeover_mode;

    cJSON *prompt_param;
    cJSON *req2rsp;
    // cJSON *update_info;
    cJSON *images;
    cJSON *biz_params;
} mmi_info_t;

mmi_info_t *c_mmi_get_info(void);

uint8_t c_mmi_get_state(void);

/**
 * @brief 获取WSS服务器主机名字符串。
 *
 * 此函数返回WSS服务器主机名字符串。
 * 
 * @return char* 返回指向WSS服务器主机名字符串的指针
 */
char *c_mmi_get_wss_host(void);

/**
 * @brief 获取WSS海外服务器主机名字符串。
 *
 * 此函数返回WSS海外服务器主机名字符串。
 * 
 * @return char* 返回指向WSS海外服务器主机名字符串的指针
 */
char *c_mmi_get_wss_host_global(void);

/**
 * @brief 获取WSS服务器端口字符串。
 *
 * 此函数返回WSS服务器端口字符串。
 * 
 * @return char* 返回指向WSS服务器端口字符串的指针
 */
char *c_mmi_get_wss_port(void);

/**
 * @brief 获取WSS服务API路径字符串。
 *
 * 此函数返回WSS服务API路径字符串。
 * 
 * @return char* 返回指向WSS服务API路径字符串的指针
 */
char *c_mmi_get_wss_api(void);

/**
 * @brief 获取WSS请求头信息字符串。
 *
 * 此函数返回WSS请求头信息字符串。
 * 
 * @return char* 
 *         返回指向WSS请求头信息字符串的指针
 */
char *c_mmi_get_wss_header(void);

/**
 * @brief 设置设备的IP地址信息
 *
 * 此函数用于设置设备的IP地址，该信息将被包含在客户端信息中发送给服务端
 * 
 * @param ip 指向IP地址字符串的指针
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_ip(char *ip);

/**
 * @brief 设置设备的地理位置坐标
 *
 * 此函数用于设置设备的经度和纬度信息，该信息将被包含在客户端信息中发送给服务端
 * 
 * @param longitude 经度值
 * @param latitude 纬度值
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_postion(double longitude, double latitude);

/**
 * @brief 设置设备所在城市信息
 *
 * 此函数用于设置设备所在的城市信息，该信息将被包含在客户端信息中发送给服务端
 * 
 * @param city 指向城市名称字符串的指针
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_city(char *city);

int32_t c_mmi_init(char *workspace, char *app_id, char *api_key);

/**
 * @brief 反初始化MMI模块
 *
 * 此函数用于释放MMI模块占用的所有资源，并重置模块状态
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_deinit(void);

/**
 * @brief 配置提示词参数
 *
 * 此函数用于配置用户自定义的提示词参数
 * 
 * @param param_list 指向提示词参数结构体数组的指针
 * @param list_size 参数数组中元素的个数
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_data_set_prompt_param(c_mmi_prompt_pram_t *param_list, uint32_t list_size);

/**
 * @brief 向录音缓冲区写入数据
 *
 * @param data 待写入的数据指针
 * @param size 数据长度（字节）
 * @return uint32_t 实际写入的字节数
 */
uint32_t c_mmi_put_recorder_data(uint8_t *data, uint32_t size);

/**
 * @brief 从播放缓冲区读取数据
 *
 * @param data 用于存储读取数据的缓冲区
 * @param size 可用缓冲区大小（字节）
 * @return uint32_t 实际读取的字节数
 */
uint32_t c_mmi_get_player_data(uint8_t *data, uint32_t size);

uint8_t c_mmi_is_working(void);

/**
 * @brief 初始化MMI
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_sdk_init(void);

int32_t c_mmi_agent_takeover(uint8_t onoff);

#include "c_mmi_config.h"
#include "c_mmi_msg.h"

#ifdef __cplusplus
}
#endif

#endif
