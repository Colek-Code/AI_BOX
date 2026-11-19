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

#ifndef __C_MMI_CONFIG_H__
#define __C_MMI_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_mmi.h"

#define C_MMI_VOICE_ID_LEN       (64)                           // max 64

#define C_MMI_CONFIG_DEFAULT()                  \
{                                               \
    .evt_cb = NULL,                             \
    .work_mode = C_MMI_MODE_PUSH2TALK,          \
    .text_mode = C_MMI_TEXT_MODE_BOTH,          \
    .incremental_response = 0,                  \
    .response_text = 0,                         \
    .voice_id = "longxiaochun_v2",              \
    .story_voice_id = "longxiaochun_v2",        \
    .upstream_mode = C_MMI_STREAM_MODE_PCM,     \
    .downstream_mode = C_MMI_STREAM_MODE_PCM,   \
    .recorder_rb_size = 8 * 1024,               \
    .player_rb_size = 8 * 1024,                 \
    .transmit_rate_limit = 0,                   \
    .enable_cbr = 0,                            \
    .frame_size = 60,                           \
    .bit_rate = 32,                             \
    .us_sample_rate = 16000,                    \
    .ds_sample_rate = 24000,                    \
    .vocabulary_id = NULL,                      \
    .volume = 50,                               \
    .speech_rate = 100,                         \
    .pitch_rate = 100,                          \
    .instruction = NULL,                        \
    .us_language = NULL,                        \
    .ds_language = NULL,                        \
    .word_timestamp = 0,                        \
    .user_id = NULL,                            \
    .disable_memory = 0,                        \
    .search_result = 0,                         \
}

enum {
    C_MMI_MODE_NONE,
    C_MMI_MODE_PUSH2TALK,
    C_MMI_MODE_TAP2TALK,
    C_MMI_MODE_DUPLEX,
    C_MMI_MODE_MAX
};

enum {
    C_MMI_TEXT_MODE_NONE,
    C_MMI_TEXT_MODE_ASR_ONLY,
    C_MMI_TEXT_MODE_LLM_ONLY,
    C_MMI_TEXT_MODE_BOTH
};

enum {
    C_MMI_STREAM_MODE_NONE,
    C_MMI_STREAM_MODE_PCM,
    C_MMI_STREAM_MODE_OPUS_OGG,
    C_MMI_STREAM_MODE_OPUS_RAW,
    C_MMI_STREAM_MODE_MP3,
    C_MMI_STREAM_MODE_MAX,
};

enum {
    C_MMI_OPUS_MODE_OGG,
    C_MMI_OPUS_MODE_RAW,
    C_MMI_OPUS_MODE_MAX,
};

enum {
    C_MMI_STREAM_TYPE_AUDIO_ONLY = 0,
    C_MMI_STREAM_TYPE_AUDIO_AND_VIDEO,
    C_MMI_STREAM_TYPE_MAX,
};

enum {
    C_MMI_PAY_MODE_LICENSE,
    C_MMI_PAY_MODE_PAYG,
};

/**
 * @brief mmi事件回调函数类型
 *
 * 当mmi模块发生状态变化或事件时触发的回调函数
 * 
 * @param event 事件类型，取值为C_MMI_EVENT_xxx系列宏定义
 * @param param 事件参数，根据事件类型不同指向不同数据结构
 * @return int32_t 返回0表示处理成功，非0表示处理失败
 * @attention 回调由 SDK 内部线程同步调用，必须快速返回，
 *             禁止阻塞操作（文件 IO、长时间等待）；阻塞会导致下行音频卡顿、
 *             心跳超时断线。需延迟处理的场景应在回调内转发到应用自建 task
 */
typedef int32_t(*c_mmi_event_callback)(uint32_t event, void *param);

typedef struct _mmi_user_config_t { 
    // 用户设置
    c_mmi_event_callback evt_cb;        // 同步回调，禁止阻塞，契约详见 c_mmi_event_callback

    uint32_t recorder_rb_size;          // 录音环形缓冲区大小，单位字节，默认 8*1024
    uint32_t player_rb_size;            // 播放环形缓冲区大小，单位字节，默认 8*1024

    uint8_t work_mode;                  // 工作模式，取值 C_MMI_MODE_PUSH2TALK/TAP2TALK/DUPLEX，默认 PUSH2TALK
    uint8_t text_mode;                  // 文本模式，控制返回ASR识别文本与LLM回复文本，取值 C_MMI_TEXT_MODE_xxx，默认 BOTH
    uint8_t incremental_response;       // 是否增量返回大模型结果，0为全量下发，1为增量，默认0
    uint8_t response_text;              // response_text配置为1时返回text字段，否则返回spoken字段
    char voice_id[C_MMI_VOICE_ID_LEN];    // TTS合成音色ID，支持克隆音色，默认"longxiaochun_v2"
    char story_voice_id[C_MMI_VOICE_ID_LEN]; // 故事播放音色ID，默认"longxiaochun_v2"

    uint8_t upstream_mode;              // 支持 C_MMI_STREAM_MODE_PCM/C_MMI_STREAM_MODE_OPUS_OGG/C_MMI_STREAM_MODE_OPUS_RAW
    uint8_t downstream_mode;            // 支持 C_MMI_STREAM_MODE_PCM/C_MMI_STREAM_MODE_OPUS_OGG/C_MMI_STREAM_MODE_OPUS_RAW/C_MMI_STREAM_MODE_MP3
    uint32_t us_sample_rate;            // 上行音频采样率，默认16000，支持范围8000/16000/240000/48000
    uint32_t ds_sample_rate;            // 下行音频采样率，默认24000，支持范围8000/16000/240000/48000，通义千问-TTS、通义千问3-TTS模型仅支持24000

    uint32_t transmit_rate_limit;       // 下行音频发送速率限制，单位：字节每秒
    uint8_t enable_cbr;                 // 合成音频是否固定比特率，默认false，只在合成音频格式为opus或raw-opus时生效。
    uint8_t frame_size;                 // 合成音频的帧大小，取值范围：10,20,40,60,100,120，默认值为60，单位ms，只在合成音频格式为opus或raw-opus时生效
    uint16_t bit_rate;                  // 合成音频的比特率，取值范围：6~510kbps，默认值32，单位kbps，只在合成音频格式为opus或raw-opus时生效

    uint8_t volume;                     // 合成音频的音量，取值范围0-100，默认50
    uint8_t speech_rate;                // 合成音频的语速，取值范围50-200，表示默认语速的50%-200%，默认100
    uint8_t pitch_rate;                 // 合成音频的声调，取值范围50-200，默认100

    char *user_id;                      // 用户自定义user_id，为NULL时，默认使用device_name，传入时需要使用静态变量或申请对应内存空间，防止指向数据被错误释放
    char *vocabulary_id;                // 热词id，传入时需要使用静态变量或申请对应内存空间，防止指向数据被错误释放
    char *instruction;                  // TTS 合成指令（方言/情感/角色控制），非必填，传入时需使用静态变量或申请对应内存空间，防止指向数据被错误释放
    char *us_language;                  // 上行语音识别语种，NULL时使用云端默认值，传入时需使用静态变量或申请对应内存空间，防止指向数据被错误释放
    char *ds_language;                  // 下行语音合成语种，NULL时使用云端默认值，传入时需使用静态变量或申请对应内存空间，防止指向数据被错误释放
    uint8_t word_timestamp;             // 使能云端下发TTS文本时间戳（RespondingContent.extra_info.word_timestamps），默认0关闭
    uint8_t disable_memory;             // 禁用长期记忆（云端功能）
    uint8_t search_result;              // 返回所有搜索结果（云端功能）
} mmi_user_config_t;

/**
 * @brief 获取用户配置区指针
 *
 * @return mmi_user_config_t* 指向用户配置结构体的指针
 */
mmi_user_config_t *c_mmi_get_user_config(void);

/**
 * @brief 获取用户配置的事件回调函数指针
 *
 * @return c_mmi_event_callback 回调函数指针，未配置时返回 NULL
 */
c_mmi_event_callback c_mmi_get_evt_cb(void);

/**
 * @brief 配置MMI模块参数
 *
 * 此函数用于初始化MMI模块的各项配置参数，包括事件回调、工作模式、文本模式、
 * 音色设置、音频流模式以及缓冲区大小等
 * 
 * @param config 指向mmi_user_config_t结构体的指针，包含配置参数
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_config(mmi_user_config_t *config);

/**
 * @brief 获取 SDK 当前的工作模式
 * 
 * @return 工作模式标识符
 *         - C_MMI_MODE_PUSH2TALK
 *         - C_MMI_MODE_TAP2TALK
 *         - C_MMI_MODE_DUPLEX
 */
uint8_t c_mmi_get_work_mode(void);

/**
 * @brief 获取工作模式字符串
 * 
 * @return const char* 工作模式名称，如 "push2talk"、"tap2talk"、"duplex"
 */
char *c_mmi_get_work_mode_str(void);

/**
 * @brief 获取文本模式字符串（云端协议字段）
 * 
 * @return const char* 协议字段值，如 ""、"transcript"、"dialog"、"transcript,dialog"
 */
char *c_mmi_get_text_mode_str(void);

/**
 * @brief 获取文本模式字符串（本地 JSON 配置文件）
 * 
 * @return const char* 本地配置值，如 ""、"asr"、"llm"、"both"
 * @note 返回值与 c_mmi_get_text_mode_str 的语义相同但取值不同：
 *       前者用于云端协议字段，本函数用于本地配置文件持久化。
 */
char *c_mmi_get_json_text_mode_str(void);

/**
 * @brief 获取上行音频流模式
 * 
 * @return uint8_t 上行模式标识符，取值为 C_MMI_STREAM_MODE_xxx
 */
uint8_t c_mmi_get_upstream_mode(void);

/**
 * @brief 获取上行音频流模式字符串
 * 
 * @return const char* 上行模式名称，如 "pcm"、"opus"、"raw-opus"
 */
char *c_mmi_get_upstream_mode_str(void);

/**
 * @brief 获取下行音频流模式
 * 
 * @return uint8_t 下行模式标识符，取值为 C_MMI_STREAM_MODE_xxx
 */
uint8_t c_mmi_get_downstream_mode(void);

/**
 * @brief 获取下行音频流模式字符串
 * 
 * @return const char* 下行模式名称，如 "pcm"、"opus"、"raw-opus"、"mp3"
 */
char *c_mmi_get_downstream_mode_str(void);

/**
 * @brief 设置工作模式
 * 
 * @param work_mode 工作模式，取值为 C_MMI_MODE_PUSH2TALK/TAP2TALK/DUPLEX
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 * @deprecated 推荐使用 c_mmi_config 统一配置
 */
int32_t c_mmi_set_work_mode(uint8_t work_mode);

/**
 * @brief 设置文本模式
 * 
 * @param text_mode 文本模式，取值为 C_MMI_TEXT_MODE_ASR_ONLY/LLM_ONLY/BOTH
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 * @deprecated 推荐使用 c_mmi_config 统一配置
 */
int32_t c_mmi_set_text_mode(uint8_t text_mode);

/**
 * @brief 设置设备名称
 * 
 * @param device_name 设备名称字符串，长度不超过 C_LICENSE_DN_STR_MAX
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_device_name(char *device_name);

/**
 * @brief 设置 voice_id，可设置克隆音色
 *
 * @param voice_id 音色 ID 字符串
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_voice_id(char *voice_id);

/**
 * @brief 设置故事音色 ID
 * 
 * @param story_voice_id 故事音色 ID 字符串
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_story_voice_id(char *story_voice_id);

/**
 * @brief 设置 TTS 合成指令（方言/情感/角色控制）
 *
 * 控制语音合成的方言、情感或角色效果。
 * 适用模型：cosyvoice-v3-flash, cosyvoice-v3-plus, cosyvoice-v3.5-flash,
 *           cosyvoice-v3.5-plus, qwen3-tts-instruct-flash-realtime
 *
 * @param instruction 合成指令字符串，传 NULL 则清除。
 *                    传入时需使用静态变量或申请对应内存空间，防止指向数据被错误释放。
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_instruction(char *instruction);

/**
 * @brief 设置上行语音识别语种
 *
 * 设置 us_language 字段，传 NULL 或 "" 标识使用云端默认语种。
 * 传入时需使用静态变量或申请对应内存空间，防止指向数据被错误释放。
 *
 * @param us_language 语种字符串（如 "zh"、"en"）
 * 详细设置参考文档 language_hints 章节，仅支持设置一个语言
 * https://help.aliyun.com/zh/model-studio/fun-asr-client-events
 * https://help.aliyun.com/zh/model-studio/qwen-asr-realtime-client-events
 * https://help.aliyun.com/zh/model-studio/paraformer-client-events
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_us_language(char *us_language);

/**
 * @brief 设置下行语音合成语种
 *
 * 设置 ds_language 字段，传 NULL 或 "" 标识使用云端默认语种。
 * 传入时需使用静态变量或申请对应内存空间，防止指向数据被错误释放。
 *
 * @param ds_language 语种字符串（如 "zh"、"en"）
 * 详细设置参考文档 language_hints 章节，仅支持设置一个语言
 * https://help.aliyun.com/zh/model-studio/cosyvoice-client-events
 * https://help.aliyun.com/zh/model-studio/qwen-tts-realtime-client-events
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_ds_language(char *ds_language);

/**
 * @brief 使能 TTS 文本时间戳功能。
 *
 * 默认发起对话时云端不下发 TTS 文本时间戳，使能后云端在下发 TTS 回复文本时
 * 携带文本中每个字对应的时间戳（word_timestamps），应用层通过
 * C_MMI_EVENT_WORD_TIMESTAMP 事件获取。仅在 text_mode 配置为返回 LLM
 * 回复文本（C_MMI_TEXT_MODE_LLM_ONLY 或 C_MMI_TEXT_MODE_BOTH）时生效，
 * 且仅支持时间戳的音色会返回。
 *
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_enable_word_timestamp(void);

/**
 * @brief 禁用长期记忆（云端功能）
 *
 * 设置 disable_memory = 1，禁用云端长期记忆。
 *
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_disable_memory(void);

/**
 * @brief 启用云端返回所有搜索结果
 *
 * 设置 search_result = 1，使能云端返回所有搜索结果。
 *
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_enable_search_result(void);

/**
 * @brief 设置上行音频流模式
 * 
 * @param stream_mode 流模式，取值为 C_MMI_STREAM_MODE_PCM/OPUS_OGG/OPUS_RAW
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 * @deprecated 推荐使用 c_mmi_config 统一配置
 */
int32_t c_mmi_set_upstream_mode(uint8_t stream_mode);

/**
 * @brief 设置下行音频流模式
 * 
 * @param stream_mode 流模式，取值为 C_MMI_STREAM_MODE_PCM/OPUS_OGG/OPUS_RAW/MP3
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 * @deprecated 推荐使用 c_mmi_config 统一配置
 */
int32_t c_mmi_set_downstream_mode(uint8_t stream_mode);

/**
 * @brief 重置对话上下文
 * 
 * 该函数用于重置多模态 SDK 对话上下文，在需要开始新一轮对话时调用。
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_reset_dialog_id(void);

/**
 * @brief 检查配置是否有效
 * 
 * @return uint8_t 1表示配置有效，0表示配置无效
 */
uint8_t c_mmi_config_check(void);

/**
 * @brief 打印当前配置信息到日志
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_config_print(void);

/**
 * @brief 获取上行类型字符串
 * 
 * @return char* 指向上行类型名称的指针
 */
char *c_mmi_get_upstream_type_str(void);

/**
 * @brief 设置上行类型
 * 
 * @param stream_type 上行类型标识符
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_upstream_type(uint8_t stream_type);

/**
 * @brief 解析 JSON 字符串并更新 MMI 配置字段
 * 
 * @param json_str JSON 格式的配置字符串
 * @return uint8_t 1表示有字段被更新，0表示无更新
 */
uint8_t c_mmi_config_update(char *json_str);

/**
 * @brief 获取当前配置的 JSON 格式字符串
 * 
 * @return char* 指向 JSON 配置字符串的指针
 */
char *c_mmi_config_get_json(void);

/**
 * @brief 从 cJSON 对象解析并更新 prompt 参数
 * 
 * @param root 指向 cJSON 对象的指针，包含 prompt 参数
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_config_analyze_prompt_params(cJSON *root);

#ifdef __cplusplus
}
#endif

#endif
