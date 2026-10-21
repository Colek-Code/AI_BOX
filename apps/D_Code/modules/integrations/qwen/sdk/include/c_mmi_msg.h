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

#ifndef __C_MMI_MSG_H__
#define __C_MMI_MSG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"
#include "cJSON.h"

#define C_MMI_MSG_START_FLAG            (1 << 0)
#define C_MMI_MSG_STOP_FLAG             (1 << 1)
#define C_MMI_MSG_REQ2SPK_FLAG          (1 << 2)
#define C_MMI_MSG_REQ2RESPOND_FLAG      (1 << 3)
#define C_MMI_MSG_SEND_SPEECH_FLAG      (1 << 4)
#define C_MMI_MSG_STOP_SPEECH_FLAG      (1 << 5)
#define C_MMI_MSG_RSP_START_FLAG        (1 << 6)
#define C_MMI_MSG_RSP_END_FLAG          (1 << 7)
#define C_MMI_MSG_HEARTBEAT_FLAG        (1 << 8)
#define C_MMI_MSG_UPDATE_INFO_FLAG      (1 << 9)

#define C_MMI_RESTART_FLAG              (1 << 31)

// C_MMI_REQ2RSP_TRANSCRIPT    文本转语音模式，大模型将传入的 text 文本字段转换成TTS语音下发
// C_MMI_REQ2RSP_PROMPT        询问模式，大模型将会根据传入的 text 文本进行回答，并将回答转换成TTS下发给设备
enum {
    C_MMI_REQ2RSP_TRANSCRIPT,
    C_MMI_REQ2RSP_PROMPT,
};

/**
 * @brief 获取需要通过websocket发送的数据
 * 
 * 本函数用于根据指定的类型获取数据，准备通过websocket进行发送
 * 它会根据传入的类型参数，将相应类型的数据填充到提供的数据缓冲区中
 * 
 * @param opcode 用于返回websocket数据类型，如：WS_DATA_TYPE_TEXT、WS_DATA_TYPE_BINARY
 * @param data 指向一个uint8_t数组的指针，该数组用于存储获取的数据
 * @param size 表示数据数组的最大容量，以字节为单位
 * @return 返回实际填充到数据数组中的字节数
 */
uint32_t c_mmi_get_send_data(uint8_t *opcode, uint8_t *data, uint32_t size);

/**
 * @brief 分析接收到的websocket数据函数
 * 
 * 此函数根据提供的数据类型和数据内容，分析接收到的数据包
 * 它的主要作用是解析数据内容，以便进一步处理或使用
 * 
 * @param opcode websocket数据类型，如：WS_DATA_TYPE_TEXT、WS_DATA_TYPE_BINARY
 * @param data 指向接收到的数据的指针，数据的内容将根据type参数进行解析
 * @param size 数据的长度，以字节为单位，用于确定数据的范围
 * @return 返回实际解析的字节数
 */
uint32_t c_mmi_analyze_recv_data(uint8_t opcode, uint8_t *data, uint32_t size);

/**
 * @brief 返回是否已接收所有音频数据
 * 
 * 该函数播放器判断是否还有音频数据需要播放
 * 
 * @param void 无参数
 * 
 * @return uint8_t 返回接收状态或结果
 *         - 0: 还有音频数据待读取
 *         - 1: 数据已全被接收
 */
uint8_t c_mmi_audio_recv_all(void);

int32_t c_mmi_set_param_req2rsp(uint8_t type, char* text, cJSON* parameters);
// int32_t c_mmi_set_param_update_info(cJSON* parameters);
int32_t c_mmi_set_biz_params(cJSON* biz_params);
int32_t c_mmi_update_biz_params(cJSON* biz_params);

/**
 * @brief 发送文本query到MMI模块进行处理
 *
 * 此函数用于大模型将会根据传入的 text 文本进行回答，并将回答转换成TTS语音下发
 * 通常用于实现文本交互功能
 * 
 * @param text 指向包含问题文本的字符串指针
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_question(char* text);

/**
 * @brief 将文本转换为语音播放
 *
 * 此函数用于大模型将传入的 text 文本字段转换成TTS语音下发
 * 实现文本到语音(TTS)的功能
 * 
 * @param text 指向包含待转换文本的字符串指针
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_tts(char* text);

/**
 * @brief 直接调用模型的插件
 *  (目前仅支持纯语音交互模型)
 * 通过传入query文本和用户自定义参数来调用指定插件并获取响应结果。
 * 调用完成后需要自行释放text和user_defined_params参数内存。
 *
 * @param text query文本字符串指针，不可为NULL
 * @param user_defined_params 用户自定义参数，以字符串格式传入，不可为NULL
 *                            格式: {"<plugin_id>": {"<param1>": "value1", "<param2>": "value2"}}
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 *
 * @code
 * char *json_str = "{\"<plugin_id>\":{\"<param1>\":\"value1\",\"<param2>\":\"value2\"}}";
 * int32_t ret = c_mmi_tool_call("query text", json_str);
 * @endcode
 */
int32_t c_mmi_tool_call(char* text, char* json_str);

/**
 * @brief 暂停心跳功能
 * 
 * 该函数用于暂停SDK的心跳机制
 * 
 * @param void 无参数
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_heartbeat_suspend(void);

/**
 * @brief 恢复心跳功能
 * 
 * 该函数用于恢复SDK的心跳机制
 * 
 * @param void 无参数
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_heartbeat_resume(void);

int32_t c_mmi_speech_start(void);
int32_t c_mmi_speech_end(void);
int32_t c_mmi_speech_pause(void);

/**
 * 开始语音上行。
 * 
 * 本函数用于通知云端，端侧开始进行语音数据上行。
 * 该接口可退出等待状态，也可打断当前正在进行的对话。
 * 当还处于拾音状态时，此接口无效。
 * 
 * @return 成功语音识别时返回0，否则返回非零的错误码。
 */
int32_t c_mmi_speech_start(void);

/**
 * 结束语音上行。
 * 
 * 本函数用于通知云端，端侧语音上行已结束。
 * 该进口仅在push2talk模式下有效。
 * 
 * @return 成功语音识别时返回0，否则返回非零的错误码。
 */
int32_t c_mmi_speech_end(void);

/**
 * 停止语音识别功能。
 * 
 * 本函数用于中断语音识别模块，进入等待状态。
 * 
 * @return 成功语音识别时返回0，否则返回非零的错误码。
 */
int32_t c_mmi_speech_pause(void);

/********************** 以下为旧版接口，下列接口将在2025-12-31弃用 **********************/
int32_t c_mmi_start_speech(void);   // 使用 c_mmi_speech_start 替代
int32_t c_mmi_stop_speech(void);    // 使用 c_mmi_speech_end 替代
int32_t c_mmi_pause_task(void);     // 使用 c_mmi_speech_pause 替代
/********************** 旧版接口结束 **********************/

#ifdef __cplusplus
}
#endif

#endif
