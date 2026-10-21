//#include <stdlib.h>
#include <string.h>
#include "system/includes.h"
#include "server/audio_server.h"
#include "device/device.h"
#include "system/app_core.h"
#include "server/server_core.h"
#include "app_sdk_audiorecorder.h"
#include "app_config.h"
//#include "board_config.h"

#ifdef D_MODULE_BRTC_AGENT_ENABLE

//RingBuffer* playerUseData = NULL;

static app_sdk_record_feed_cb g_reocrd_feed_cb = NULL;
static app_sdk_record_end_cb g_reocrd_end_cb = NULL;
static volatile bool s_has_record = false;
static struct server *s_enc_server = NULL;
static rec_state_t s_rec_state = AUDIO_RECRD_STOP;
static OS_MUTEX s_record_mutex;
static int __record_mutex_init()
{
/*
    if (NULL == playerUseData) {

        playerUseData = WebRtc_CreateBuffer(3000, 320);// 缓存3s的数据
    }
*/
    return os_mutex_create(&s_record_mutex);
}
late_initcall(__record_mutex_init);

void AecSetDataPlayer(char * buf, int len) {

    /*int ret = WebRtc_WriteBuffer(playerUseData, buf, len); // 写入1个元素
    if (ret != 0) {
        // 缓存区满了，
    }*/
}

bool g_record_real = true;
void mute_record(bool record) {
    g_record_real = record;
}

char buf_default = {0,};
static int vfs_fwrite(void *file, void *data, u32 len)
{
    if (s_rec_state != AUDIO_RECRD_START)
    {
        return len;
    }
    if(g_reocrd_feed_cb){
        if (g_record_real) {
            g_reocrd_feed_cb(data,len);
        }
    }

    return len;

}

static int vfs_fclose(void *file)
{
    return 0;
}

static const struct audio_vfs_ops audiorecord_vfs_ops =
{
    .fwrite = vfs_fwrite,
    .fclose = vfs_fclose,
};

static void record_enc_server_event_handler(void *priv, int argc, int *argv)
{
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_ERR:
    case AUDIO_SERVER_EVENT_END:
        printf("AUDIO_SERVER_EVENT_END\n");
        if(g_reocrd_end_cb)
            g_reocrd_end_cb();
        break;
    case AUDIO_SERVER_EVENT_CURR_TIME:
        break;
    case AUDIO_SERVER_EVENT_SPEAK_START:
        printf("[VAD] SPEAK_START\n");
        break;
    case AUDIO_SERVER_EVENT_SPEAK_STOP:
        // if(g_reocrd_end_cb)␍
        //     g_reocrd_end_cb();␍
        printf("[VAD] SPEAK_STOP\n");
        /* 注意：不调用 g_reocrd_end_cb()，否则会关闭整个录音通道。
         * BRTC 持续对话模式下录音生命周期由 SDK 控制，VAD 仅标记状态 */
        break;
    default:
        break;
    }
}
int app_sdk_audio_record_start(int max_time_ms,int used_vad,app_sdk_record_feed_cb cb_feed,app_sdk_record_end_cb cb_end,char *format)
{
    os_mutex_pend(&s_record_mutex, 0);
    if (!s_has_record)
    {
        g_record_real = true;
        s_has_record = true;
        int err;
        union audio_req req = {0};
        g_reocrd_feed_cb = cb_feed;
        g_reocrd_end_cb = cb_end;

        if (!s_enc_server)
        {
            s_enc_server = server_open("audio_server", "enc");
            if (!s_enc_server)
            {
                printf("coapi_audio_record_start\n");

                os_mutex_post(&s_record_mutex);
                return -1;
            }
            server_register_event_handler_to_task(s_enc_server, NULL, record_enc_server_event_handler,"app_core");
        }
        else
        {
            os_mutex_post(&s_record_mutex);
            return -1;
        }

        req.enc.cmd = AUDIO_ENC_OPEN;
        req.enc.channel = 1;
        req.enc.volume = 50;// USER_MIC_ASR_CLOUD_ADC_GAIN_AI;
        req.enc.priority = 0;
        req.enc.output_buf = NULL;
#ifdef CONFIG_NO_SDRAM_ENABLE
        req.enc.output_buf_len = 3200*4;
#else
        req.enc.output_buf_len = 3200*8;
#endif // CONFIG_NO_SDRAM_ENABLE
        req.enc.sample_rate = 16000;
        req.enc.format = format;//填入格式，比如speex,opus,或者其他。填入pcm的话，可以再vfs_fwrite里面处理录音吐出来的数据
        req.enc.frame_size = 3200;
        req.enc.sample_source = "mic";
        req.enc.vfs_ops = &audiorecord_vfs_ops;
        req.enc.dns_enable = 0;     /* 独立 DNS 模块关闭（降噪走 AEC 内置 DNS，见 EnableBit BIT(5)） */
        req.enc.use_vad = 1;         /* 本地 VAD 开启（1=旧算法, 2=JL新算法） */
        req.enc.vad_auto_refresh = 1; /* 自动循环：SPEAK_START→SPEAK_STOP→SPEAK_START… */
        req.enc.vad_start_threshold = 300; /* 连续有声 300ms 认为开始说话 */
        req.enc.vad_stop_threshold = 800;  /* 连续无声 800ms 才认为停止说话（减少误触发） */
        req.enc.msec = max_time_ms;
//#if defined BRTC_SDK_ENABLE
        req.enc.sample_rate = 8000;
        req.enc.output_buf_len = 320*4;
        req.enc.frame_size = 320;
        req.enc.volume = 50;
//#endif // defined
#ifdef D_BRTC_ENABLE_G722
        req.enc.sample_rate = 16000;
        req.enc.output_buf_len = 640 *4 ;
        req.enc.frame_size = 640;
        req.enc.volume = 50;
#endif
        printf("req.enc.use_vad:%d, vad_start:%d, vad_stop:%d\n",
               req.enc.use_vad, req.enc.vad_start_threshold, req.enc.vad_stop_threshold);
#ifdef CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
        //req.enc.channel_bit_map = BIT(USER_MIC_ASR_CLOUD_ADC_CHANNEL);
        req.enc.channel_bit_map =   BIT(CONFIG_AUDIO_ADC_CHANNEL_L);

#endif


        /*
         * AEC 音频前处理（本地算法，全部运行在设备端）
         * EnableBit 各位含义：
         *   BIT(0) - AEC  回声消除
         *   BIT(1) - NLP  非线性处理
         *   BIT(2) - ANS  噪声抑制
         *   BIT(4) - AGC  自动增益控制
         *   BIT(5) - DNS  深度降噪（AEC 内置通路）
         * 当前配置 enablebit=39(0x27): AEC+NLP+ANS+DNS 开启, AGC 关闭
         */
        struct aec_s_attr aec_param = {0};

        req.enc.aec_attr = &aec_param;
        req.enc.aec_enable = 1;

        extern void get_cfg_file_aec_config(struct aec_s_attr * aec_param);
        get_cfg_file_aec_config(&aec_param);

        #if 1
            aec_param.ANS_NoiseLevel =      2.2e3f; //初始噪声水平,用来加速降噪收敛,跟 mic 信号的信噪比有关。 Mic 信号信噪比高， 该值可以小一点， 反之则需要稍微大一点。default: 2.2e3f(0 ~ 32767)
        #endif

        if (aec_param.EnableBit == 0) { /* 配置文件未启用任何 AEC 子模块则整体关闭 */
            req.enc.aec_enable = 0;
            req.enc.aec_attr = NULL;
        }

#if defined(CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE) && defined(CONFIG_AISP_LINEIN_ADC_CHANNEL) && defined(CONFIG_PHONE_CALL_ADC_CHANNEL)
        if (req.enc.aec_enable) {
            aec_param.output_way = 0;               //1:使用硬件回采 0:使用软件回采
            if (aec_param.output_way) {
                req.enc.channel_bit_map |= BIT(CONFIG_AISP_LINEIN_ADC_CHANNEL);             //配置回采硬件通道
                if (CONFIG_AISP_LINEIN_ADC_CHANNEL < CONFIG_PHONE_CALL_ADC_CHANNEL) {
                    req.enc.ch_data_exchange = 1;     //如果回采通道使用的硬件channel比MIC通道使用的硬件channel靠前的话处理数据时需要交换一下顺序
                }
            }
        }
#endif

        if (req.enc.sample_rate == 16000) { /* G722: 16kHz 宽带 AEC */
            aec_param.wideband = 1;
            aec_param.hw_delay_offset = 50;
        } else {                              /* u-law: 8kHz 窄带 AEC */
            aec_param.wideband = 0;
            aec_param.hw_delay_offset = 250;
        }

        s_rec_state = AUDIO_RECRD_START;
        err = server_request(s_enc_server, AUDIO_REQ_ENC, &req);
        os_mutex_post(&s_record_mutex);

        return 0;
    }
    os_mutex_post(&s_record_mutex);

    return -1;
}

int app_sdk_audio_record_stop(void)
{
    os_mutex_pend(&s_record_mutex, 0);
    printf("coapi_audio_record_stop = %d\n", s_has_record);
    if (s_has_record)
    {
        s_has_record = false;

        int err;
        union audio_req req;
        if (!s_enc_server || s_rec_state == AUDIO_RECRD_STOP)
        {
            os_mutex_post(&s_record_mutex);
            return 0;
        }
        s_rec_state = AUDIO_RECRD_STOP;
        req.enc.cmd = AUDIO_ENC_CLOSE;
        err = server_request(s_enc_server, AUDIO_REQ_ENC, &req);

        server_close(s_enc_server);
        s_enc_server = NULL;
#if defined(CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE) && defined(CONFIG_ASR_CLOUD_ADC_CHANNEL) && defined(CONFIG_AISP_LINEIN_ADC_GAIN)
        extern void adc_multiplex_set_gain(const char *source, u8 channel_bit_map, u8 gain);
        //adc_multiplex_set_gain("mic", BIT(USER_MIC_ASR_CLOUD_ADC_CHANNEL), USER_MIC_ASR_CLOUD_ADC_GAIN);
        adc_multiplex_set_gain("mic", BIT(CONFIG_ASR_CLOUD_ADC_CHANNEL), CONFIG_AISP_LINEIN_ADC_GAIN);
#endif
    }
    os_mutex_post(&s_record_mutex);
    return 0;
}

#endif /* D_MODULE_BRTC_AGENT_ENABLE */
