#include "server/audio_server.h"
#include "server/server_core.h"
#include "fs/fs.h"
#include "media/spectrum/SpectrumShow_api.h"
#include "app_config.h"


#define CONFIG_PRODUCT_AUDIO_TEST_ADC_CHANNEL       1
#define CONFIG_PRODUCT_AUDIO_TEST_ADC_GAIN          100
#define CONFIG_PRODUCT_AUDIO_TEST_DAC_GAIN          100
#define CONFIG_PRODUCT_AUDIO_TEST_ADC_SAMPLERATE    44100

#define VOLUME_STEP         5
#define GAIN_STEP           5
#define MIN_VOLUME_VALUE	5
#define MAX_VOLUME_VALUE	100

struct product_audio_test_hdl {
    FILE *fp;
    struct server *enc_server;
    struct server *dec_server;
    const char *sample_source;
    int sample_rate;
    u8 volume;
    u8 gain;
    u8 channel;
    u8 spectrum_num;
    u8 spectrum_num_contrast;
    u8 spectrum_diff_val;
    u16 spectrum_cnt;
    void *work_buf;
};

static struct product_audio_test_hdl product_audio_test_handler;

#define __this (&product_audio_test_handler)

struct pt_sin_info {
    const char *file_name;
    u8 spectrum_num;            // 频谱点
    u8 spectrum_num_contrast;   // 对比频谱点
    u8 spectrum_diff_val;       // 频谱差值
};

static struct pt_sin_info pt_sin_table[] = {
    {"sine500", 0x03, 0x05, 0x05},
    {"sine1k",  0x04, 0x06, 0x05},
    {"sine3k2", 0x04, 0x06, 0x05},
};

//AUDIO ADC支持的采样率
static const u16 sample_rate_table[] = {
    8000,
    11025,
    12000,
    16000,
    22050,
    24000,
    32000,
    44100,
    48000,
};

static int product_audio_test_vfs_fwrite(void *file, void *data, u32 len)
{
#ifdef CONFIG_SPECTRUM_FFT_EFFECT_ENABLE
    SpectrumShowRun(file, data, len / 2);

    short *db_data = getSpectrumValue(file);
    int db_num = getSpectrumNum(file);

    if (__this->spectrum_cnt && (db_num > __this->spectrum_num) && (db_num > __this->spectrum_num_contrast)) {
        s16 diff_val = db_data[__this->spectrum_num] - db_data[__this->spectrum_num_contrast];
        /* log_debugebug("[%d %d %d %d]",db_data[__this->spectrum_num],db_data[__this->spectrum_num_contrast],diff_val, __this->spectrum_diff_val); */
        if (diff_val > __this->spectrum_diff_val) {
            __this->spectrum_cnt--;
        }
    }

    if (db_data && db_num > 0) {
        for (int i = 0; i < db_num; i++) {
            //输出db_num个 db值
            /* printf("db_data db[%d] %d\n", i, db_data[i]); */
        }
    }
#endif

    return len;
}

static int product_audio_test_vfs_fread(void *file, void *data, u32 len)
{
    return len;
}

static int product_audio_test_vfs_fclose(void *file)
{
    return 0;
}

static int product_audio_test_vfs_flen(void *file)
{
    return 0;
}

static const struct audio_vfs_ops product_audio_test_vfs_ops = {
    .fwrite = product_audio_test_vfs_fwrite,
    .fread  = product_audio_test_vfs_fread,
    .fclose = product_audio_test_vfs_fclose,
    .flen   = product_audio_test_vfs_flen,
};

static int product_audio_test_close(void)
{
    union audio_req req = {0};

    if (__this->enc_server) {
        req.enc.cmd = AUDIO_ENC_CLOSE;
        server_request(__this->enc_server, AUDIO_REQ_ENC, &req);
    }

    if (__this->dec_server) {
        req.dec.cmd = AUDIO_DEC_STOP;
        server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    }

    if (__this->fp) {
        fclose(__this->fp);
        __this->fp = NULL;
    }

#ifdef CONFIG_SPECTRUM_FFT_EFFECT_ENABLE
    if (__this->work_buf) {
        free(__this->work_buf);
        __this->work_buf = NULL;
    }
#endif

    return 0;
}

static int product_audio_test_start(int sample_rate, u8 channel, const char *file_name)
{
    int err;
    union audio_req req = {0};

    char path[64];

    snprintf(path, sizeof(path), CONFIG_VOICE_PROMPT_FILE_PATH"%s.*", file_name);

    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }

    if (channel > 2) {
        channel = 2;
    }

#ifdef CONFIG_SPECTRUM_FFT_EFFECT_ENABLE
    __this->work_buf = zalloc(getSpectrumShowBuf());
    if (!__this->work_buf) {
        fclose(file);
        return -1;
    }

    SpectrumShowInit(__this->work_buf, 0.9, 0.9,
                     sample_rate, channel, channel > 1 ? 2 : 0, JL_FFT_BASE);
#endif

    /****************打开解码DAC器*******************/
    req.dec.cmd             = AUDIO_DEC_OPEN;
    req.dec.volume          = __this->volume;
    req.dec.sample_source   = CONFIG_AUDIO_DEC_PLAY_SOURCE;
    req.dec.file            = file;

    err = server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    if (err) {
        goto __err;
    }

    req.dec.cmd = AUDIO_DEC_START;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);

    /****************打开编码器*******************/
    memset(&req, 0, sizeof(union audio_req));

    //BIT(x)用来区分上层需要获取哪个通道的数据
    if (channel == 2) {
        req.enc.channel_bit_map = BIT(CONFIG_AUDIO_ADC_CHANNEL_L) | BIT(CONFIG_AUDIO_ADC_CHANNEL_R);
    } else {
        req.enc.channel_bit_map = BIT(CONFIG_AUDIO_ADC_CHANNEL_L);
    }
    req.enc.frame_size = 512 * 2 * channel;	//收集够多少字节PCM数据就回调一次fwrite
    req.enc.output_buf_len = req.enc.frame_size * 3; //底层缓冲buf至少设成3倍frame_size
    req.enc.cmd = AUDIO_ENC_OPEN;
    req.enc.channel = channel;
    req.enc.volume = __this->gain;
    req.enc.sample_rate = sample_rate;
    req.enc.format = "pcm";
    req.enc.sample_source = __this->sample_source;
    req.enc.vfs_ops = &product_audio_test_vfs_ops;
    req.enc.file = (FILE *)__this->work_buf;

    err = server_request(__this->enc_server, AUDIO_REQ_ENC, &req);
    if (err) {
        goto __err1;
    }

    __this->fp = file;

    return 0;

__err1:
    req.dec.cmd = AUDIO_DEC_STOP;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);

__err:
    fclose(file);
    free(__this->work_buf);
    __this->work_buf = NULL;

    return -1;
}

static void product_audio_test_play_pause(void)
{
    union audio_req req = {0};

    req.dec.cmd = AUDIO_DEC_PP;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);

    req.enc.cmd = AUDIO_ENC_PP;
    server_request(__this->enc_server, AUDIO_REQ_ENC, &req);
}

//调整ADC的模拟增益
static int product_audio_test_enc_gain_change(int step)
{
    union audio_req req = {0};

    int gain = __this->gain + step;
    if (gain < 0) {
        gain = 0;
    } else if (gain > 100) {
        gain = 100;
    }
    if (gain == __this->gain) {
        return -1;
    }
    __this->gain = gain;

    if (!__this->enc_server) {
        return -1;
    }

    log_d("set_enc_gain: %d\n", gain);

    req.enc.cmd     = AUDIO_ENC_SET_VOLUME;
    req.enc.volume  = gain;
    return server_request(__this->enc_server, AUDIO_REQ_ENC, &req);
}

//调整DAC的数字音量和模拟音量
static int product_audio_test_dec_volume_change(int step)
{
    union audio_req req = {0};

    int volume = __this->volume + step;
    if (volume < MIN_VOLUME_VALUE) {
        volume = MIN_VOLUME_VALUE;
    } else if (volume > MAX_VOLUME_VALUE) {
        volume = MAX_VOLUME_VALUE;
    }
    if (volume == __this->volume) {
        return -1;
    }
    __this->volume = volume;

    if (!__this->dec_server) {
        return -1;
    }

    log_d("set_dec_volume: %d\n", volume);

    req.dec.cmd     = AUDIO_DEC_SET_VOLUME;
    req.dec.volume  = volume;
    return server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
}

static void product_audio_test_ready(void *p)
{
    u8 index = (u8)p;

    product_audio_test_close();

    if (__this->spectrum_cnt) {
        printf("spectrum err\n");
        return;
    }

    if (index >= ARRAY_SIZE(pt_sin_table)) {
        printf("product audio test end\n");
        return;
    }

    __this->spectrum_num = pt_sin_table[index].spectrum_num;
    __this->spectrum_num_contrast = pt_sin_table[index].spectrum_num_contrast;
    __this->spectrum_diff_val = pt_sin_table[index].spectrum_diff_val;
    __this->spectrum_cnt = 100;

    product_audio_test_start(__this->sample_rate, __this->channel, pt_sin_table[index].file_name);

    ++index;

    sys_timeout_add((void *)index, product_audio_test_ready, 3000);
}

int product_audio_test_init(void)
{
    memset(__this, 0, sizeof(struct product_audio_test_hdl));

#if CONFIG_AUDIO_ENC_SAMPLE_SOURCE == AUDIO_ENC_SAMPLE_SOURCE_PLNK0
    __this->sample_source = "plnk0";
#elif CONFIG_AUDIO_ENC_SAMPLE_SOURCE == AUDIO_ENC_SAMPLE_SOURCE_PLNK1
    __this->sample_source = "plnk1";
#elif CONFIG_AUDIO_ENC_SAMPLE_SOURCE == AUDIO_ENC_SAMPLE_SOURCE_IIS0
    __this->sample_source = "iis0";
#elif CONFIG_AUDIO_ENC_SAMPLE_SOURCE == AUDIO_ENC_SAMPLE_SOURCE_IIS1
    __this->sample_source = "iis1";
#elif CONFIG_AUDIO_ENC_SAMPLE_SOURCE == AUDIO_ENC_SAMPLE_SOURCE_LINEIN
    __this->sample_source = "linein";
#else
    __this->sample_source = "mic";
#endif

    __this->volume = CONFIG_PRODUCT_AUDIO_TEST_DAC_GAIN;
    __this->channel = CONFIG_PRODUCT_AUDIO_TEST_ADC_CHANNEL;
    __this->gain = CONFIG_PRODUCT_AUDIO_TEST_ADC_GAIN;
    __this->sample_rate = CONFIG_PRODUCT_AUDIO_TEST_ADC_SAMPLERATE;

    __this->enc_server = server_open("audio_server", "enc");

    __this->dec_server = server_open("audio_server", "dec");

    product_audio_test_ready(0);
}

void product_audio_test_exit(void)
{
    product_audio_test_close();
    server_close(__this->dec_server);
    __this->dec_server = NULL;
    server_close(__this->enc_server);
    __this->enc_server = NULL;
}

