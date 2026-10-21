/**
 * @file d_sdk_audio_config.h
 * @brief JieLi audio codec, recorder, DAC, ADC and EQ configuration.
 */

#ifndef D_SDK_AUDIO_CONFIG_H
#define D_SDK_AUDIO_CONFIG_H

#ifdef CONFIG_AUDIO_ENABLE
#define CONFIG_PCM_DEC_ENABLE
#define CONFIG_WAV_DEC_ENABLE

/* A standalone reminder does not need cloud codecs or microphone DSP. */
#if !D_LOCAL_PROMPT_ENABLE || D_FEATURE_QWEN || D_FEATURE_XIAOLE_WAKEUP || \
    D_FEATURE_BRTC || D_FEATURE_RTC_AUDIO || D_FEATURE_LOCAL_MUSIC || defined(D_AUDIO_DEMO)
#define CONFIG_MIDI_CTRL_DEC_ENABLE

#if __FLASH_SIZE__ > (1 * 1024 * 1024)
#define CONFIG_MP3_DEC_ENABLE
#define CONFIG_M4A_DEC_ENABLE
#endif

#if __FLASH_SIZE__ > (2 * 1024 * 1024)
#define CONFIG_DTS_DEC_ENABLE
#define CONFIG_ADPCM_DEC_ENABLE
#define CONFIG_WMA_DEC_ENABLE
#define CONFIG_AMR_DEC_ENABLE
#define CONFIG_APE_DEC_ENABLE
#define CONFIG_FLAC_DEC_ENABLE
#define CONFIG_SPEEX_DEC_ENABLE
#define CONFIG_OPUS_DEC_ENABLE
#define CONFIG_OGG_DEC_ENABLE
#endif

#define CONFIG_PCM_ENC_ENABLE
#define CONFIG_WAV_ENC_ENABLE

#if __FLASH_SIZE__ > (1 * 1024 * 1024)
#define CONFIG_VIRTUAL_DEV_ENC_ENABLE
#define CONFIG_SPEEX_ENC_ENABLE
#define CONFIG_OPUS_ENC_ENABLE
#define CONFIG_VAD_ENC_ENABLE
#endif

#if __FLASH_SIZE__ > (2 * 1024 * 1024)
#define CONFIG_MP3_ENC_ENABLE
#define CONFIG_ADPCM_ENC_ENABLE
#define CONFIG_AMR_ENC_ENABLE
#define CONFIG_AEC_ENC_ENABLE
#define CONFIG_DNS_ENC_ENABLE
#endif
#endif /* Full audio feature set; PCM/WAV remain available for local cues. */

#define CONFIG_LOCAL_MUSIC_EQ_ENABLE        0
#define CONFIG_LOCAL_MUSIC_DRC_ENABLE       0
#define LOCAL_MUSIC_PRINTF_ENABLE           1
#define CONFIG_AUDIO_MIX_ENABLE
#define CONFIG_AUDIO_PS_ENABLE
#define CONFIG_JLFAT_ENABLE

#define AUDIO_ENC_SAMPLE_SOURCE_MIC         0
#define AUDIO_ENC_SAMPLE_SOURCE_PLNK0       1
#define AUDIO_ENC_SAMPLE_SOURCE_PLNK1       2
#define AUDIO_ENC_SAMPLE_SOURCE_IIS0        3
#define AUDIO_ENC_SAMPLE_SOURCE_IIS1        4
#define AUDIO_ENC_SAMPLE_SOURCE_LINEIN      5

#define CONFIG_AUDIO_ENC_SAMPLE_SOURCE      AUDIO_ENC_SAMPLE_SOURCE_MIC
#define CONFIG_AUDIO_DEC_PLAY_SOURCE        "dac"
#define CONFIG_AUDIO_RECORDER_SAMPLERATE    16000
#define CONFIG_AUDIO_RECORDER_CHANNEL       1
#define CONFIG_AUDIO_RECORDER_DURATION      (30 * 1000)
#define CONFIG_AUDIO_RECORDER_SAVE_FORMAT   "ogg"
#define CONFIG_AUDIO_RECORDER_PLAY_FORMAT   "pcm"

#ifndef TCFG_DAC_MUTE_PORT
#define TCFG_DAC_MUTE_PORT                  IO_PORTB_02
#endif
#ifndef TCFG_DAC_MUTE_VALUE
#define TCFG_DAC_MUTE_VALUE                 0
#endif
#ifndef TCFG_DAC_DIFF_OUTPUT
#define TCFG_DAC_DIFF_OUTPUT                0
#endif
#ifndef TCFG_DAC_HW_CHANNEL
#define TCFG_DAC_HW_CHANNEL                 0x05
#endif
#ifndef TCFG_DAC_CH_NUM
#define TCFG_DAC_CH_NUM                     4
#endif
#ifndef TCFG_DAC_MUTE_DELAY_MS
#define TCFG_DAC_MUTE_DELAY_MS              200
#endif

#define CONFIG_ALL_ADC_CHANNEL_OPEN_ENABLE
#define CONFIG_AUDIO_ADC_CHANNEL_L          1
#define CONFIG_AUDIO_ADC_CHANNEL_R          3
#define CONFIG_REVERB_ADC_CHANNEL           1
#define CONFIG_UAC_MIC_ADC_CHANNEL          1
#define CONFIG_AUDIO_ADC_GAIN               100
#define CONFIG_AISP_MIC0_ADC_CHANNEL        1
#define CONFIG_AISP_MIC1_ADC_CHANNEL        3
#define CONFIG_AISP_LINEIN_ADC_CHANNEL      2
#define CONFIG_AISP_MIC_ADC_GAIN            80
#define CONFIG_AISP_LINEIN_ADC_GAIN         60
#else
#ifndef TCFG_DAC_MUTE_PORT
#define TCFG_DAC_MUTE_PORT                  0xff
#endif
#ifndef TCFG_DAC_MUTE_VALUE
#define TCFG_DAC_MUTE_VALUE                 0
#endif
#endif /* CONFIG_AUDIO_ENABLE */

#ifndef CONFIG_JLFAT_ENABLE
#define CONFIG_JLFAT_ENABLE
#endif

#if defined CONFIG_AUDIO_ENABLE && __FLASH_SIZE__ > (1 * 1024 * 1024)
#define TCFG_EQ_ENABLE                      1
#else
#define TCFG_EQ_ENABLE                      0
#endif
#define TCFG_EQ_ONLINE_ENABLE               0
#define TCFG_HW_SOFT_EQ_ENABLE              1
#define TCFG_LIMITER_ENABLE                 1
#define TCFG_DRC_ENABLE                     TCFG_LIMITER_ENABLE
#define TCFG_EQ_FILE_ENABLE                 1
#define TCFG_EQ_MODE_CHOOSE                 0

#if (TCFG_EQ_MODE_CHOOSE == 0)
#define TCFG_AUDIO_MDRC_ENABLE              2
#define TCFG_LAST_WHOLE_DRC_ENABLE          1
#else
#define TCFG_EQ_DIVIDE_ENABLE               1
#define TCFG_EQ_SPILT_ENABLE                1
#define TCFG_DRC_SPILT_ENABLE               1
#endif

#define TCFG_NULL_COMM                      0
#define TCFG_USB_COMM                       1
#if TCFG_EQ_ONLINE_ENABLE && defined EQ_CORE_V1
#define TCFG_COMM_TYPE                      TCFG_USB_COMM
#else
#define TCFG_COMM_TYPE                      TCFG_NULL_COMM
#endif

#endif /* D_SDK_AUDIO_CONFIG_H */
