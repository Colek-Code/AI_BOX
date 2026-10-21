#include "system/includes.h"
#include "server/audio_server.h"
#include "syscfg_id.h"
#include <stdlib.h>
#include "app_sdk_localdata_audio_play.h"
#include "app_config.h"

#ifdef D_MODULE_BRTC_AGENT_ENABLE

/* BRTC demo 移植 */
#ifndef ceofVolume
#define ceofVolume          0.8f        //D++ 音量系数
#endif
#ifndef MIN_VOLUME_VALUE
#define MIN_VOLUME_VALUE    10
#endif
#ifndef MAX_VOLUME_VALUE
#define MAX_VOLUME_VALUE    100
#endif
#ifndef INIT_VOLUME_VALUE
#define INIT_VOLUME_VALUE   100
#endif


#ifndef D_BRTC_ENABLE_G722
#define __ULAW_SIGN_BIT      (0x80)
#define __ULAW_QUANT_MASK    (0xf)
#define __ULAW_SEG_MASK      (0x70)
#define __ULAW_SEG_SHIFT     (4)
#define __ULAW_BIAS          (0x84)
static short __ulaw2linear(unsigned char u_val)
{
    short t;
    u_val = ~u_val;
    t = ((u_val & __ULAW_QUANT_MASK) << 3) + __ULAW_BIAS;
    t <<= ((unsigned)u_val & __ULAW_SEG_MASK) >> __ULAW_SEG_SHIFT;
    return ((u_val & __ULAW_SIGN_BIT) ? (__ULAW_BIAS - t) : (t - __ULAW_BIAS));
}
#endif

bool rtc_player_tts = true;

//#if defined BRTC_SDK_ENABLE
static app_sdk_play_feed_cb g_play_feed_cb = NULL;
static app_sdk_play_end_cb g_play_end_cb = NULL;
static struct server *s_dec_server = NULL;
static volatile bool g_has_play = false;
static play_state_t s_play_state = AUDIO_PLAY_STOP;
static OS_MUTEX s_play_mutex;
static int __g_rtc_player_volume = 50;
static void (*s_play_ready_notify)(void) = NULL; /* 预热完成后订阅端就绪通知 */
bool g_player_pause = false;
bool g_palyer_pause_interval = 20;

static int __play_mutex_init()
{
    return os_mutex_create(&s_play_mutex);
}
late_initcall(__play_mutex_init);


static int app_sdk_audio_play_volume_step(int step)
{
    union audio_req req = {0};

    int volume = __g_rtc_player_volume + step;
    if (volume < MIN_VOLUME_VALUE) {
        volume = MIN_VOLUME_VALUE;
    } else if (volume > MAX_VOLUME_VALUE) {
        volume = MAX_VOLUME_VALUE;
    }
    if (volume == __g_rtc_player_volume) {
        return -EINVAL;
    }
    __g_rtc_player_volume = volume;

#ifdef CONFIG_UI_ENABLE
    os_taskq_post(POST_TASK_NAME, 2, UI_MSG_SET_VOLUME, volume);
#endif

    printf("set_dec_volume agent step: %d\n", volume);
/*
#ifdef CONFIG_DEC_DIGITAL_VOLUME_ENABLE
#if TCFG_USER_EMITTER_ENABLE
    if (get_bt_emitter_audio_server() && __this->digital_vol_hdl && __this->dec_server) {
        return user_audio_digital_volume_set(__this->digital_vol_hdl, volume, 1);
    }
#endif
#if TCFG_USER_VIRTUAL_PLAY_ENABLE
    if (check_user_virtual_audio_if_running() && __this->digital_vol_hdl && __this->dec_server) {
        return user_audio_digital_volume_set(__this->digital_vol_hdl, volume, 1);
    }
#endif
#endif

#ifdef CONFIG_VOICE_PROMPT_FILE_PATH
    if (volume == MAX_VOLUME_VALUE) {
        play_file(CONFIG_VOICE_PROMPT_FILE_PATH"VolumeFull.mp3");
    } else {
        play_file(CONFIG_VOICE_PROMPT_FILE_PATH"Volume.mp3");
    }
#endif
*/
    if (!s_dec_server) {
        return -EFAULT;
    }

    req.dec.cmd     = AUDIO_DEC_SET_VOLUME;
    req.dec.volume  = volume * ceofVolume;
    os_mutex_pend(&s_play_mutex, 0);
    if( 0 == server_request(s_dec_server, AUDIO_REQ_DEC, &req)) {
        os_mutex_post(&s_play_mutex);

        syscfg_write(CFG_MUSIC_VOL, &volume, sizeof(volume));
        printf("step server_request  volume:%d \r\n", volume);
        return -EFAULT;
    }
    os_mutex_post(&s_play_mutex);

    syscfg_write(CFG_MUSIC_VOL, &volume, sizeof(volume));
    printf("step server_request  volume:%d \r\n", volume);

    return 1;
}
static int app_sdk_audio_play_volume_lite() {

    union audio_req req = {0};

    if (!s_dec_server) {
        return -EFAULT;
    }
    req.dec.cmd     = AUDIO_DEC_SET_VOLUME;
    req.dec.volume  = __g_rtc_player_volume * ceofVolume;
    int ret  = 0 ;

    os_mutex_pend(&s_play_mutex, 0);
    if (ret = server_request(s_dec_server, AUDIO_REQ_DEC, &req) != 0) {
        os_mutex_post(&s_play_mutex);
        printf("server_request failed  :%d\r\n", ret);

        return - EFAULT;
    }
    os_mutex_post(&s_play_mutex);
    printf("syscfg_write finished:%d\n", __g_rtc_player_volume);

    return 1;
}

static int app_sdk_audio_play_volume(int volume) {

    union audio_req req = {0};

    if (volume < MIN_VOLUME_VALUE) {
        volume = MIN_VOLUME_VALUE;
    } else if (volume > MAX_VOLUME_VALUE) {
#ifdef CONFIG_VOICE_PROMPT_FILE_PATH
        //play_file(CONFIG_VOICE_PROMPT_FILE_PATH"VolumeFull.mp3");
#endif
        volume = MAX_VOLUME_VALUE;
    }
    if (volume == __g_rtc_player_volume) {
        return -EINVAL;
    }
    __g_rtc_player_volume = volume;

#ifdef CONFIG_UI_ENABLE
    os_taskq_post(POST_TASK_NAME, 2, UI_MSG_SET_VOLUME, __g_rtc_player_volume);
#endif

#ifdef CONFIG_DEC_DIGITAL_VOLUME_ENABLE
#if TCFG_USER_EMITTER_ENABLE
    if (get_bt_emitter_audio_server() && __this->digital_vol_hdl && __this->dec_server) {
        return user_audio_digital_volume_set(__this->digital_vol_hdl, __g_rtc_player_volume, 1);
    }
#endif
#if TCFG_USER_VIRTUAL_PLAY_ENABLE
    if (check_user_virtual_audio_if_running() && __this->digital_vol_hdl && __this->dec_server) {
        return user_audio_digital_volume_set(__this->digital_vol_hdl, __g_rtc_player_volume, 1);
    }
#endif
#endif

    if (!s_dec_server) {
        return -EFAULT;
    }
    printf("set_dec_volume: %d\n", __g_rtc_player_volume);
    req.dec.cmd     = AUDIO_DEC_SET_VOLUME;
    req.dec.volume  = __g_rtc_player_volume * ceofVolume;
    int ret  = 0 ;

    os_mutex_pend(&s_play_mutex, 0);
    if (ret = server_request(s_dec_server, AUDIO_REQ_DEC, &req) != 0) {
        os_mutex_post(&s_play_mutex);
        syscfg_write(CFG_MUSIC_VOL, &__g_rtc_player_volume, sizeof(__g_rtc_player_volume));

        printf("server_request failed  :%d\r\n", ret);

        return - EFAULT;
    }
    os_mutex_post(&s_play_mutex);
    syscfg_write(CFG_MUSIC_VOL, &__g_rtc_player_volume, sizeof(__g_rtc_player_volume));


    printf("syscfg_write finished:%d\n", __g_rtc_player_volume);

    return 1;
}

int app_adk_play_pause() {
    g_player_pause = true;
    g_palyer_pause_interval = 20;
}

static int vfs_fread(void *file, void *data, u32 len)
{
    // printf("%s, start,len:%d\n", __func__, len);
    if (s_play_state != AUDIO_PLAY_START)
    {
        return len;
    }
    memset(data, 0, len);
    if (g_play_feed_cb)
    {
#ifdef D_BRTC_ENABLE_G722
        int rlen = g_play_feed_cb(data, len);
        if (rlen < 0) {
            rlen = 0;
        }
        if ((u32)rlen < len) {
            memset((u8 *)data + rlen, 0, len - rlen);
        }
#else
        u32 enc_len = len >> 1;
        static unsigned char ulaw_buf[320];
        int enc_rlen = g_play_feed_cb((char *)ulaw_buf, enc_len);
        if (enc_rlen < 0) {
            enc_rlen = 0;
        }
        if ((u32)enc_rlen > enc_len) {
            enc_rlen = enc_len;
        }
        short *pcm = (short *)data;
        for (int i = 0; i < enc_rlen; i++) {
            pcm[i] = __ulaw2linear(ulaw_buf[i]);
        }
#endif
    }
    int32_t cur_time =  timer_get_ms();
    if (g_player_pause) { //调用时间间隔
        if (g_palyer_pause_interval-- >0) {
            memset(data, 0, len);
        }else {
            g_player_pause  = false;
        }
    }
    //printf("%s, done,len:%d, cur_time = %d\n", __func__, len, cur_time);
    return len;
}

static int vfs_fclose(void *file)
{
    return 0;
}

static const struct audio_vfs_ops audioplay_vfs_ops =
{
    .fread = vfs_fread,
    .fclose = vfs_fclose,
};

static void play_dec_server_event_handler(void *priv, int argc, int *argv)
{
    printf("[play_dec_server_event_handler] %s event %d", __func__, argv[0]);
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_ERR:
    case AUDIO_SERVER_EVENT_END:
        printf("AUDIO_SERVER_EVENT_END\n");
        if(g_play_end_cb)
            g_play_end_cb();
        break;
    case AUDIO_SERVER_EVENT_CURR_TIME:
        break;
    case AUDIO_SERVER_EVENT_SPEAK_START:
        printf("+++vad start++++\n");
        break;
    case AUDIO_SERVER_EVENT_SPEAK_STOP:
        printf("+++vad stop++++\n");
        if(g_play_end_cb)
            g_play_end_cb();
        break;
    default:
        break;
    }
}

int app_sdk_audio_play_start(app_sdk_play_feed_cb cb_feed, app_sdk_play_end_cb cb_end)
{
    os_mutex_pend(&s_play_mutex, 0);

    /* 已预热：DAC 已打开，只需更新回调即可 */
    if (g_has_play && s_dec_server && cb_feed) {
        g_play_feed_cb = cb_feed;
        g_play_end_cb  = cb_end;
        void (*notify)(void) = s_play_ready_notify;
        s_play_ready_notify = NULL; /* 只触发一次 */
        os_mutex_post(&s_play_mutex);
        printf("[PLAY] subscriber ready, callbacks updated\n");
        if (notify) {
            notify();
        }
        return 0;
    }

    if (!g_has_play)
    {
        g_has_play = true;
        int err;
        union audio_req req = {0};
        g_play_feed_cb = cb_feed;
        g_play_end_cb = cb_end;

        if (!s_dec_server) {
            s_dec_server = server_open("audio_server", "dec");
            if (!s_dec_server) {
                printf("app_sdk_audio_play_start s_dec_server null\n");
                os_mutex_post(&s_play_mutex);
                return -1;
            }
            server_register_event_handler_to_task(s_dec_server, NULL, play_dec_server_event_handler, "app_core");
        } else {
            os_mutex_post(&s_play_mutex);
            return -1;
        }

        syscfg_read(CFG_MUSIC_VOL, &__g_rtc_player_volume, sizeof(__g_rtc_player_volume));
        if (__g_rtc_player_volume < INIT_VOLUME_VALUE || __g_rtc_player_volume > MAX_VOLUME_VALUE) {
            __g_rtc_player_volume = INIT_VOLUME_VALUE;
            syscfg_write(CFG_MUSIC_VOL, &__g_rtc_player_volume, sizeof(__g_rtc_player_volume));
        }

        req.dec.cmd             = AUDIO_DEC_OPEN;
        req.dec.volume          = __g_rtc_player_volume * ceofVolume;
        req.dec.output_buf      = NULL;
        req.dec.output_buf_len  = 0;
        req.dec.channel         = 1;
        req.dec.sample_rate     = 0;
        req.dec.priority        = 1;
        req.dec.vfs_ops         = &audioplay_vfs_ops;
        req.dec.dec_type        = "pcm";
        req.dec.sample_source   = "dac";

#ifdef D_BRTC_ENABLE_G722
        req.dec.output_buf_len  = 640;
        req.dec.channel         = 1;
        req.dec.sample_rate     = 16000;
#else
        req.dec.output_buf_len  = 640;
        req.dec.channel         = 1;
        req.dec.sample_rate     = 8000;
#endif
        printf("app_sdk_audio_play_start AUDIO_REQ_DEC task\n");
        //打开解码器
        if (server_request(s_dec_server, AUDIO_REQ_DEC, &req) != 0) {
            os_mutex_post(&s_play_mutex);
            return -1;
        }

        printf("app_sdk_audio_play_start AUDIO_DEC_START task\n");
        //开始解码
        s_play_state = AUDIO_PLAY_START;
        req.dec.cmd = AUDIO_DEC_START;
        if (server_request(s_dec_server, AUDIO_REQ_DEC, &req) != 0) {
            os_mutex_post(&s_play_mutex);
            return -1;
        }
        os_mutex_post(&s_play_mutex);
        return 0;
    }
    os_mutex_post(&s_play_mutex);
    return -1;
}

/* 设置订阅端就绪通知回调（预热后 SDK 首次调用 play_start 时触发） */
void app_sdk_audio_play_set_ready_notify(void (*cb)(void))
{
    s_play_ready_notify = cb;
}

/* 预热播放通道：提前打开 DAC，避免首次播放时因 DAC 初始化延迟导致音频被截断 */
int app_sdk_audio_play_prewarm(void)
{
    printf("[PLAY] prewarm: opening DAC early...\n");
    return app_sdk_audio_play_start(NULL, NULL);
}

int app_sdk_audio_play_stop(void)
{
    os_mutex_pend(&s_play_mutex, 0);
    printf("app_sdk_audio_play_stop = %d\n", g_has_play);
    if (g_has_play) {
        g_has_play = false;

        int err;
        union audio_req req;
        if (!s_dec_server || s_play_state == AUDIO_PLAY_STOP) {
            os_mutex_post(&s_play_mutex);
            return 0;
        }
        s_play_state = AUDIO_PLAY_STOP;
        req.dec.cmd = AUDIO_DEC_STOP;
        err = server_request(s_dec_server, AUDIO_REQ_DEC, &req);

        server_close(s_dec_server);
        s_dec_server = NULL;
    }
    os_mutex_post(&s_play_mutex);
    return 0;
}

extern int mp3_get_dec_volume();

int app_sdk_audio_play_pause(bool pause) {

    os_mutex_pend(&s_play_mutex, 0);
    if (g_has_play) {
        int err;
        union audio_req req;
        if (!s_dec_server || s_play_state == AUDIO_PLAY_STOP) {
            os_mutex_post(&s_play_mutex);
            return 0;
        }
        if (pause) {

            s_play_state = AUDIO_PLAY_PAUSE;
            req.dec.cmd = AUDIO_DEC_PAUSE;
        } else {
            s_play_state = AUDIO_PLAY_START;
            req.dec.cmd = AUDIO_DEC_START ;
        }
        err = server_request(s_dec_server, AUDIO_REQ_DEC, &req);

    }
    os_mutex_post(&s_play_mutex);

    /*if (!pause) { //丢弃5帧数据 160*2*4*5
            char buf[1024]= {0, };
        for(int i = 0; i < 5; i++){
            if(g_play_feed_cb){
                g_play_feed_cb(buf, 960);
            }
        }
    }*/

    /*
    if(mp3_get_dec_volume() != __g_rtc_player_volume){
        app_sdk_audio_play_volume_lite(__g_rtc_player_volume);
    }*/
    return 0;
}

#endif /* D_MODULE_BRTC_AGENT_ENABLE */
