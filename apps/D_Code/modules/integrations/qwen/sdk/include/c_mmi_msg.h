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

#ifndef __C_MMI_MSG_H__
#define __C_MMI_MSG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"
#include "c_mmi_ringbuffer.h"
#include "cJSON.h"

#define C_MMI_MSG_START_FLAG            (1 << 0)
#define C_MMI_MSG_CANCEL_FLAG           (1 << 1)
#define C_MMI_MSG_STOP_FLAG             (1 << 2)
#define C_MMI_MSG_REQ2SPK_FLAG          (1 << 3)
#define C_MMI_MSG_REQ2RESPOND_FLAG      (1 << 4)
#define C_MMI_MSG_SEND_SPEECH_FLAG      (1 << 5)
#define C_MMI_MSG_STOP_SPEECH_FLAG      (1 << 6)
#define C_MMI_MSG_CANCEL_SPEECH_FLAG    (1 << 7)
#define C_MMI_MSG_RSP_START_FLAG        (1 << 8)
#define C_MMI_MSG_RSP_END_FLAG          (1 << 9)
#define C_MMI_MSG_UPDATE_INFO_FLAG      (1 << 10)
#define C_MMI_MSG_HEARTBEAT_FLAG        (1 << 11)

// C_MMI_REQ2RSP_TRANSCRIPT    文本转语音模式，大模型将传入的 text 文本字段转换成TTS语音下发
// C_MMI_REQ2RSP_PROMPT        询问模式，大模型将会根据传入的 text 文本进行回答，并将回答转换成TTS下发给设备
enum {
    C_MMI_REQ2RSP_TRANSCRIPT,
    C_MMI_REQ2RSP_PROMPT,
};

/********************** 内部接口（c_mmi.c 调用）**********************/

typedef struct _mmi_prompt_pram_t {
    char *key;
    char *value;
} c_mmi_prompt_pram_t;

/********************** 以下为旧版接口，下列接口将在 2025-12-31 弃用 **********************/

/********************** 旧版接口结束 **********************/

#ifdef __cplusplus
}
#endif

#endif
/**
 * @brief 创建全局互斥锁（多线程同步保护）
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_mutex_init(void);

/**
 * @brief 销毁全局互斥锁
 */
void c_mmi_mutex_deinit(void);

/**
 * @brief 查询是否处于 deinit 进行中
 * @return uint8_t 1表示 deinit 进行中，0表示否
 */
uint8_t c_mmi_get_shutting_down(void);

/**
 * @brief 设置 deinit 进行中标志
 * @param val 1置位，0清除
 */
void c_mmi_set_shutting_down(uint8_t val);

/**
 * @brief 获取需要通过websocket发送的数据
 * 
 * 本函数用于根据指定的类型获取数据，准备通过websocket进行发送
 * 它会根据传入的类型参数，将相应类型的数据填充到提供的数据缓冲区中
 * 
 * @param opcode 用于返回websocket数据类型，如:WEBSOCKET_OPCODE_TEXT/WEBSOCKET_OPCODE_BINARY
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
 * @param opcode websocket数据类型，如:WEBSOCKET_OPCODE_TEXT/WEBSOCKET_OPCODE_BINARY
 * @param data 指向接收到的数据的指针，数据的内容将根据type参数进行解析
 * @param size 数据的长度，以字节为单位，用于确定数据的范围
 * @return 返回实际解析的字节数
 */
uint32_t c_mmi_analyze_recv_data(uint8_t opcode, uint8_t *data, uint32_t size);

/**
 * @brief 返回是否已接收所有音频数据
 * 
 * 该函数供播放器判断是否还有音频数据需要播放。
 * 在本地播放器完成云端下行音频数据之后必须调用该函数，用于通知云端服务“端侧播放已完成”
 * 
 * @return uint8_t 返回接收状态或结果
 *         - 0: 还有音频数据待读取
 *         - 1: 数据已全被接收
 */
uint8_t c_mmi_audio_recv_all(void);

/**
 * @brief 设置 req2rsp 参数并发送请求
 * 
 * @param type 请求类型，取值为 C_MMI_REQ2RSP_TRANSCRIPT/PROMPT
 * @param text 文本内容字符串
 * @param parameters cJSON 格式的参数对象
 * @param cancel_speech 是否在请求中附带 cancel_speech=true（打断当前播报）
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_param_req2rsp(uint8_t type, char* text, cJSON* parameters, uint8_t cancel_speech);

/**
 * @brief 更新 images 参数
 * 
 * @param images cJSON 格式的图片数组对象
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_update_images(cJSON* images);

/**
 * @brief 设置业务参数
 * 
 * @param biz_params cJSON 格式的业务参数对象，SDK 内部会负责释放
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_biz_params(cJSON* biz_params);

/**
 * @brief 更新业务参数
 * 
 * @param biz_params cJSON 格式的业务参数对象，SDK 内部会负责释放，不要在外部重复释放
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_update_biz_params(cJSON* biz_params);

/**
 * @brief 设置 ASR 后处理替换词表（首次，在 start 前调用）
 * 
 * @param replace_words cJSON 数组，元素为 {source, target, matchMode} 对象，SDK 内部负责释放
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_replace_words(cJSON* replace_words);

/**
 * @brief 更新 ASR 后处理替换词表（会话中动态更新，触发 UpdateInfo 上行）
 * 
 * @param replace_words cJSON 数组，元素为 {source, target, matchMode} 对象，SDK 内部负责释放，不要在外部重复释放
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_update_replace_words(cJSON* replace_words);

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
 * 通过传入 query 文本和用户自定义参数来调用指定插件并获取响应结果。
 * 调用完成后需要自行释放 text 和 json_str 参数内存。
 *
 * @param text query 文本字符串指针，不可为 NULL
 * @param json_str 用户自定义参数，以 JSON 字符串格式传入，不可为 NULL
 *                 格式: {"<plugin_id>": {"<param1>": "value1", "<param2>": "value2"}}
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 *
 * @code
 * char *json_str = "{\"<plugin_id>\":{\"<param1>\":\"value1\",\"<param2>\":\"value2\"}}";
 * int32_t ret = c_mmi_tool_call("query text", json_str);
 * @endcode
 */
int32_t c_mmi_tool_call(char* text, char* json_str);

/**
 * @brief 更新心跳时间戳
 *
 * @param cur_time 当前时间戳，单位毫秒
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_heartbeat_update(int64_t cur_time);

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

/**
 * @brief 开始语音上行
 * 
 * 通知云端端侧开始进行语音数据上行。
 * 该接口可退出等待状态，也可打断当前正在进行的对话。
 * 处于拾音状态时，打断当前拾音并重新开始。
 * 支持 duplex 模式端侧主动发起语音。
 * 
 * @return int32_t 成功时返回0，否则返回非零的错误码
 */
int32_t c_mmi_speech_start(void);

/**
 * @brief 结束语音上行
 * 
 * 通知云端端侧语音上行已结束。
 * 该接口仅在 push2talk 模式下有效。
 * 
 * @return int32_t 成功时返回0，否则返回非零的错误码
 */
int32_t c_mmi_speech_end(void);

/**
 * @brief 暂停语音识别功能
 * 
 * 中断语音识别模块，进入等待状态。
 * 支持 duplex 模式端侧主动中断。
 * 
 * @return int32_t 成功时返回0，否则返回非零的错误码
 */
int32_t c_mmi_speech_pause(void);

/**
 * @brief 开始语音上行（旧版接口）
 * @deprecated 使用 c_mmi_speech_start 替代
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_start_speech(void);

/**
 * @brief 结束语音上行（旧版接口）
 * @deprecated 使用 c_mmi_speech_end 替代
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_stop_speech(void);

/**
 * @brief 暂停语音识别（旧版接口）
 * @deprecated 使用 c_mmi_speech_pause 替代
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_pause_task(void);

/**
 * @brief 停止当前对话任务
 *
 * 通知 SDK 结束当前任务，回到等待发起的初始状态。
 * 
 * @return int32_t 成功时返回0，否则返回非零的错误码
 */
int32_t c_mmi_stop(void);

/**
 * @brief 获取主状态
 *
 * @return uint8_t 主状态值，取值为 C_MMI_STATE_XXX
 */
uint8_t c_mmi_get_major_state(void);

/**
 * @brief 获取子状态
 *
 * @return uint8_t 子状态值，取值为 C_MMI_STATE_XXX_WAIT_SEND 等
 */
uint8_t c_mmi_get_state(void);

/**
 * @brief 查询是否已完成初始化
 *
 * @return uint8_t 1表示已初始化，0表示未初始化
 */
uint8_t c_mmi_is_inited(void);

/**
 * @brief 查询是否处于工作状态
 *
 * @return uint8_t 1表示工作中，0表示空闲
 */
uint8_t c_mmi_is_working(void);

/**
 * @brief 设置语音工作状态
 *
 * @param enable 1表示语音工作中，0表示停止
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_speech_set(uint8_t enable);

/**
 * @brief 查询语音是否工作中
 *
 * @return uint8_t 1表示工作中，0表示停止
 */
uint8_t c_mmi_speech_is_work(void);

/**
 * @brief 获取自动语音模式开关
 *
 * @return uint8_t 1表示开启，0表示关闭
 */
uint8_t c_mmi_get_auto_speech(void);

/**
 * @brief 设置自动语音模式开关
 *
 * @param enable 1表示开启，0表示关闭
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_auto_speech(uint8_t enable);

/**
 * @brief 获取提示词参数
 *
 * @return cJSON* 提示词参数对象，未配置时返回 NULL
 */
cJSON *c_mmi_get_prompt_param(void);

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
 * @brief 获取当前 task_id 的 UUID 格式字符串
 *
 * @return const char* 只读字符串指针
 */
const char *c_mmi_get_task_id_str(void);

/**
 * @brief 设置请求等待标志
 */
void c_mmi_set_req_wait_flag(void);

/**
 * @brief 获取 websocket 连接 header
 *
 * @return char* header 字符串，未初始化时返回 NULL
 */
char *c_mmi_get_wss_header(void);

/**
 * @brief 初始化 MMI 核心资源
 *
 * 内部完成 ringbuffer 创建、header 组装与初始状态设置
 *
 * @param workspace 工作空间 ID
 * @param app_id 应用 ID
 * @param api_key 鉴权密钥
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_msg_init(char *workspace, char *app_id, char *api_key);

/**
 * @brief 释放 MMI 内部资源
 *
 * 释放 ringbuffer、header 等初始化时申请的资源
 */
void c_mmi_msg_deinit(void);

/**
 * @brief 复位 MMI 内部状态
 *
 * 将内部状态结构体整体清零
 */
void c_mmi_reset(void);

/**
 * @brief 清空录音与播放 ringbuffer 数据
 *
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_clean_audio_data(void);

/**
 * @brief 写入录音数据到上行 ringbuffer
 *
 * @param data 音频数据指针
 * @param size 数据长度，以字节为单位
 * @return uint32_t 实际写入的字节数
 */
uint32_t c_mmi_put_recorder_data(uint8_t *data, uint32_t size);

/**
 * @brief 从下行 ringbuffer 读取播放数据
 *
 * @param data 输出缓冲指针
 * @param size 期望读取的字节数
 * @return uint32_t 实际读取的字节数
 */
uint32_t c_mmi_get_player_data(uint8_t *data, uint32_t size);

/**
 * @brief 设置设备 IP
 *
 * @param ip IP 字符串
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_ip(char *ip);

/**
 * @brief 设置设备位置信息
 *
 * @param longitude 经度
 * @param latitude 纬度
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_postion(double longitude, double latitude);

/**
 * @brief 设置设备所在城市
 *
 * @param city 城市名称字符串
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_city(char *city);

/**
 * @brief 设置 agent 接管模式
 *
 * @param onoff 1表示开启接管，0表示关闭
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_agent_takeover(uint8_t onoff);

