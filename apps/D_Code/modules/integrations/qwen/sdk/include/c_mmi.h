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

#define C_MMI_WORKSPACE_ID_LEN      (C_LICENSE_WORKSPACE_ID_LEN)    // workspace id 20B
#define C_MMI_API_KEY_LEN           (256)                           // 20260331-新发布

#define C_MMI_CITY_LEN              (32)    // max 31+1

enum {
    C_MMI_EVENT_USER_CONFIG,        // 用户对于sdk的配置应该在该事件回调中实现，如音频缓冲区大小、工作模式、音色等
    C_MMI_EVENT_STATE_CHANGE,      // major state 切换时触发，param = uint8_t* 指向新状态值

    C_MMI_EVENT_DATA_INIT,	        // 当SDK完成初始化后触发该事件，可在该事件回调中开始建立业务连接
    C_MMI_EVENT_DATA_DEINIT,	    // 当SDK注销后触发此事件
    
    C_MMI_EVENT_SPEECH_READY,	    // 当正确建立WSS连接后触发该事件，在push和tap模式下仅在该事件后才可以调用speech start
    C_MMI_EVENT_SPEECH_PREPARE,     // 当SDK已准备好可以开始新一轮对话时触发此事件
    C_MMI_EVENT_SPEECH_START,       // 当SDK开始进行音频上行时触发此事件
    C_MMI_EVENT_SPEECH_INTERRUPT,	// 当当前会话被打断时触发该事件，如对话一半按键打断，对话一半有其他请求等

    C_MMI_EVENT_ASR_START,	        // 当ASR开始返回数据时触发此事件
    C_MMI_EVENT_ASR_INCOMPLETE,	    // 此事件返回尚未完成ASR的文本数据（全量）
    C_MMI_EVENT_ASR_COMPLETE,	    // 此事件返回完成ASR的全部文本数据（全量）
    C_MMI_EVENT_ASR_END,		    // 当ASR结束时触发此事件

    C_MMI_EVENT_LLM_INCOMPLETE,	    // 此事件返回尚未处理完成的LLM文本数据（全量）
    C_MMI_EVENT_LLM_COMPLETE,	    // 此事件返回处理完成的LLM全部文本数据（全量）
    C_MMI_EVENT_WORD_TIMESTAMP,	    // 此事件返回完整word_timestamps cJSON对象（流式更新），由应用按格式消费，param仅回调期间有效，需使能word_timestamp
    C_MMI_EVENT_LATENCY,            // 当收到云端下发的延迟统计时触发此事件，值为ASR结束到TTS首包生成的耗时（仅含云端时间），param指向uint32_t毫秒值，仅回调期间有效

    C_MMI_EVENT_TTS_START,	        // 当开始音频下行时触发此事件
    C_MMI_EVENT_TTS_END,	        // 当音频完成下行时触发此事件

    C_MMI_EVENT_HEARTBEAT,	        // 当SDK收到云端心跳回复时触发此事件

    C_MMI_EVENT_ERROR,	            // 当SDK收到云端返回错误时触发此事件
};

// MAJOR STATE
enum {
    C_MMI_STATE_NO_INIT,
    C_MMI_STATE_INIT,
    C_MMI_STATE_LISTENING,
    C_MMI_STATE_THINKING,
    C_MMI_STATE_RESPONDING,
    C_MMI_STATE_RESTART,
    C_MMI_STATE_DEINIT,
};

enum {
    C_MMI_STATE_INIT_WAIT_SEND,
    C_MMI_STATE_INIT_WAIT_START,
    C_MMI_STATE_INIT_STARTED,
    C_MMI_STATE_INIT_DONE,

    C_MMI_STATE_LISTENING_IDEL,
    C_MMI_STATE_LISTENING_WAIT_SEND,
    C_MMI_STATE_LISTENING_WAIT_ASR,
    C_MMI_STATE_LISTENING_ASR,
    C_MMI_STATE_LISTENING_WAIT_END,
    C_MMI_STATE_LISTENING_ASR_END,

    C_MMI_STATE_THINKING_WAIT_LLM,
    C_MMI_STATE_THINKING_LLM,
    C_MMI_STATE_THINKING_LLM_END,

    C_MMI_STATE_RESPONDING_WAIT_AUDIO,
    C_MMI_STATE_RESPONDING_AUDIO,
    C_MMI_STATE_RESPONDING_AUDIO_END,
    C_MMI_STATE_RESPONDING_WAIT_PLAY,

    C_MMI_STATE_RESTART_WAIT_SEND,
    C_MMI_STATE_RESTART_WAIT_ACCPECT,

    C_MMI_STATE_DEINIT_WAIT_SEND,
    C_MMI_STATE_DEINIT_WAIT_STOP,
    C_MMI_STATE_DEINIT_STOPED,
};

typedef struct _mmi_err_info_t {
    int32_t code;
    char *name;
    char *msg;
} c_mmi_err_info_t;

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
 * @brief 初始化mmi
 * 
 * 该函数用于初始化mmi，仅后付费模式需要调用该接口
 * 
 * @param workspace WorkSpaceId，通过百炼平台获取
 * @param app_id AppId，由阿里云颁发，字符串格式
 * @param api_key 通过百炼平台获取
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
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
 * @brief 初始化 MMI
 * 
 * 该函数完成协议握手、设备注册、token 获取等初始化流程。
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_sdk_init(void);

#include "c_mmi_config.h"
#include "c_mmi_msg.h"

#ifdef __cplusplus
}
#endif

#endif
