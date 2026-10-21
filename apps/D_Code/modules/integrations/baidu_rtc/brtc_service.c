//*********************************************************************************//
//                              BRTC 百度 RTC 智能体服务                          //
//                                                                                 //
//   基于 BRTC demo brtc_wrapper.c 模式，集成到 D_Code 事件总线                   //
//   链路：WiFi就绪 → HTTP换Token → 创建引擎 → 登录 → 呼叫 → 对话 → 销毁       //
//*********************************************************************************//

#include "config/app_config.h"

#if D_FEATURE_BRTC

/* 解决 cpu.h typedef bool 与 stdbool.h #define bool _Bool 冲突 */
#define BOOL_DEFINE_CONFLICT

#include "brtc_service.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "framework/d_event_payloads.h"
#include "os/os_api.h"
#include "system/timer.h"
#include "http/http_cli.h"
#include "baidu_chat_agents_engine.h"
#include "app_sdk_audiorecorder.h"
#include "app_sdk_localdata_audio_play.h"
#include "cJSON.h"
#include "framework/compat/d_stdio_compat.h"
#include <string.h>
#include <stdlib.h>

#define TAG "BRTC"

/* 提示音路径 */
#ifdef CONFIG_VOICE_PROMPT_FILE_PATH
#define AGENT_QUITE_NOTICE      CONFIG_VOICE_PROMPT_FILE_PATH"AgentFailed.mp3"
#define AGENT_RECOVER_NOTICE    CONFIG_VOICE_PROMPT_FILE_PATH"AgentRe.mp3"
#else
#define AGENT_QUITE_NOTICE      "AgentFailed.mp3"
#define AGENT_RECOVER_NOTICE    "AgentRe.mp3"
#endif

/* HTTP POST Body */
#ifdef D_BRTC_ENABLE_G722
#define BRTC_POST_DATA \
    "{ \"app_id\": \"" D_BRTC_APP_ID "\", " \
    "\"config\" : \"{\\\"llm\\\" : \\\"" D_BRTC_LLM "\\\", " \
    "\\\"llm_token\\\" : \\\"no\\\", " \
    "\\\"rtc_ac\\\": \\\"g722\\\", " \
    "\\\"lang\\\" : \\\"" D_BRTC_LANG "\\\", " \
    "\\\"asr_long_audio_mode\\\" : " D_BRTC_ASR_LONG_AUDIO_MODE "}\", " \
    "\"quick_start\": true}"
#else
#define BRTC_POST_DATA \
    "{ \"app_id\": \"" D_BRTC_APP_ID "\", " \
    "\"config\" : \"{\\\"llm\\\" : \\\"" D_BRTC_LLM "\\\", " \
    "\\\"llm_token\\\" : \\\"no\\\", " \
    "\\\"rtc_ac\\\": \\\"pcmu\\\", " \
    "\\\"lang\\\" : \\\"" D_BRTC_LANG "\\\", " \
    "\\\"asr_long_audio_mode\\\" : " D_BRTC_ASR_LONG_AUDIO_MODE "}\", " \
    "\"quick_start\": true}"
#endif

#define BRTC_URL_GENERATE   D_BRTC_USERSERVER_URL "/generateAIAgentCall"
#define BRTC_URL_STOP       D_BRTC_USERSERVER_URL "/stopAIAgentInstance"
#define BRTC_HTTP_BUF_SIZE  (4 * 1024)

//*********************************************************************************//
//          外部函数声明 + 弱默认实现（链接时由其他模块覆盖）                    //
//*********************************************************************************//

/* 头文件未声明的平台层函数 */
extern int  app_sdk_audio_play_pause(bool pause);

/* 其他模块可能未提供的函数（weak 默认实现，链接时可被强符号覆盖） */
int  __attribute__((weak)) play_mix_file(const char *path)              { return 0; }
int  __attribute__((weak)) sa_net_music_audio_play_pause(bool pause)    { return 0; }
void __attribute__((weak)) sa_audio_play(const char *url, bool type)    { }
int  __attribute__((weak)) set_dec_volume(int step)                     { return 0; }
int  __attribute__((weak)) set_dec_volume_step(int step)                { return 0; }
int  __attribute__((weak)) sa_audio_is_net_music(void)                  { return 0; }
int  __attribute__((weak)) mp3_set_dec_volume_reset(void)               { return 0; }
int  __attribute__((weak)) mp3_set_dec_volume_mute(int volume)          { return 0; }

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

typedef enum {
    BRTC_STATE_IDLE = 0,
    BRTC_STATE_HTTP_REQ,
    BRTC_STATE_ENGINE_INIT,
    BRTC_STATE_CALLING,
    BRTC_STATE_RUNNING,
    BRTC_STATE_STOPPING,
} brtc_state_t;

/* 音频设备独占标识 */
static bool g_audio_device_multi_used = false;

/* TTS 播放状态 */
static bool g_brtc_speaking_tts = false;

/* 延迟播放 MP3 */
static bool g_brtc_delay_play_mp3 = false;
static char g_brtc_playurl[1024];

/* 可退出标识 */
static bool g_brtc_agent_can_exit = false;

/* 外部请求终止 */
static int g_kill_time_task_req = 0;

/* 快速退出标识 */
static bool g_brtc_agent_exit_quickly = false;

static struct {
    brtc_state_t            state;
    BaiduChatAgentEngine   *engine;
    char                   *json_str;           /* HTTP 响应原文（stop 时复用） */
    bool                    wifi_connected;
    bool                    normal_quit;
    bool                    abnormal_quit;
    bool                    abnormal_quit_first;
    bool                    license_fail;
    bool                    asr_realtime_enabled;
    u32                     time_last_asr;
    int                     engine_task_pid;
    int                     timer_task_pid;
} brtc_ctx;

//*********************************************************************************//
//                              SDK 事件回调（13 个）                               //
//*********************************************************************************//

static void on_error_cb(int errCode, const char *errMsg)
{
    LOGE(TAG, "onError: %d %s", errCode, errMsg ? errMsg : "");
    d_event_post(EVT_BRTC_ERROR, NULL, 0);
}

static void on_call_state_change_cb(AGentCallState state)
{
    LOGI(TAG, "onCallStateChange: %d", state);
    switch (state) {
    case AGENT_LOGIN_SUCCESS:
        d_event_post(EVT_BRTC_LOGIN_OK, NULL, 0);
        break;
    case AGENT_LOGIN_FAIL:
        LOGE(TAG, "LOGIN_FAIL");
        /* 播放提示音 */
        play_mix_file(AGENT_QUITE_NOTICE);
        d_event_post(EVT_BRTC_LOGIN_FAIL, NULL, 0);
        break;
    case AGENT_CALL_SUCCESS:
        if (!brtc_ctx.asr_realtime_enabled && brtc_ctx.engine) {
            LOGI(TAG, "Enable ASR realtime (from CallStateChange)");
            baidu_chat_agent_engine_send_event_to_agent(brtc_ctx.engine, ASR_EVENT_ENABLE_REALTIME);
            brtc_ctx.asr_realtime_enabled = true;
        }
        break;
    case AGENT_CALL_FAIL:
        d_event_post(EVT_BRTC_CALL_FAIL, NULL, 0);
        break;
    default:
        break;
    }
}

static void on_connection_state_change_cb(AGentConnectState state)
{
    LOGI(TAG, "onConnectionStateChange: %d", state);
    if (brtc_ctx.abnormal_quit_first && state == AGENT_CONNECTION_STATE_DISCONNECTED) {
        brtc_ctx.abnormal_quit_first = false;
        if (!brtc_ctx.normal_quit) {
            brtc_ctx.abnormal_quit = true;
        }
        d_event_post(EVT_BRTC_DISCONNECTED, NULL, 0);
    } else if (state == AGENT_CONNECTION_STATE_CONNECTED) {
        d_event_post(EVT_BRTC_CONNECTED, NULL, 0);
    } else if (state == AGENT_CONNECTION_STATE_RECONNECTING) {
        d_event_post(EVT_BRTC_RECONNECTING, NULL, 0);
    }
}

static void on_user_asr_subtitle_cb(const char *text, size_t text_len, bool final, const ASRExtInfo *info)
{
    LOGI(TAG, "ASR[%s]: %.*s", final ? "FIN" : "MID", (int)text_len, text);
    brtc_ctx.time_last_asr = timer_get_ms();
    brtc_asr_data_t asr = {0};
    size_t clen = text_len < sizeof(asr.text) - 1 ? text_len : sizeof(asr.text) - 1;
    memcpy(asr.text, text, clen);
    asr.is_final = final;
    d_event_post(EVT_BRTC_ASR_USER, &asr, sizeof(asr));
}

static void on_ai_agent_subtitle_cb(const char *text, bool final)
{
    LOGI(TAG, "AI[%s]: %s", final ? "FIN" : "MID", text ? text : "");
    brtc_ctx.time_last_asr = timer_get_ms();
    brtc_asr_data_t asr = {0};
    if (text) strncpy(asr.text, text, sizeof(asr.text) - 1);
    asr.is_final = final;
    d_event_post(EVT_BRTC_ASR_AGENT, &asr, sizeof(asr));
}

static void on_ai_agent_speaking_cb(bool speaking)
{
    g_brtc_speaking_tts = speaking;
    LOGI(TAG, "AI Speaking: %s, audio_multi_used:%d", speaking ? "START" : "STOP", g_audio_device_multi_used);

    /* 音频设备独占逻辑 */
    if (speaking) {
        if (g_audio_device_multi_used) {
            sa_net_music_audio_play_pause(true);   /* MP3 释放 */
            app_sdk_audio_play_pause(false);        /* RTC 抢过来 */
        }
    } else {
        if (g_audio_device_multi_used) {
            app_sdk_audio_play_pause(true);         /* RTC 让出 */
            sa_net_music_audio_play_pause(false);   /* MP3 恢复 */
        }
    }

    bool val = speaking;
    d_event_post(EVT_BRTC_AI_SPEAKING, &val, sizeof(val));
}

static void on_function_call_cb(const char *id, const char *params)
{
    LOGI(TAG, "FunctionCall: id=%s, params=%s", id ? id : "", params ? params : "");

    /*  用 cJSON 解析音量/播放控制 */
    cJSON *obj = cJSON_Parse(params);
    if (obj) {
        cJSON *content = cJSON_GetObjectItem(obj, "content");
        if (content) {
            cJSON *function_name = cJSON_GetObjectItem(content, "function_name");
            const char *func_str = function_name ? cJSON_GetStringValue(function_name) : NULL;

            if (func_str && (!strcmp(func_str, "adjust_volume") || !strcmp(func_str, "play_control"))) {
                cJSON *parameter_list = cJSON_GetObjectItem(content, "parameter_list");
                if (parameter_list && cJSON_IsArray(parameter_list)) {
                    cJSON *mode_obj = cJSON_GetArrayItem(parameter_list, 0);
                    cJSON *mode_item = mode_obj ? cJSON_GetObjectItem(mode_obj, "mode") : NULL;
                    const char *mode_str = mode_item ? cJSON_GetStringValue(mode_item) : NULL;

                    if (mode_str) {
                        if (0 == strcmp(mode_str, "down")) {
                            set_dec_volume_step(-40);
                            app_sdk_audio_play_volume_step(-40);
                        } else if (0 == strcmp(mode_str, "up")) {
                            set_dec_volume_step(40);
                            app_sdk_audio_play_volume_step(40);
                        } else if (0 == strcmp(mode_str, "set")) {
                            cJSON *mode_value = cJSON_GetArrayItem(parameter_list, 1);
                            cJSON *value_item = mode_value ? cJSON_GetObjectItem(mode_value, "value") : NULL;
                            if (value_item && cJSON_IsNumber(value_item)) {
                                int volume = value_item->valueint;
                                app_sdk_audio_play_volume(volume);
                                set_dec_volume(volume);
                            }
                        } else if (0 == strcmp(mode_str, "stop") || 0 == strcmp(mode_str, "pause")) {
                            app_sdk_audio_play_pause(false);
                            sa_audio_play("", 0);
                        }
                    }
                }
                if (!brtc_ctx.normal_quit && brtc_ctx.engine) {
                    baidu_chat_agent_engine_send_text_to_TTS(brtc_ctx.engine, "设置完成");
                }
            } else {
                LOGI(TAG, "FunctionCall function_name: %s", func_str ? func_str : "null");
            }
        }
        cJSON_Delete(obj);
    }

    /* 事件总线分发 */
    brtc_func_call_t fc = {0};
    if (id)     strncpy(fc.id, id, sizeof(fc.id) - 1);
    if (params) strncpy(fc.params, params, sizeof(fc.params) - 1);
    d_event_post(EVT_BRTC_FUNCTION_CALL, &fc, sizeof(fc));

    /* 回复 ok */
    if (!brtc_ctx.normal_quit && brtc_ctx.engine) {
        baidu_chat_agent_engine_send_functioncall_result(brtc_ctx.engine, id, "{\"result\":\"ok\"}");
    }
}

static void on_audio_player_op_cb(const char *path, bool start)
{
    LOGI(TAG, "AudioPlayerOp: %s path=%s", start ? "START" : "STOP", path ? path : "");

    if (start) {
        if (g_brtc_speaking_tts) {
            /* 延迟播放音乐 */
            g_brtc_delay_play_mp3 = true;
            memset(g_brtc_playurl, 0, sizeof(g_brtc_playurl));
            if (path) strncpy(g_brtc_playurl, path, sizeof(g_brtc_playurl) - 1);
            LOGI(TAG, "[delay] mp3_delay_url:%s", path ? path : "");
        } else {
            app_sdk_audio_play_pause(true);
            sa_audio_play(path, start);
        }
    } else {
        app_adk_play_pause(0);
        app_sdk_audio_play_pause(false);
        sa_audio_play(path, start);
    }

    /* 事件总线分发 */
    brtc_player_op_t op = {0};
    if (path) strncpy(op.path, path, sizeof(op.path) - 1);
    op.start = start;
    d_event_post(EVT_BRTC_PLAYER_OP, &op, sizeof(op));
}

/* 订阅端 RTP 就绪回调：此时 voip_play 已创建，音频接收通道已通，可安全发送 TTS */
static void on_play_ready_notify(void)
{
    LOGI(TAG, "Play ready — sending welcome TTS");
    if (brtc_ctx.engine) {
        baidu_chat_agent_engine_send_text_to_TTS(brtc_ctx.engine, "您好，我来了");
    }
}

static void on_media_setup_cb(void)
{
    LOGI(TAG, "onMediaSetup — 媒体就绪");
    brtc_ctx.state = BRTC_STATE_RUNNING;
    brtc_ctx.time_last_asr = timer_get_ms();
    d_event_post(EVT_BRTC_CALL_READY, NULL, 0);
    /* 启用实时 ASR（双保险） */
    if (!brtc_ctx.asr_realtime_enabled && brtc_ctx.engine) {
        LOGI(TAG, "Enable ASR realtime (from MediaSetup)");
        baidu_chat_agent_engine_send_event_to_agent(brtc_ctx.engine, ASR_EVENT_ENABLE_REALTIME);
        brtc_ctx.asr_realtime_enabled = true;
    }
    /* 预热 DAC + 注册就绪回调：订阅端 RTP 就绪后才发送欢迎语，防止音频丢失 */
    app_sdk_audio_play_set_ready_notify(on_play_ready_notify);
    app_sdk_audio_play_prewarm();
}

static void on_audio_data_cb(const uint8_t *data, size_t len) { }
static void on_video_data_cb(const uint8_t *data, size_t len, int w, int h) { }

static void on_license_result_cb(bool result)
{
    LOGI(TAG, "onLicenseResult: %s", result ? "PASS" : "FAIL");
    if (result) {
        d_event_post(EVT_BRTC_LICENSE_OK, NULL, 0);
    } else {
        LOGE(TAG, "onLicenseResult: failed");
        brtc_ctx.license_fail = true;
        d_event_post(EVT_BRTC_LICENSE_FAIL, NULL, 0);
        os_time_dly(10);  /* 短暂等待 */
    }
}

static void on_agent_event_updated_cb(const uint8_t *data, size_t len)
{
    LOGI(TAG, "AgentEventUpdated: %.*s, len:%d", (int)len, data, (int)len);
    /* 检查远端播放事件 */
    if (len == strlen(AGENT_EVENT_REMOTE_PLAYER_BEGIN) &&
        strncmp((const char *)data, AGENT_EVENT_REMOTE_PLAYER_BEGIN, len) == 0) {
        /* 远端音乐开始播放 */
    } else if (len == strlen(AGENT_EVENT_REMOTE_PLAYER_END) &&
               strncmp((const char *)data, AGENT_EVENT_REMOTE_PLAYER_END, len) == 0) {
        /* 远端音乐停止播放 */
    }
}

static const BaiduChatAgentEvent s_brtc_events = {
    .onError                 = on_error_cb,
    .onCallStateChange       = on_call_state_change_cb,
    .onConnectionStateChange = on_connection_state_change_cb,
    .onUserAsrSubtitle       = on_user_asr_subtitle_cb,
    .onAIAgentSubtitle       = on_ai_agent_subtitle_cb,
    .onAIAgentSpeaking       = on_ai_agent_speaking_cb,
    .onFunctionCall          = on_function_call_cb,
    .onAudioPlayerOp         = on_audio_player_op_cb,
    .onMediaSetup            = on_media_setup_cb,
    .onAudioData             = on_audio_data_cb,
    .onVideoData             = on_video_data_cb,
    .onLicenseResult         = on_license_result_cb,
    .onAgentEventUpdated     = on_agent_event_updated_cb,
};

//*********************************************************************************//
//                  HTTP 请求（支持 GET + POST）     //
//*********************************************************************************//

typedef enum {
    BRTC_HTTP_GET,
    BRTC_HTTP_POST,
} brtc_http_method_t;

/**
 * @brief 发送 HTTP 请求（GET 或 POST），返回响应 body 副本（调用者需 free）
 * @param method   BRTC_HTTP_GET 或 BRTC_HTTP_POST
 * @param url      请求 URL
 * @param json_body POST 时的 body（GET 时传 NULL）
 * @return 响应字符串指针, NULL=失败
 */
static char *brtc_http_request(brtc_http_method_t method, const char *url, const char *json_body)
{
    LOGI(TAG, "HTTP %s %s", method == BRTC_HTTP_POST ? "POST" : "GET", url);
    if (json_body) LOGD(TAG, "  Body: %s", json_body);

    httpcli_ctx *ctx = (httpcli_ctx *)calloc(1, sizeof(httpcli_ctx));
    if (!ctx) return NULL;

    http_body_obj body_buf = {0};
    body_buf.buf_len   = BRTC_HTTP_BUF_SIZE;
    body_buf.buf_count = 1;
    body_buf.recv_len  = 0;
    body_buf.p         = (char *)malloc(BRTC_HTTP_BUF_SIZE);
    if (!body_buf.p) {
        free(ctx);
        return NULL;
    }

    ctx->url             = url;
    ctx->timeout_millsec = D_BRTC_HTTP_TIMEOUT_MS;
    ctx->connection      = "close";
    ctx->priv            = &body_buf;

    httpin_error ret;
    if (method == BRTC_HTTP_POST && json_body) {
        ctx->data_format = "application/json";
        ctx->post_data   = json_body;
        ctx->data_len    = strlen(json_body);
        ret = httpcli_post(ctx);
    } else {
        ret = httpcli_get(ctx);
    }

    char *result = NULL;
    if (ret == HERROR_OK && body_buf.recv_len > 0) {
        result = (char *)malloc(body_buf.recv_len + 1);
        if (result) {
            memcpy(result, body_buf.p, body_buf.recv_len);
            result[body_buf.recv_len] = '\0';
        }
        LOGI(TAG, "HTTP 响应(%d bytes): %.200s", body_buf.recv_len, body_buf.p);
    } else {
        LOGE(TAG, "HTTP 失败 ret=%d recv_len=%d", ret, body_buf.recv_len);
    }

    httpcli_close(ctx);
    if (body_buf.p) free(body_buf.p);
    free(ctx);

    return result;
}

/* 便捷包装 */
static inline char *brtc_http_post(const char *url, const char *json_body)
{
    return brtc_http_request(BRTC_HTTP_POST, url, json_body);
}

static inline char *brtc_http_get(const char *url)
{
    return brtc_http_request(BRTC_HTTP_GET, url, NULL);
}

/**
 * @brief 停止 AI Agent 实例
 */
static void brtc_stop_agent_instance(const char *json_resp)
{
    if (!json_resp) return;

    cJSON *json = cJSON_Parse(json_resp);
    if (!json) return;

    cJSON *item = cJSON_GetObjectItem(json, "ai_agent_instance_id");
    if (item) {
        char body[256];
        if (cJSON_IsNumber(item)) {
            snprintf(body, sizeof(body),
                     "{\"app_id\":\"%s\",\"ai_agent_instance_id\":\"%.0f\"}",
                     D_BRTC_APP_ID, item->valuedouble);
        } else if (cJSON_IsString(item)) {
            snprintf(body, sizeof(body),
                     "{\"app_id\":\"%s\",\"ai_agent_instance_id\":\"%s\"}",
                     D_BRTC_APP_ID, item->valuestring);
        } else {
            cJSON_Delete(json);
            return;
        }
        LOGI(TAG, "停止 Agent 实例...");
        char *resp = brtc_http_post(BRTC_URL_STOP, body);
        if (resp) free(resp);
    }
    cJSON_Delete(json);
}

//*********************************************************************************//
//                  填充引擎默认参数                //
//*********************************************************************************//

static void brtc_set_user_params(AgentEngineParams *params)
{
    snprintf(params->agent_platform_url, sizeof(params->agent_platform_url),
             "%s", D_BRTC_USERSERVER_URL);
    snprintf(params->appid,    sizeof(params->appid),    "%s", D_BRTC_APP_ID);
    snprintf(params->userId,   sizeof(params->userId),   "%s", D_BRTC_USER_ID);
    snprintf(params->workflow, sizeof(params->workflow),  "%s", D_BRTC_WORKFLOW);
    snprintf(params->license_key, sizeof(params->license_key), "%s", D_BRTC_LICENSE_KEY);

    snprintf(params->cer, sizeof(params->cer), "%s", "./a.cer");
    params->instance_id            = 10373;
    params->verbose                = false;
    params->enable_internal_device = true;
    params->enable_local_agent     = true;
    params->enable_voice_interrupt = true;
    params->level_voice_interrupt  = 80;
    strncpy(params->llm,  D_BRTC_LLM,  sizeof(params->llm) - 1);
    strncpy(params->lang, D_BRTC_LANG, sizeof(params->lang) - 1);
    params->AudioInChannel   = 1;
    params->AudioInFrequency = 16000;
    //params->enable_aec_inner = true;
    params->region = REGION_MAINLAND;   //默认是中国大陆
}

//*********************************************************************************//
//            引擎创建 + 初始化 + 呼叫任务                                          //
//*********************************************************************************//

static void brtc_engine_open_task(void *arg)
{
    struct AgentEngineParams *pArg = (struct AgentEngineParams *)arg;

    BaiduChatAgentEngine *engine = baidu_create_chat_agent_engine(&s_brtc_events);
    if (!engine) {
        LOGE(TAG, "create_engine 失败");
        if (pArg) free(pArg);
        brtc_ctx.state = BRTC_STATE_IDLE;
        d_event_post(EVT_BRTC_ERROR, NULL, 0);
        return;
    }
    brtc_ctx.engine = engine;

    AgentEngineParams agentParams;
    memset(&agentParams, 0, sizeof(agentParams));
    brtc_set_user_params(&agentParams);

    /* 覆盖远程参数 */
    if (pArg) {
        agentParams.enable_local_agent = pArg->enable_local_agent;
        snprintf(agentParams.appid, sizeof(agentParams.appid), "%s", pArg->appid);
        snprintf(agentParams.remote_params, sizeof(agentParams.remote_params),
                 "%s", pArg->remote_params);
        free(pArg);
    }

    LOGI(TAG, "engine_init (local=%d)...", agentParams.enable_local_agent);
    brtc_ctx.state = BRTC_STATE_ENGINE_INIT;

    int result = baidu_chat_agent_engine_init(engine, &agentParams);
    if (result != 200) {
        LOGE(TAG, "engine_init 失败: %d", result);
        baidu_chat_agent_engine_destroy(engine);
        brtc_ctx.engine = NULL;
        brtc_ctx.state = BRTC_STATE_IDLE;
        d_event_post(EVT_BRTC_LOGIN_FAIL, NULL, 0);
        return;
    }

    LOGI(TAG, "engine_call...");
    brtc_ctx.state = BRTC_STATE_CALLING;
    baidu_chat_agent_engine_call(engine);
}

//*********************************************************************************//
//              超时监控任务                                                        //
//*********************************************************************************//

static void brtc_timer_task(void *priv)
{
    LOGI(TAG, "BRTC 监控任务启动");
    g_brtc_agent_can_exit = true;   /* 标识可正常退出 */

    while (1) {
        u32 cur_time = timer_get_ms();

        /* 异常退出 / 网络断开 / 鉴权失败 / 空闲超时 */
        if (brtc_ctx.abnormal_quit || brtc_ctx.license_fail ||
            (!brtc_ctx.wifi_connected && brtc_ctx.state >= BRTC_STATE_CALLING) ||
            (brtc_ctx.time_last_asr != 0 &&
             cur_time > brtc_ctx.time_last_asr + D_BRTC_IDLE_TIMEOUT_MS &&
             sa_audio_is_net_music() == 0 &&
             brtc_ctx.state == BRTC_STATE_RUNNING))
        {
            LOGW(TAG, "监控触发退出 (abnormal=%d license=%d wifi=%d)",
                 brtc_ctx.abnormal_quit, brtc_ctx.license_fail, brtc_ctx.wifi_connected);

            mute_record(false);  /* 停止发送录音数据 */
            brtc_ctx.normal_quit = true;

            /* license 失败时等 4 秒 */
            if (brtc_ctx.license_fail) {
                os_time_dly(400);  /* 400 * 10ms = 4s */
                LOGE(TAG, "brtc license confirm failed ...");
            } else {
                LOGI(TAG, "ASR no ai response over 5 min ...");
            }

            /* 告别语 / 提示音 */
            if (brtc_ctx.engine && brtc_ctx.wifi_connected && !brtc_ctx.license_fail) {
                baidu_chat_agent_engine_send_text_to_TTS(brtc_ctx.engine, "我退下了");
                os_time_dly(1000);
            } else {
                /* 网络断开提示音 */
                if (!g_brtc_agent_exit_quickly && !brtc_ctx.wifi_connected) {
                    os_time_dly(300);
                    play_mix_file(AGENT_QUITE_NOTICE);
                }
                /* 异常退出提示音 */
                if (brtc_ctx.abnormal_quit) {
                    play_mix_file(AGENT_QUITE_NOTICE);
                }
            }

            brtc_ctx.time_last_asr = 0;

            /* 销毁引擎 */
            if (brtc_ctx.engine) {
                LOGI(TAG, "销毁引擎...");
                baidu_chat_agent_engine_destroy(brtc_ctx.engine);
                brtc_ctx.engine = NULL;
                LOGI(TAG, "引擎已销毁");
            }
            break;
        }

        /* 外部请求终止 (kill_time_task_req) */
        if (g_kill_time_task_req) {
            if (brtc_ctx.engine) {
                LOGI(TAG, "外部请求终止，销毁引擎...");
                baidu_chat_agent_engine_destroy(brtc_ctx.engine);
                brtc_ctx.engine = NULL;
            }
            os_time_dly(10);
            break;
        }

        /* 延迟播放 MP3（等待 TTS 结束后播放） */
        if (g_brtc_delay_play_mp3 && !g_brtc_speaking_tts) {
            app_sdk_audio_play_pause(true);
            g_brtc_delay_play_mp3 = false;
            sa_audio_play(g_brtc_playurl, 1);
            LOGI(TAG, "[mp3] real play: %s", g_brtc_playurl);
        }

        os_time_dly(50);
    }

    /* 通知服务端停止 agent 实例 */
    if (brtc_ctx.json_str) {
        brtc_stop_agent_instance(brtc_ctx.json_str);
        free(brtc_ctx.json_str);
        brtc_ctx.json_str = NULL;
    }

    g_brtc_agent_can_exit = false;  
    brtc_ctx.state = BRTC_STATE_IDLE;
    brtc_ctx.engine_task_pid = 0;
    brtc_ctx.timer_task_pid  = 0;
    LOGI(TAG, "BRTC 监控任务结束");
}

//*********************************************************************************//
//       主任务：HTTP 换 Token + fork 引擎任务 + 监控任务                           //
//*********************************************************************************//

static void brtc_agent_task(void *priv)
{
    LOGI(TAG, "=== BRTC 启动流程 ===");
    brtc_ctx.state = BRTC_STATE_HTTP_REQ;

    /* 1. HTTP POST 换取 token */
    char *response = brtc_http_post(BRTC_URL_GENERATE, BRTC_POST_DATA);
    if (!response) {
        LOGE(TAG, "HTTP 请求失败");
        /* 播放提示音 */
        play_mix_file(AGENT_QUITE_NOTICE);
        brtc_ctx.state = BRTC_STATE_IDLE;
        d_event_post(EVT_BRTC_ERROR, NULL, 0);
        return;
    }

    LOGI(TAG, "HTTP 响应: %s", response);

    /* 2. 保存原始 JSON（后续 stop 时使用） */
    brtc_ctx.json_str = malloc(strlen(response) + 1);
    if (brtc_ctx.json_str) {
        strcpy(brtc_ctx.json_str, response);
    }

    /* 3. 构造引擎参数
     *    - enable_local_agent = false
     *    - remote_params = 完整 HTTP 响应（SDK 内部解析）
     *    - appid = 我们的 app_id
     */
    struct AgentEngineParams *pParams =
        (struct AgentEngineParams *)calloc(1, sizeof(struct AgentEngineParams));
    if (!pParams) {
        LOGE(TAG, "AgentEngineParams malloc 失败");
        free(response);
        if (brtc_ctx.json_str) { free(brtc_ctx.json_str); brtc_ctx.json_str = NULL; }
        brtc_ctx.state = BRTC_STATE_IDLE;
        return;
    }

    snprintf(pParams->remote_params, sizeof(pParams->remote_params), "%s", response);
    snprintf(pParams->appid, sizeof(pParams->appid), "%s", D_BRTC_APP_ID);
    pParams->enable_local_agent = false;

    free(response);

    /* 4. 初始化运行状态 */
    brtc_ctx.time_last_asr         = timer_get_ms();
    brtc_ctx.normal_quit           = false;
    brtc_ctx.abnormal_quit         = false;
    brtc_ctx.abnormal_quit_first   = true;
    brtc_ctx.license_fail          = false;
    brtc_ctx.asr_realtime_enabled  = false;
    g_brtc_agent_exit_quickly      = false;
    g_kill_time_task_req           = 0;
    g_brtc_delay_play_mp3          = false;
    g_brtc_speaking_tts            = false;
    g_audio_device_multi_used      = false;
    g_brtc_agent_can_exit          = false;

    /* 5. 创建引擎任务（ 96K 栈 + 128 队列） */
    int ret = thread_fork("brtc_engine_open", 21, 1024 * 96, 128,
                          &brtc_ctx.engine_task_pid,
                          brtc_engine_open_task, pParams);
    LOGI(TAG, "engine task fork ret=%d pid=%d", ret, brtc_ctx.engine_task_pid);
    if (ret != OS_NO_ERR) {
        free(pParams);
        if (brtc_ctx.json_str) {
            free(brtc_ctx.json_str);
            brtc_ctx.json_str = NULL;
        }
        brtc_ctx.state = BRTC_STATE_IDLE;
        d_event_post(EVT_BRTC_ERROR, NULL, 0);
        return;
    }

    /* 6. 创建监控任务 */
    thread_fork("brtc_timer", 10, 1024 * 96, 0,
                &brtc_ctx.timer_task_pid,
                brtc_timer_task, NULL);
    LOGI(TAG, "timer task pid=%d", brtc_ctx.timer_task_pid);
}

//*********************************************************************************//
//                              WiFi 事件订阅回调                                  //
//*********************************************************************************//

static void brtc_on_wifi_event(const d_event_t *event, void *user_data)
{
    if (event->id == EVT_WIFI_CONNECTED) {
        LOGI(TAG, "WiFi 已连接，自动启动 BRTC");
        brtc_ctx.wifi_connected = true;
        brtc_service_start();
    } else if (event->id == EVT_WIFI_DISCONNECTED) {
        LOGW(TAG, "WiFi 断开");
        brtc_ctx.wifi_connected = false;
        if (brtc_ctx.state >= BRTC_STATE_CALLING) {
            brtc_ctx.abnormal_quit = true;
        }
    }
}

//*********************************************************************************//
//                              公开 API                                            //
//*********************************************************************************//

d_err_t brtc_service_init(void)
{
    memset(&brtc_ctx, 0, sizeof(brtc_ctx));
    if (d_event_subscribe(EVT_WIFI_CONNECTED, brtc_on_wifi_event, NULL) != 0) {
        LOGE(TAG, "EVT_WIFI_CONNECTED 订阅失败");
        return D_ERR_IO;
    }
    if (d_event_subscribe(EVT_WIFI_DISCONNECTED, brtc_on_wifi_event, NULL) != 0) {
        d_event_unsubscribe(EVT_WIFI_CONNECTED, brtc_on_wifi_event);
        LOGE(TAG, "EVT_WIFI_DISCONNECTED 订阅失败");
        return D_ERR_IO;
    }
    LOGI(TAG, "BRTC 服务初始化完成（等待 WiFi 连接）");
    return D_OK;
}

d_err_t brtc_service_start(void)
{
    int ret;

    if (brtc_ctx.state != BRTC_STATE_IDLE) {
        LOGW(TAG, "BRTC 已在运行 state=%d", brtc_ctx.state);
        return D_ERR_BUSY;
    }

    brtc_ctx.state = BRTC_STATE_HTTP_REQ;

    /* 主任务栈 10K 足够（内部再 fork 引擎和监控子任务） */
    ret = thread_fork("brtc_agent", 10, 1024 * 10, 0, NULL,
                      brtc_agent_task, NULL);
    if (ret != OS_NO_ERR) {
        brtc_ctx.state = BRTC_STATE_IDLE;
        LOGE(TAG, "thread_fork 失败: %d", ret);
        return D_ERR_IO;
    }
    return D_OK;
}

d_err_t brtc_service_stop(void)
{
    if (brtc_ctx.state == BRTC_STATE_IDLE) {
        return D_OK;
    }
    brtc_ctx.normal_quit = true;
    brtc_ctx.abnormal_quit = true;
    g_kill_time_task_req = 1;  /* 外部终止请求 */
    LOGI(TAG, "已请求停止 BRTC");
    return D_OK;
}

void brtc_service_send_text(const char *text)
{
    if (brtc_ctx.engine && brtc_ctx.state == BRTC_STATE_RUNNING) {
        baidu_chat_agent_engine_send_text(brtc_ctx.engine, text);
    }
}

void brtc_service_send_tts(const char *text)
{
    if (brtc_ctx.engine && brtc_ctx.state == BRTC_STATE_RUNNING) {
        baidu_chat_agent_engine_send_text_to_TTS(brtc_ctx.engine, text);
    }
}

void brtc_service_interrupt(void)
{
    if (brtc_ctx.engine && brtc_ctx.state == BRTC_STATE_RUNNING) {
        baidu_chat_agent_engine_interrupt(brtc_ctx.engine);
    }
}

bool brtc_service_is_running(void)
{
    return brtc_ctx.state >= BRTC_STATE_CALLING;
}

bool app_brtc_agent_can_exit(void)
{
    return g_brtc_agent_can_exit;
}

bool brtc_aiagent_tts_busy(void)
{
    return g_brtc_speaking_tts;
}

void brtc_audio_device_need_used_set(bool used)
{
    g_audio_device_multi_used = used;
}

bool brtc_audio_device_need_used_get(void)
{
    return g_audio_device_multi_used;
}

void app_sdk_should_close(int quickly)
{
    g_brtc_agent_exit_quickly = (quickly != 0);
    brtc_ctx.wifi_connected = false;
    brtc_ctx.abnormal_quit = true;
    /* MP3 播放需要停止 */
    sa_audio_play("", 0);
}

void app_brtc_agent_longtext_mode(bool enable)
{
    if (!brtc_ctx.engine) return;
    baidu_chat_agent_engine_send_event_to_agent(brtc_ctx.engine, ASR_EVENT_DISABLE_REALTIME);
    if (enable) {
        os_time_dly(20);
        baidu_chat_agent_engine_send_event_to_agent(brtc_ctx.engine, ASR_EVENT_START_LONGTEXT_REC);
    } else {
        baidu_chat_agent_engine_send_event_to_agent(brtc_ctx.engine, ASR_EVENT_STOP_LONGTEXT_REC);
    }
}

void app_brtc_agent_asr_realtime(bool enable)
{
    if (!brtc_ctx.engine) return;
    if (enable) {
        baidu_chat_agent_engine_send_event_to_agent(brtc_ctx.engine, ASR_EVENT_ENABLE_REALTIME);
    } else {
        baidu_chat_agent_engine_send_event_to_agent(brtc_ctx.engine, ASR_EVENT_DISABLE_REALTIME);
    }
}

#endif /* D_FEATURE_BRTC */
