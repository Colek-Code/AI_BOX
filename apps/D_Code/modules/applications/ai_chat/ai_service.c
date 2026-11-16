/**
 * @file    ai_service.c
 * @brief   AI 大模型服务
 *
 * ❌ 禁止：#include "lvgl.h"
 */

#include "config/app_config.h"

#if D_FEATURE_AI_CHAT

#include "ai_service.h"
#include "system/includes.h"
#include "wifi/wifi_connect.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "net/http/http_cli.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "framework/d_event_payloads.h"
#include <string.h>

/* ❌ 禁止：#include "lvgl.h" */
/* ❌ 禁止：#include "d_face_engine.h" */

#define TAG "AI_SVC"

//*********************************************************************************//
//                              配置（注入）                                        //
//*********************************************************************************//

static ai_service_config_t s_config;
static volatile int s_network_ready = 0;
static volatile int s_busy = 0;

//*********************************************************************************//
//                              缓冲区                                              //
//*********************************************************************************//

#define HTTP_BUF_SIZE       2048
#define RESPONSE_BUF_SIZE   4096
#define AI_REPLY_SIZE       2048

static char s_http_response[RESPONSE_BUF_SIZE];
static char *s_recv_buf = NULL;
static int   s_recv_len = 0;

/* 系统提示词 */
static const char AI_SYS_PROMPT[] =
    "\xe4\xbd\xa0\xe6\x98\xaf\xe5\xb5\x8c\xe5\x85\xa5\xe5\xbc\x8f\xe8\xae\xbe\xe5\xa4\x87\xe4\xb8\x8a\xe7\x9a\x84\xe6\x99\xba\xe8\x83\xbd\xe5\x8a\xa9\xe6\x89\x8b\xe3\x80\x82"
    "\xe8\xaf\xb7\xe7\x94\xa8\xe7\xae\x80\xe7\x9f\xad\xe4\xb8\xad\xe6\x96\x87\xe5\x9b\x9e\xe7\xad\x94\xef\xbc\x8c\xe4\xb8\x8d\xe8\xb6\x85\xe8\xbf\x87" "50\xe5\xad\x97\xe3\x80\x82";

/* 情绪分类提示词 */
static const char AI_EMO_CLASSIFY_PROMPT[] =
    "Classify the dialogue emotion. Output ONLY one digit 0-7."
    " 0=neutral 1=angry 2=sad 3=happy 4=sleepy 5=doubtful 6=cold 7=hot";

//*********************************************************************************//
//                              JSON 构建/解析（保留原有逻辑）                      //
//*********************************************************************************//

/**
 * @brief 在输出容量内转义引号、反斜杠及常见换行控制字节。
 * @param[in] src 非空 NUL 结尾输入。
 * @param[out] dst 调用方提供的非空输出缓冲。
 * @param[in] dst_size 至少两字节的输出总容量。
 * @return 实际输出字节数，不含 NUL；到容量边界时截断。
 * @note 不验证 UTF-8 或完整 JSON 字符集；调用方使用既有有限请求文本。
 */
static int json_escape_string(const char *src, char *dst, int dst_size)
{
    int i = 0, j = 0;
    while (src[i] && j < dst_size - 2) {
        switch (src[i]) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:   dst[j++] = src[i]; break;
        }
        i++;
    }
    dst[j] = '\0';
    return j;
}

/**
 * @brief 以首个匹配键和冒号查找带引号文字，处理有限转义并拷贝。
 * @param[in] json 非空 NUL 结尾响应文字。
 * @param[in] key 非空待查字段名。
 * @param[out] value 非空输出缓冲。
 * @param[in] value_size 至少一字节输出容量。
 * @return 找到后返回拷贝字节数；找不到键或值开头不是引号返回 -1。
 * @note 这是简单字符串提取，不校验完整 JSON 或嵌套语义；容量不足会截断仍返回已拷贝长度。
 */
static int find_json_string(const char *json, const char *key, char *value, int value_size)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    const char *key_pos = strstr(json, search_key);
    if (!key_pos) return -1;
    const char *colon = strchr(key_pos, ':');
    if (!colon) return -1;
    colon++;
    while (*colon == ' ' || *colon == '\t' || *colon == '\n' || *colon == '\r') colon++;
    if (*colon != '"') return -1;
    colon++;
    int i = 0;
    while (*colon && *colon != '"' && i < value_size - 1) {
        if (*colon == '\\' && *(colon + 1)) {
            colon++;
            switch (*colon) {
            case 'n':  value[i++] = '\n'; break;
            case 'r':  value[i++] = '\r'; break;
            case 't':  value[i++] = '\t'; break;
            case '"':  value[i++] = '"';  break;
            case '\\': value[i++] = '\\'; break;
            default:   value[i++] = *colon; break;
            }
        } else {
            value[i++] = *colon;
        }
        colon++;
    }
    value[i] = '\0';
    return i;
}

/**
 * @brief 按所选旧 AI 平台组装系统提示及用户消息请求体。
 * @param[in] user_msg 非空借用用户文字，先拷贝到有限转义缓冲。
 * @param[out] json_buf 请求体输出缓冲。
 * @param[in] buf_size 请求体总容量。
 * @return 沿用 snprintf：返回完整目标长度，可能达到或超过 buf_size 表示截断。
 */
static int build_request_json(const char *user_msg, char *json_buf, int buf_size)
{
    char escaped_msg[512];
    json_escape_string(user_msg, escaped_msg, sizeof(escaped_msg));

#if (D_AI_PLATFORM == 1)
    return snprintf(json_buf, buf_size,
        "{\"model\":\"%s\",\"input\":{\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}"
        "]}}",
        s_config.model, AI_SYS_PROMPT, escaped_msg);
#else
    return snprintf(json_buf, buf_size,
        "{\"model\":\"%s\",\"instructions\":\"%s\","
        "\"max_output_tokens\":500,"
        "\"input\":[{\"type\":\"message\",\"role\":\"user\",\"content\":\"%s\"}]}",
        s_config.model, AI_SYS_PROMPT, escaped_msg);
#endif
}

/**
 * @brief 按平台兼容顺序提取 text／content 字段。
 * @param[in] json 非空响应文本。
 * @param[out] reply 非空回复缓冲。
 * @param[in] reply_size 回复总容量。
 * @return 返回实际提取字节数或 -1 未找到；沿用简单提取器的截断语义。
 */
static int parse_ai_response(const char *json, char *reply, int reply_size)
{
#if (D_AI_PLATFORM == 1)
    return find_json_string(json, "text", reply, reply_size);
#else
    const char *output_text_pos = strstr(json, "\"output_text\"");
    if (output_text_pos) {
        return find_json_string(output_text_pos, "text", reply, reply_size);
    }
    int ret = find_json_string(json, "content", reply, reply_size);
    if (ret <= 0) ret = find_json_string(json, "text", reply, reply_size);
    return ret;
#endif
}

//*********************************************************************************//
//                              情绪识别（保留原有逻辑）                            //
//*********************************************************************************//

/* 关键词→情绪 */
static const struct { const char *kw; d_emotion_t emo; } s_keyword_emotions[] = {
    { "\xe4\xb8\x8d\xe5\xbc\x80\xe5\xbf\x83", EMOTION_SAD },
    { "\xe4\xb8\x8d\xe9\xab\x98\xe5\x85\xb4", EMOTION_ANGRY },
    { "\xe5\x93\x88\xe5\x93\x88\xe5\x93\x88", EMOTION_HAPPY },
    { "\xe5\xbc\x80\xe5\xbf\x83",             EMOTION_HAPPY },
    { "\xe5\xbf\xab\xe4\xb9\x90",             EMOTION_HAPPY },
    { "\xe5\x93\x88\xe5\x93\x88",             EMOTION_HAPPY },
    { "\xe9\x9a\xbe\xe8\xbf\x87",             EMOTION_SAD },
    { "\xe4\xbc\xa4\xe5\xbf\x83",             EMOTION_SAD },
    { "\xe7\x94\x9f\xe6\xb0\x94",             EMOTION_ANGRY },
    { "\xe6\x84\xa4\xe6\x80\x92",             EMOTION_ANGRY },
    { "\xe5\xa5\xbd\xe5\x9b\xb0",             EMOTION_SLEEPY },
    { "\xe5\xa5\xbd\xe7\xb4\xaf",             EMOTION_SLEEPY },
    { "\xe6\x99\x9a\xe5\xae\x89",             EMOTION_SLEEPY },
    { "\xe5\x8f\xaf\xe8\x83\xbd",             EMOTION_DOUBTFUL },
    { "\xe4\xb9\x9f\xe8\xae\xb8",             EMOTION_DOUBTFUL },
    { "\xe5\xa5\xbd\xe5\x86\xb7",             EMOTION_COLD },
    { "\xe5\xa5\xbd\xe7\x83\xad",             EMOTION_HOT },
};
#define KEYWORD_EMO_COUNT  (sizeof(s_keyword_emotions) / sizeof(s_keyword_emotions[0]))

/**
 * @brief 按静态关键词顺序选择首个匹配情绪。
 * @param[in] text 非空 NUL 结尾候选文字。
 * @return 匹配情绪，无匹配为 NEUTRAL。
 */
static d_emotion_t ai_keyword_emotion(const char *text)
{
    int i;
    for (i = 0; i < KEYWORD_EMO_COUNT; i++) {
        if (strstr(text, s_keyword_emotions[i].kw)) return s_keyword_emotions[i].emo;
    }
    return EMOTION_NEUTRAL;
}

//*********************************************************************************//
//                              HTTPS Keep-Alive（保留原有逻辑）                    //
//*********************************************************************************//

static httpcli_ctx s_http_ctx;
static char s_full_url[256];
static char s_custom_header[512];

/**
 * @brief 把 SDK 响应分块追加到当前静态接收上下文，错误或中止拒绝继续。
 * @param[in] ctx SDK 回调上下文，当前未使用。
 * @param[in] buf 本次回调借用的响应块。
 * @param[in] size 分块字节数。
 * @param[in] priv SDK 私有参数，当前未使用。
 * @param[in] status SDK HTTP 接收阶段。
 * @return HTTPIN_ERROR／ABORT 返回 -1，其余状态返回 0；超过固定容量的块不复制。
 * @note 接收边界使用 RESPONSE_BUF_SIZE，不使用 https_post_request 的 response_size；仅当前串行请求使用。
 */
static int http_response_cb(void *ctx, void *buf, unsigned int size, void *priv, httpin_status status)
{
    switch (status) {
    case HTTPIN_HEADER:  s_recv_len = 0; break;
    case HTTPIN_PROGRESS:
        if (s_recv_buf && s_recv_len + size < RESPONSE_BUF_SIZE - 1) {
            memcpy(s_recv_buf + s_recv_len, buf, size);
            s_recv_len += size;
            s_recv_buf[s_recv_len] = '\0';
        }
        break;
    case HTTPIN_FINISHED: break;
    case HTTPIN_ERROR:
    case HTTPIN_ABORT:    return -1;
    default: break;
    }
    return 0;
}

/**
 * @brief 存在 SDK socket 句柄时关闭当前静态 HTTP 连接。
 */
static void https_keepalive_close(void)
{
    if (s_http_ctx.sock_hdl) {
        httpcli_close(&s_http_ctx);
    }
}

/**
 * @brief 已有 socket 时复用，否则按借用配置建立静态 HTTPS Keep-Alive 上下文。
 * @return 已有连接或创建成功为 0，创建失败清空上下文并返回 -1。
 */
static int https_keepalive_ensure(void)
{
    if (s_http_ctx.sock_hdl != NULL) return 0;

    LOGD(TAG, "建立 Keep-Alive 连接...");
    memset(&s_http_ctx, 0, sizeof(s_http_ctx));
    snprintf(s_full_url, sizeof(s_full_url), "https://%s%s", s_config.api_host, s_config.api_path);
    s_http_ctx.url = s_full_url;
    s_http_ctx.timeout_millsec = s_config.timeout_ms;
    s_http_ctx.cb = http_response_cb;

    httpin_error err = httpcli_post_keepalive_init(&s_http_ctx);
    if (err != HERROR_OK) {
        LOGE(TAG, "Keep-Alive 初始化失败: %d", err);
        memset(&s_http_ctx, 0, sizeof(s_http_ctx));
        return -1;
    }
    return 0;
}

/**
 * @brief 设置授权头与接收缓冲并同步发送，发送失败时关闭重连再试一次。
 * @param[in] body 调用期间借用的 NUL 结尾 JSON 请求体。
 * @param[out] response 非空接收缓冲，当前实现要求至少 RESPONSE_BUF_SIZE 字节。
 * @param[in] response_size 用于清零的字节数；回调容量仍固定为 RESPONSE_BUF_SIZE。
 * @return 成功返回回调累计的响应字节数，连接或两次发送失败返回 -1。
 * @note 共用静态 HTTP／接收状态，未提供并发安全；max_retries 配置不控制此处固定的一次重连重试。
 */
static int https_post_request(const char *body, char *response, int response_size)
{
    int body_len = strlen(body);

    snprintf(s_custom_header, sizeof(s_custom_header),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Authorization: Bearer %s\r\n"
             "Accept: application/json\r\n"
             "Connection: keep-alive\r\n\r\n",
             s_config.api_path, s_config.api_host, body_len, s_config.api_key);

    s_recv_buf = response;
    s_recv_len = 0;
    memset(response, 0, response_size);

    if (https_keepalive_ensure() != 0) return -1;

    s_http_ctx.user_http_header = s_custom_header;
    s_http_ctx.post_data = body;
    s_http_ctx.data_len = body_len;
    s_http_ctx.timeout_millsec = s_config.timeout_ms;
    s_http_ctx.cb = http_response_cb;
    s_http_ctx.priv = response;

    httpin_error ret = httpcli_post_keepalive_send(&s_http_ctx);
    if (ret != HERROR_OK) {
        LOGW(TAG, "Keep-Alive 发送失败(%d), 重连...", ret);
        https_keepalive_close();
        if (https_keepalive_ensure() != 0) return -1;

        s_recv_buf = response;
        s_recv_len = 0;
        memset(response, 0, response_size);
        s_http_ctx.user_http_header = s_custom_header;
        s_http_ctx.post_data = body;
        s_http_ctx.data_len = body_len;
        s_http_ctx.timeout_millsec = s_config.timeout_ms;
        s_http_ctx.cb = http_response_cb;
        s_http_ctx.priv = response;

        ret = httpcli_post_keepalive_send(&s_http_ctx);
        if (ret != HERROR_OK) {
            LOGE(TAG, "重试失败(%d), 放弃", ret);
            https_keepalive_close();
            return -1;
        }
    }
    return s_recv_len;
}

//*********************************************************************************//
//                              核心 AI 对话                                        //
//*********************************************************************************//

/**
 * @brief 网络就绪时同步请求旧 AI 平台并提取非空回复。
 * @param[in] user_message 借用用户消息。
 * @param[out] reply 调用方回复缓冲。
 * @param[in] reply_size 回复总容量。
 * @return 有有效回复为 0，未就绪、HTTP 或解析失败为 -1。
 */
static int ai_chat(const char *user_message, char *reply, int reply_size)
{
    char json_body[1024];
    int ret;

    if (!s_network_ready) {
        LOGW(TAG, "网络未就绪");
        return -1;
    }

    LOGI(TAG, "用户: %s", user_message);
    build_request_json(user_message, json_body, sizeof(json_body));

    memset(s_http_response, 0, sizeof(s_http_response));
    ret = https_post_request(json_body, s_http_response, sizeof(s_http_response));
    if (ret <= 0) {
        LOGE(TAG, "HTTP 请求失败");
        return -1;
    }

    ret = parse_ai_response(s_http_response, reply, reply_size);
    if (ret <= 0) {
        LOGE(TAG, "解析响应失败");
        return -1;
    }

    LOGI(TAG, "AI 回复: %s", reply);
    return 0;
}

/**
 * @brief 用有限第二次请求分类对话情绪，取回复中首个零至七数字。
 * @param[in] user_msg 借用用户消息。
 * @param[in] ai_reply 借用 AI 回复。
 * @return 匹配情绪，构建、网络或解析失败为 NEUTRAL。
 * @note 使用静态构建缓冲，由单一后台任务串行调用。
 */
static d_emotion_t ai_classify_emotion_via_llm(const char *user_msg, const char *ai_reply)
{
    static char cls_input[384];
    static char cls_escaped[512];
    static char cls_json[1024];
    char result[32];
    int ret;

    snprintf(cls_input, sizeof(cls_input), "User:%s Assistant:%s", user_msg, ai_reply);
    json_escape_string(cls_input, cls_escaped, sizeof(cls_escaped));

#if (D_AI_PLATFORM == 1)
    ret = snprintf(cls_json, sizeof(cls_json),
        "{\"model\":\"%s\",\"input\":{\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"},"
        "{\"role\":\"user\",\"content\":\"%s\"}"
        "]},\"parameters\":{\"max_tokens\":3}}",
        s_config.model, AI_EMO_CLASSIFY_PROMPT, cls_escaped);
#else
    ret = snprintf(cls_json, sizeof(cls_json),
        "{\"model\":\"%s\",\"instructions\":\"%s\","
        "\"max_output_tokens\":3,\"thinking\":{\"type\":\"disabled\"},"
        "\"input\":[{\"type\":\"message\",\"role\":\"user\",\"content\":\"%s\"}]}",
        s_config.model, AI_EMO_CLASSIFY_PROMPT, cls_escaped);
#endif

    if (ret <= 0 || ret >= (int)sizeof(cls_json)) return EMOTION_NEUTRAL;

    memset(s_http_response, 0, sizeof(s_http_response));
    ret = https_post_request(cls_json, s_http_response, sizeof(s_http_response));
    if (ret <= 0) return EMOTION_NEUTRAL;

    memset(result, 0, sizeof(result));
    ret = parse_ai_response(s_http_response, result, sizeof(result));
    if (ret <= 0) return EMOTION_NEUTRAL;

    const char *p = result;
    while (*p) {
        if (*p >= '0' && *p <= '7') {
            d_emotion_t emo = (d_emotion_t)(*p - '0');
            LOGI(TAG, "LLM 情绪分类: %d", emo);
            return emo;
        }
        p++;
    }
    return EMOTION_NEUTRAL;
}

//*********************************************************************************//
//                              事件驱动接口                                        //
//*********************************************************************************//

/* 请求消息缓冲（任务间通信）— 必须在 on_ai_request 前声明 */
static volatile int s_request_pending = 0;
static char s_request_msg[128];

/**
 * @brief 在不忙时有界复制请求文字并置待处理／忙标志，忙时发布 BUSY。
 * @param[in] evt 事件总线借用的请求事件，调用链保证非空。
 * @param[in] ctx 订阅上下文，当前未使用。
 * @note 回调不执行 HTTP；当前标志为 volatile，未在此提供多生产者原子准入保证。
 */
static void on_ai_request(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_AI_REQUEST) return;
    const ai_request_t *req = (const ai_request_t *)evt->data;
    if (!req || !req->msg) return;

    if (s_busy) {
        LOGW(TAG, "AI 正忙，丢弃请求");
        d_event_publish(EVT_AI_BUSY, NULL, 0);
        return;
    }

    /* 设置请求标志，AI 后台任务轮询到后处理 */
    strncpy(s_request_msg, req->msg, sizeof(s_request_msg) - 1);
    s_request_msg[sizeof(s_request_msg) - 1] = '\0';
    s_request_pending = 1;
    s_busy = 1;
}

/**
 * @brief 收到连接事件时设置网络就绪标志，重复就绪忽略。
 * @param[in] evt 借用 WiFi 事件。
 * @param[in] ctx 订阅上下文，当前未使用。
 */
static void on_wifi_connected(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_WIFI_CONNECTED) return;
    if (s_network_ready) return;  /* 去重：已就绪则忽略 */
    LOGI(TAG, "网络就绪，AI 服务可用");
    s_network_ready = 1;
}

/**
 * @brief 收到断网事件时撤销就绪并关闭当前 Keep-Alive 连接。
 * @param[in] evt 借用 WiFi 事件。
 * @param[in] ctx 订阅上下文，当前未使用。
 * @note 当前实现未与后台同步请求建立独立互斥或在途排空合同。
 */
static void on_wifi_disconnected(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_WIFI_DISCONNECTED) return;
    LOGW(TAG, "网络断开");
    s_network_ready = 0;
    https_keepalive_close();
}

//*********************************************************************************//
//                              AI 后台任务                                         //
//*********************************************************************************//

/**
 * @brief 长期等待网络及单槽请求，同步生成回复和情绪后发布结果或错误。
 * @param[in] priv 任务上下文，当前未使用。
 * @note 任务无退出分支；事件发布后的消息载荷仍沿用事件总线复制合同，不直接操作 LVGL。
 */
static void ai_task(void *priv)
{
    char reply[512];

    LOGI(TAG, "AI 后台任务启动 (model=%s)", s_config.model);

    /* 等待网络就绪 */
    while (!s_network_ready) {
        os_time_dly(100);
    }
    LOGI(TAG, "网络就绪，AI 服务可用");
    os_time_dly(100);

    /* 主循环：等待事件驱动的请求 */
    while (1) {
        if (s_request_pending) {
            LOGI(TAG, "处理请求: %s", s_request_msg);

            memset(reply, 0, sizeof(reply));
            int ret = ai_chat(s_request_msg, reply, sizeof(reply));

            ai_response_t rsp;
            memset(&rsp, 0, sizeof(rsp));

            if (ret == 0 && reply[0]) {
                strncpy(rsp.text, reply, sizeof(rsp.text) - 1);

                /* LLM 情绪分类 */
                d_emotion_t emo = ai_classify_emotion_via_llm(s_request_msg, rsp.text);
                if (emo == EMOTION_NEUTRAL) {
                    emo = ai_keyword_emotion(rsp.text);
                    if (emo == EMOTION_NEUTRAL) {
                        emo = ai_keyword_emotion(s_request_msg);
                    }
                }
                rsp.emotion = emo;

                /* 发布响应事件 */
                d_event_publish(EVT_AI_RESPONSE, &rsp, sizeof(rsp));
            } else {
                /* 发布错误事件 */
                d_event_publish(EVT_AI_ERROR, NULL, 0);
            }

            s_request_pending = 0;
            s_busy = 0;
        }
        os_time_dly(10);
    }
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

/**
 * @brief 复制配置描述、订阅请求及 WiFi 事件并创建长期后台任务。
 */
d_err_t ai_service_init(const ai_service_config_t *config)
{
    if (!config || !config->api_key) return D_ERR_INVALID_ARG;

    s_config = *config;
    LOGI(TAG, "AI 服务初始化 (host=%s, model=%s)", config->api_host, config->model);

    /* 订阅事件 */
    if ((d_event_subscribe(EVT_AI_REQUEST, on_ai_request, NULL) != 0) ||
        (d_event_subscribe(EVT_WIFI_CONNECTED, on_wifi_connected, NULL) != 0) ||
        (d_event_subscribe(EVT_WIFI_DISCONNECTED, on_wifi_disconnected, NULL) != 0)) {
        ai_service_deinit();
        LOGE(TAG, "AI 事件订阅失败");
        return D_ERR_IO;
    }

    /* 创建AI后台任务 */
    if (os_task_create(ai_task, NULL, 10, 4096, 0, "ai_svc") != 0) {
        d_event_unsubscribe(EVT_AI_REQUEST, on_ai_request);
        d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_connected);
        d_event_unsubscribe(EVT_WIFI_DISCONNECTED, on_wifi_disconnected);
        LOGE(TAG, "ai_svc 创建失败");
        return D_ERR_IO;
    }

    return D_OK;
}

/**
 * @brief 解除三类事件订阅并关闭当前 Keep-Alive 连接。
 */
void ai_service_deinit(void)
{
    d_event_unsubscribe(EVT_AI_REQUEST,        on_ai_request);
    d_event_unsubscribe(EVT_WIFI_CONNECTED,    on_wifi_connected);
    d_event_unsubscribe(EVT_WIFI_DISCONNECTED, on_wifi_disconnected);
    https_keepalive_close();
}



#endif /* D_FEATURE_AI_CHAT */
