#ifndef __APP_SDK_LOCALDATA_AUDIO_PLAY_H__
#define __APP_SDK_LOCALDATA_AUDIO_PLAY_H__
#include "generic/printf.h"
int app_sdk_localdata_audio_play(const u8 *data,u32 len,u8 volume,bool playpcm);
void app_sdk_localdata_audio_pp(void);
void app_sdk_localdata_audio_stop(void);
int app_sdk_localdata_audio_dec_status(void);
void app_sdk_localdata_audio_set_vol(u8 volume);
//#if defined BRTC_SDK_ENABLE
typedef enum
{
    AUDIO_PLAY_START,
    AUDIO_PLAY_STOP,
    AUDIO_PLAY_PAUSE
} play_state_t;
typedef int (*app_sdk_play_feed_cb)(char *buf, int size);
typedef void (*app_sdk_play_end_cb)(void);
int app_sdk_audio_play_start(app_sdk_play_feed_cb cb_feed, app_sdk_play_end_cb cb_end);
int app_sdk_audio_play_prewarm(void);
void app_sdk_audio_play_set_ready_notify(void (*cb)(void));
int app_sdk_audio_play_stop(void);
int app_sdk_audio_play_volume(int level);
int app_sdk_audio_play_volume_step(int step);
int app_sdk_audio_play_volume_lite();

int app_adk_play_pause(int default_ms);
//#endif
#endif // __APP_SDK_LOCALDATA_AUDIO_PLAY_H__
