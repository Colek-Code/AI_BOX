#ifndef __APP_SDK_AUDIORECORDER_H__
#define __APP_SDK_AUDIORECORDER_H__
typedef enum
{
    AUDIO_RECRD_START,
    AUDIO_RECRD_STOP,
} rec_state_t;
typedef int (*app_sdk_record_feed_cb)(char *buf, int size);
typedef void (*app_sdk_record_end_cb)(void);
int app_sdk_audio_record_start(int max_time_ms,int used_vad,app_sdk_record_feed_cb cb_feed,app_sdk_record_end_cb cb_end,char *format);
int app_sdk_audio_record_stop(void);
void mute_record(bool record);
#endif
