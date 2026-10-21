#ifndef AUDIO_INPUT_H_INCLUDED
#define AUDIO_INPUT_H_INCLUDED

#include "system/includes.h"
#include "server/audio_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D++ 编码类型选择优先级：
 *   1. d_app_config.h 已根据 D_QWEN_US_AUDIO_FORMAT 定义 AUDIO_TYPE_OPUS（Qwen 上行 raw-opus）
 *   2. D_MODULE_RTC_AUDIO_ENABLE + D_RTC_AUDIO_CODEC_OPUS（RTC 环回测试）
 *   3. 以上均未定义时回退 PCM 直通
 */
#if defined(AUDIO_TYPE_OPUS)
    /* 已由外部（d_app_config.h 或 Makefile）定义，跳过 */
#elif defined(D_MODULE_RTC_AUDIO_ENABLE) && defined(D_RTC_AUDIO_CODEC_OPUS)
#define AUDIO_TYPE_OPUS
#else
#define AUDIO_TYPE_G711A                     /* PCM 直通 */
#endif

#if defined(D_MODULE_QWEN_AGENT_ENABLE) && \
    defined(D_QWEN_DS_AUDIO_FORMAT) && \
    defined(D_QWEN_AUDIO_FORMAT_RAW_OPUS) && \
    (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
#define AUDIO_PLAYER_TYPE_OPUS
#define AUDIO_PLAYER_OPUS_PKT_LEN D_QWEN_DS_OPUS_PKT_LEN
#define AUDIO_PLAYER_OPUS_FORCE_SR D_QWEN_DS_SAMPLE_RATE
#elif defined(D_MODULE_RTC_AUDIO_ENABLE) && defined(D_RTC_AUDIO_CODEC_OPUS)
#define AUDIO_PLAYER_TYPE_OPUS
#define AUDIO_PLAYER_OPUS_PKT_LEN 240U
#define AUDIO_PLAYER_OPUS_FORCE_SR 16000U
#else
#define AUDIO_PLAYER_TYPE_PCM
#endif

typedef unsigned char UCHAR_T;
typedef char CHAR_T;
typedef signed int INT_T;
typedef int BOOL_T;

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef INOUT
#define INOUT
#endif

#ifndef VOID
#define VOID void
#endif

#ifndef VOID_T
#define VOID_T void
#endif


#ifndef CONST
#define CONST const
#endif

#define AUDIO_DEBUG_ENABLE

#ifdef AUDIO_DEBUG_ENABLE
#define audio_debug(format, ...)      printf(format, ## __VA_ARGS__)
#else
#define audio_debug(...)
#endif


typedef struct {
    INT_T channel_num;
    INT_T bit_dept;
    INT_T sample_rate;
    INT_T audio_power_off;
} _AUDIO_PARAM;


int _device_write_voice_data(void *data, unsigned int len);
int _device_get_voice_data(void *data, unsigned int max_len);
void audio_stream_init(int sample_rate, int bit_dept, int channel_num);
void start_audio_stream(void);
void stop_audio_stream(void);
void _device_net_audio_play(bool flag);       /* D++ 独立控制播放（解码器） */
void _device_net_audio_recorder(bool flag);   /* D++ 独立控制录音（编码器） */
void _device_wbuf_clear(void);               /* D++ 清空编码器 cbuffer */
void _device_rbuf_clear(void);               /* D++ 清空解码器 cbuffer */
int audio_stream_set_play_volume(int volume);
/* Persistent D_Code Qwen volume/open synchronization. */
#if defined(D_FEATURE_QWEN) && D_FEATURE_QWEN
int audio_stream_volume_init(void);
#endif
int audio_stream_get_play_volume(void);
bool is_audio_play_open(void);
bool is_audio_play_stop_pending(void);
bool is_audio_record_open(void);
int get_recoder_state();

#ifdef __cplusplus
}
#endif

#endif // AUDIO_INPUT_H_INCLUDED
