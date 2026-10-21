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
    .us_sample_rate = 24000,                    \
    .ds_sample_rate = 24000,                    \
    .vocabulary_id = NULL,                      \
    .volume = 50,                               \
    .speech_rate = 100,                         \
    .pitch_rate = 100,                          \
    .user_id = NULL,                            \
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
 */
typedef int32_t(*c_mmi_event_callback)(uint32_t event, void *param);

typedef struct _mmi_user_config_t { 
    // 用户设置
    c_mmi_event_callback evt_cb;

    uint8_t work_mode;
    uint8_t text_mode;
    char voice_id[C_MMI_VOICE_ID_LEN];
    char story_voice_id[C_MMI_VOICE_ID_LEN];

    uint8_t upstream_mode;              // 支持 C_MMI_STREAM_MODE_PCM/C_MMI_STREAM_MODE_OPUS_OGG/C_MMI_STREAM_MODE_OPUS_RAW
    uint8_t downstream_mode;            // 支持 C_MMI_STREAM_MODE_PCM/C_MMI_STREAM_MODE_OPUS_OGG/C_MMI_STREAM_MODE_OPUS_RAW/C_MMI_STREAM_MODE_MP3

    uint32_t recorder_rb_size;
    uint32_t player_rb_size;

    uint16_t transmit_rate_limit;       // 下行音频发送速率限制，单位：字节每秒
    uint8_t enable_cbr;                 // 合成音频是否固定比特率，默认false，只在合成音频格式为opus或raw-opus时生效。
    uint8_t frame_size;                 // 合成音频的帧大小，取值范围：10,20,40,60,100,120，默认值为60，单位ms，只在合成音频格式为opus或raw-opus时生效
    uint16_t bit_rate;                  // 合成音频的比特率，取值范围：6~510kbps，默认值32，单位kbps，只在合成音频格式为opus或raw-opus时生效

    uint32_t us_sample_rate;            // 上行音频采样率，默认16000，支持范围8000/16000/240000/48000
    uint32_t ds_sample_rate;            // 下行音频采样率，默认24000，支持范围8000/16000/240000/48000，通义千问-TTS、通义千问3-TTS模型仅支持24000

    char *vocabulary_id;                // 热词id，传入时需要使用静态变量或申请对应内存空间，防止指向数据被错误释放
    uint8_t volume;                     // 合成音频的音量，取值范围0-100，默认50
    uint8_t speech_rate;                // 合成音频的语速，取值范围50-200，表示默认语速的50%-200%，默认100
    uint8_t pitch_rate;                 // 合成音频的声调，取值范围50-200，默认100

    char *user_id;                      // 用户自定义user_id，为NULL时，默认使用device_name，传入时需要使用静态变量或申请对应内存空间，防止指向数据被错误释放
} mmi_user_config_t;

typedef struct _mmi_config_t { 
    uint8_t config_valid;
    uint8_t upstream_type;

    // 由存储获得的参数
    char device_name[C_MMI_DN_STR_MAX + 1];
    char dialog_id[C_MMI_DIALOG_ID_LEN + 1];

    mmi_user_config_t user_config;
} mmi_config_t;

mmi_config_t *c_mmi_get_config(void);

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
 * @brief 获取SDK当前的工作模式
 *
 * 该函数用于获取SDK当前的工作模式状态
 *
 * @param[in]  void  无输入参数
 *
 * @return 工作模式标识符
 *         - C_MMI_MODE_PUSH2TALK
 *         - C_MMI_MODE_TAP2TALK
 *         - C_MMI_MODE_DUPLEX
 */
uint8_t c_mmi_get_work_mode(void);

char *c_mmi_get_work_mode_str(void);
char *c_mmi_get_text_mode_str(void);
char *c_mmi_get_upstream_mode_str(void);
uint8_t c_mmi_get_downstream_mode(void);
char *c_mmi_get_downstream_mode_str(void);

int32_t c_mmi_set_work_mode(uint8_t work_mode);                     // 推荐使用 c_mmi_config 替代
int32_t c_mmi_set_text_mode(uint8_t text_mode);                     // 推荐使用 c_mmi_config 替代

int32_t c_mmi_set_device_name(char *device_name);

/**
 * @brief 设置voice_id，可设置克隆音色
 *
 * 此函数用于设置模型回复语音音色
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_voice_id(char *voice_id);

/**
 * @brief 设置故事音色ID
 * 
 * 该函数用于设置故事agent的音色ID
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_set_story_voice_id(char *story_voice_id);

int32_t c_mmi_set_upstream_mode(uint8_t stream_mode);               // 推荐使用 c_mmi_config 替代
int32_t c_mmi_set_downstream_mode(uint8_t stream_mode);             // 推荐使用 c_mmi_config 替代
int32_t c_mmi_set_audio_mode_ringbuffer(uint32_t recorder_rb_size, uint32_t player_rb_size);    // 推荐使用 c_mmi_config 替代

/**
 * @brief 重置对话上下文
 * 
 * 该函数用于重置多模态SDK对话上下文，在需要开始新一轮对话时调用。
 * 
 * @param 无参数
 * 
 * @return int32_t 返回操作结果，0表示成功，非0表示失败
 */
int32_t c_mmi_reset_dialog_id(void);

int32_t c_mmi_register_event_callback(c_mmi_event_callback cb);     // 推荐使用 c_mmi_config 替代

uint8_t c_mmi_config_check(void);
int32_t c_mmi_config_print(void);

char *c_mmi_get_upstream_type_str(void);
int32_t c_mmi_set_upstream_type(uint8_t stream_type);

int32_t c_mmi_config_update(char *json_str);

#ifdef __cplusplus
}
#endif

#endif
