/**
 * @file    ws_service.c
 * @brief   WebSocket 云平台通信服务实现（正式协议）
 *
 * 改写要点：
 *   - 签名 Query String 握手（SDK 不支持自定义 Header）
 *   - 应用层心跳（不使用 SDK _heart_thread）
 *   - 云平台消息信封：type / traceId / deviceSessionId / payload
 *   - 只发布 WS 命名空间事件（0x07xx），不 include ai_service
 *
 * 依赖：d_crypto, device_credentials, SDK WebSocket, d_event_bus
 */

#include "config/app_config.h"

#if D_DEVICE_CLOUD_ENABLE

#include "system/includes.h"
#include "os/os_api.h"
#include "web_socket/websocket_api.h"
#include "framework/d_event_bus.h"
#include "framework/d_error.h"
#include "framework/d_log.h"
#include "framework/d_event_payloads.h"
#include "framework/d_crypto.h"
#include "framework/compat/d_stdio_compat.h"
#include "modules/services/provisioning/device_credentials.h"
#include "ws_service.h"

#include <string.h>

#define TAG "WS"

/* 前向声明 */
static d_err_t ws_send_envelope(const char *type, const char *payload_json);
static void ws_publish_general_event(int event_id, const char *msg_type, const char *json);
static int ws_do_disconnect(void);
d_err_t ws_send_heartbeat(void);

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

static struct websocket_struct s_ws_info;
static volatile int s_connected       = 0;
static volatile int s_running         = 0;
static volatile int s_wifi_ready      = 0;
static volatile int s_ntp_ready       = 0;
static volatile int s_cred_ready      = 0;  /* 凭证已就绪（已激活） */
static volatile int s_connect_req     = 0;
static volatile int s_disconnect_req  = 0;
static uint32_t s_connection_intent;
static uint8_t s_resources_owned; /* Only ws_client creates/releases the SDK context. */
static OS_MUTEX s_send_mutex;
static uint8_t s_send_mutex_ready;

static uint8_t ws_connection_allowed(uint32_t intent)
{
    uint8_t allowed;
    OS_ENTER_CRITICAL();
    allowed = s_running && s_wifi_ready && s_ntp_ready && s_cred_ready &&
        !s_disconnect_req && intent == s_connection_intent;
    OS_EXIT_CRITICAL();
    return allowed;
}

/* 运行时会话 */
static int  s_device_session_id  = 0;
static int  s_agent_profile_id   = 0;
static int  s_hb_interval_sec    = D_WS_HEARTBEAT_INTERVAL_S;
static u32  s_last_pong_tick     = 0;
static u32  s_last_hb_tick       = 0;  
static u32  s_msg_seq            = 0;

/* URL 解析缓冲区（模块级，避免 static local 被重连覆盖） */
static char s_host_buf[128];        
static char s_ws_url[384];
static char s_ws_path[192] = "/device/ws";

/* 接收拷贝缓冲区（避免直接修改 SDK 内部 buffer） */
static char s_recv_buf[D_WS_RECV_BUF_SIZE];

/* 断线重连指数退避 */
static u32  s_reconnect_backoff_s = D_WS_RECONNECT_INTERVAL_S;

/* 连续握手失败计数 */
static int  s_handshake_fail_count = 0;
#define WS_MAX_HANDSHAKE_FAILS  3   /* 连续失败 N 次 → 发 EVT_WS_HANDSHAKE_FAILED */

static int ws_runtime_is_platform_ws(void)
{
    if (cred_is_activated() == 0) {
        return 0;
    }

    return (cred_get_runtime_provider() == RUNTIME_PROVIDER_PLATFORM_WS);
}


//*********************************************************************************//
//                              简易 JSON 工具                                     //
//*********************************************************************************//

static int json_find_str(const char *json, const char *key, char *out, int out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    const char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return -1;
    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return -1;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i;
}

static int json_find_int(const char *json, const char *key, int *out)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    const char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return -1;
    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t') p++;
    *out = atoi(p);
    return 0;
}

/* JSON 字符串转义 */
static int json_escape(const char *src, char *dst, int dst_size)
{
    int i = 0, j = 0;
    while (src[i] && j < dst_size - 2) {
        char c = src[i++];
        if (c == '"' || c == '\\') { dst[j++] = '\\'; }
        else if (c == '\n') { dst[j++] = '\\'; c = 'n'; }
        else if (c == '\r') { dst[j++] = '\\'; c = 'r'; }
        else if (c == '\t') { dst[j++] = '\\'; c = 't'; }
        dst[j++] = c;
    }
    dst[j] = '\0';
    return j;
}

static int json_copy_value(const char *json, const char *key, char *out, int out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos || !out || out_size <= 0) return -1;

    const char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return -1;

    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    if (*p == '{' || *p == '[') {
        char open_ch = *p;
        char close_ch = (open_ch == '{') ? '}' : ']';
        int depth = 0;
        int in_string = 0;
        int escaped = 0;
        int i = 0;

        while (*p && i < out_size - 1) {
            char c = *p++;
            out[i++] = c;

            if (in_string) {
                if (escaped) {
                    escaped = 0;
                } else if (c == '\\') {
                    escaped = 1;
                } else if (c == '"') {
                    in_string = 0;
                }
                continue;
            }

            if (c == '"') {
                in_string = 1;
            } else if (c == open_ch) {
                depth++;
            } else if (c == close_ch) {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
        }

        out[i] = '\0';
        return i;
    }

    if (*p == '"') {
        int i = 0;
        int escaped = 0;
        do {
            char c = *p++;
            out[i++] = c;
            if (escaped) {
                escaped = 0;
            } else if (c == '\\') {
                escaped = 1;
            } else if (c == '"' && i > 1) {
                break;
            }
        } while (*p && i < out_size - 1);
        out[i] = '\0';
        return i;
    }

    {
        int i = 0;
        while (*p && *p != ',' && *p != '}' && *p != ']' && i < out_size - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return i;
    }
}

//*********************************************************************************//
//                              URL 编码工具                                        //
//*********************************************************************************//

static int url_encode_param(const char *key, const char *val, char *out, int out_size)
{
    /* 简化：key 和 val 均不含需要编码的特殊字符（hex/nonce 均为 [a-f0-9]） */
    return snprintf(out, out_size, "%s=%s", key, val);
}

//*********************************************************************************//
//                              握手 URL 构建                                       //
//*********************************************************************************//

static int build_handshake_path(const char *base_path, char *file_buf, int file_size)
{
    const device_cred_t *cred = cred_get();
    if (!cred || cred->access_key[0] == '\0') {
        LOGE(TAG, "无运行时凭证");
        return -1;
    }

    /* 生成签名参数 */
    char ts[32], nonce[33], sign[65];
    snprintf(ts, sizeof(ts), "%llu", d_get_timestamp_ms());
    d_generate_nonce(nonce, sizeof(nonce));

    /* canonical: deviceId|accessKey|timestamp|nonce */
    char canonical[512];
    const char *parts[] = { cred->device_id, cred->access_key, ts, nonce };
    int clen = d_build_canonical(parts, 4, canonical, sizeof(canonical));
    if (clen < 0) return -1;

    d_err_t err = d_hmac_sha256(
        (const unsigned char *)cred->access_secret, (int)strlen(cred->access_secret),
        (const unsigned char *)canonical, clen, sign);
    if (err != D_OK) return -1;

    if (!base_path || base_path[0] == '\0') {
        base_path = "/device/ws";
    }

    /* 拼接 Query String：<endpoint_path>?deviceId=...&accessKey=...&timestamp=...&nonce=...&sign=... */
    int pos = snprintf(file_buf, file_size, "%s%c", base_path, strchr(base_path, '?') ? '&' : '?');
    pos += url_encode_param("deviceId",  cred->device_id,  file_buf + pos, file_size - pos);
    file_buf[pos++] = '&';
    pos += url_encode_param("accessKey", cred->access_key,  file_buf + pos, file_size - pos);
    file_buf[pos++] = '&';
    pos += url_encode_param("timestamp", ts,                file_buf + pos, file_size - pos);
    file_buf[pos++] = '&';
    pos += url_encode_param("nonce",     nonce,             file_buf + pos, file_size - pos);
    file_buf[pos++] = '&';
    pos += url_encode_param("sign",      sign,              file_buf + pos, file_size - pos);
    file_buf[pos++] = '&';
    pos += url_encode_param("firmwareVersion", D_FIRMWARE_VERSION, file_buf + pos, file_size - pos);

    LOGD(TAG, "握手路径 (%d bytes): %s", pos, file_buf);
    return pos;
}

//*********************************************************************************//
//                              低层发送                                            //
//*********************************************************************************//

static d_err_t ws_raw_send(const char *json_str, uint32_t intent)
{
    int result;
    if (json_str == NULL) return D_ERR_INVALID_ARG;
    if (!s_send_mutex_ready) return D_ERR_NOT_READY;
    os_mutex_pend(&s_send_mutex, 0);
    if (!s_connected || !ws_connection_allowed(intent) || !s_ws_info._send) {
        os_mutex_post(&s_send_mutex);
        return D_ERR_NOT_READY;
    }
    LOGD(TAG, "发送: %.200s", json_str);
    result = s_ws_info._send(&s_ws_info, (u8 *)json_str, (int)strlen(json_str), WCT_TXTDATA);
    os_mutex_post(&s_send_mutex);
    return result != 0 ? D_OK : D_ERR_IO;
}

/* 构建消息信封并发送 */
static d_err_t ws_send_envelope_ex(const char *type, int agent_profile_id,
                                   const char *command_id, const char *payload_json)
{
    const device_cred_t *cred = cred_get();
    char trace_id[24];
    uint32_t intent, sequence;
    int session_id;
    OS_ENTER_CRITICAL();
    intent = s_connection_intent;
    sequence = ++s_msg_seq;
    session_id = s_device_session_id;
    OS_EXIT_CRITICAL();
    snprintf(trace_id, sizeof(trace_id), "t_%08x_%u", (unsigned int)rand32(), (unsigned int)sequence);

    char buf[1280];  
    int pos = snprintf(buf, sizeof(buf),
        "{\"traceId\":\"%s\",\"type\":\"%s\","
        "\"deviceId\":\"%s\",\"deviceSessionId\":%d,",
        trace_id, type, cred->device_id, session_id);

    if (pos <= 0 || pos >= (int)sizeof(buf)) return D_ERR_NO_MEMORY;

    if (agent_profile_id > 0) {
        int n = snprintf(buf + pos, sizeof(buf) - pos, "\"agentProfileId\":%d,", agent_profile_id);
        if (n <= 0 || pos + n >= (int)sizeof(buf)) return D_ERR_NO_MEMORY;
        pos += n;
    }

    if (command_id && command_id[0]) {
        int n = snprintf(buf + pos, sizeof(buf) - pos, "\"commandId\":\"%s\",", command_id);
        if (n <= 0 || pos + n >= (int)sizeof(buf)) return D_ERR_NO_MEMORY;
        pos += n;
    }

    {
        int n = snprintf(buf + pos, sizeof(buf) - pos,
            "\"timestamp\":%llu,\"payload\":%s}",
            d_get_timestamp_ms(),
            (payload_json && payload_json[0]) ? payload_json : "{}");
        if (n <= 0 || pos + n >= (int)sizeof(buf)) return D_ERR_NO_MEMORY;
        pos += n;
    }

    return ws_raw_send(buf, intent);
}

static d_err_t ws_send_envelope(const char *type, const char *payload_json)
{
    return ws_send_envelope_ex(type, s_agent_profile_id, NULL, payload_json);
}

//*********************************************************************************//
//                              接收分发                                            //
//*********************************************************************************//

static void dispatch_message(const char *json, int len)
{
    char msg_type[64] = {0};
    json_find_str(json, "type", msg_type, sizeof(msg_type));

    /* connected — 握手后服务器首条消息 */
    if (strcmp(msg_type, "connected") == 0) {
        json_find_int(json, "deviceSessionId", &s_device_session_id);
        int hb = 0;
        const char *payload_pos = strstr(json, "\"payload\"");
        if (payload_pos && json_find_int(payload_pos, "heartbeatIntervalSec", &hb) == 0 && hb > 0) {
            s_hb_interval_sec = hb;
        } else if (json_find_int(json, "heartbeatIntervalSec", &hb) == 0 && hb > 0) {
            s_hb_interval_sec = hb;
        }
        LOGI(TAG, "connected: sessionId=%d, hb=%ds", s_device_session_id, s_hb_interval_sec);
        ws_connected_info_t info = { .device_session_id = s_device_session_id };
        d_event_post(EVT_WS_CONNECTED, &info, sizeof(info));
        return;
    }

    /* pong — 心跳应答 */
    if (strcmp(msg_type, "pong") == 0) {
        s_last_pong_tick = os_time_get();
        int new_hb = 0;
        const char *payload_pos = strstr(json, "\"payload\"");
        if (payload_pos && json_find_int(payload_pos, "heartbeatIntervalSec", &new_hb) == 0 && new_hb > 0) {
            s_hb_interval_sec = new_hb;
        } else if (json_find_int(json, "heartbeatIntervalSec", &new_hb) == 0 && new_hb > 0) {
            s_hb_interval_sec = new_hb;
        }
        return;
    }

    /* error */
    if (strcmp(msg_type, "error") == 0) {
        d_error_info_t err = {0};
        json_find_int(json, "code", &err.code);
        static char s_err_msg[128];
        s_err_msg[0] = '\0';
        json_find_str(json, "message", s_err_msg, sizeof(s_err_msg));
        err.msg = s_err_msg[0] ? s_err_msg : "WebSocket error";
        LOGE(TAG, "服务器错误: code=%d, msg=%s", err.code, err.msg);
        d_event_publish(EVT_WS_ERROR, &err, sizeof(err));
        return;
    }

    /* ack — 仅日志 */
    if (strcmp(msg_type, "ack") == 0) {
        LOGD(TAG, "收到 ack");
        return;
    }

    /* chat.accepted */
    if (strcmp(msg_type, "chat.accepted") == 0) {
        ws_chat_event_t evt = {0};
        json_find_str(json, "traceId", evt.trace_id, sizeof(evt.trace_id));
        d_event_post(EVT_WS_CHAT_ACCEPTED, &evt, sizeof(evt));
        return;
    }

    /* chat.delta */
    if (strcmp(msg_type, "chat.delta") == 0) {
        ws_chat_delta_t delta = {0};
        json_find_str(json, "traceId", delta.trace_id, sizeof(delta.trace_id));
        /* 提取 payload 中的增量文本 */
        const char *payload_pos = strstr(json, "\"payload\"");
        if (payload_pos) {
            json_find_str(payload_pos, "content", delta.text, sizeof(delta.text));
            if (delta.text[0] == '\0') {
                json_find_str(payload_pos, "text", delta.text, sizeof(delta.text));
            }
        }
        d_event_publish(EVT_WS_CHAT_DELTA, &delta, sizeof(delta));
        return;
    }

    /* chat.done */
    if (strcmp(msg_type, "chat.done") == 0) {
        ws_chat_event_t evt = {0};
        json_find_str(json, "traceId", evt.trace_id, sizeof(evt.trace_id));
        d_event_post(EVT_WS_CHAT_DONE, &evt, sizeof(evt));
        return;
    }

    /* command — 平台下行命令（精确匹配，优先于 command.* 上行前缀匹配） */
    if (strcmp(msg_type, "command") == 0) {
        LOGI(TAG, "收到平台命令: %s", json);
        ws_publish_general_event(EVT_WS_COMMAND_MSG, msg_type, json);
        return;
    }

    /* command.* — 设备上行命令回执（ack / result）经过本地 echo 路由 */
    if (strncmp(msg_type, "command.", 8) == 0) {
        ws_publish_general_event(EVT_WS_COMMAND_MSG, msg_type, json);
        return;
    }

    /* workflow.* */
    if (strncmp(msg_type, "workflow.", 9) == 0) {
        ws_publish_general_event(EVT_WS_WORKFLOW_MSG, msg_type, json);
        return;
    }

    if (strcmp(msg_type, "event.accepted") == 0) {
        ws_publish_general_event(EVT_WS_MESSAGE_RECV, msg_type, json);
        return;
    }

    if (strcmp(msg_type, "agent.switched") == 0) {
        int new_agent_profile_id = 0;
        if (json_find_int(json, "agentProfileId", &new_agent_profile_id) == 0 && new_agent_profile_id > 0) {
            s_agent_profile_id = new_agent_profile_id;
        }
        ws_publish_general_event(EVT_WS_MESSAGE_RECV, msg_type, json);
        return;
    }

    if (strcmp(msg_type, "binary.unsupported") == 0) {
        LOGW(TAG, "服务器返回 binary.unsupported");
        ws_publish_general_event(EVT_WS_MESSAGE_RECV, msg_type, json);
        return;
    }

    /* 通用消息 */
    LOGD(TAG, "未处理消息类型: %s", msg_type);
    ws_publish_general_event(EVT_WS_MESSAGE_RECV, msg_type, json);
}

static void ws_publish_general_event(int event_id, const char *msg_type, const char *json)
{
    ws_general_msg_t msg = {0};
    strncpy(msg.type, msg_type, sizeof(msg.type) - 1);
    json_find_str(json, "commandId", msg.command_id, sizeof(msg.command_id));
    json_copy_value(json, "payload", msg.payload, sizeof(msg.payload));
    d_event_publish(event_id, &msg, sizeof(msg));
}

//*********************************************************************************//
//                              WebSocket SDK 回调                                 //
//*********************************************************************************//

static void ws_recv_callback(u8 *buf, u32 len, u8 type, void *priv)
{
    if (!s_resources_owned || priv != s_ws_info.priv) return;
    if (type == WCT_TXTDATA && len > 0) {
        if (buf == NULL || !s_connected || !ws_connection_allowed((uint32_t)(uintptr_t)priv)) {
            return;
        }
        if (len >= D_WS_RECV_BUF_SIZE) {
            LOGW(TAG, "消息过长(%u >= %d)，丢弃", (unsigned)len, D_WS_RECV_BUF_SIZE);
            return;
        }
        memcpy(s_recv_buf, buf, len);
        s_recv_buf[len] = '\0';
        LOGD(TAG, "收到(%d): %.200s", len, s_recv_buf);

        /* 分发 */
        dispatch_message(s_recv_buf, (int)len);

    } else if (type == WCT_DISCONN || type == WCT_CLOSE_OK) {
        LOGW(TAG, "服务器断开连接");
        OS_ENTER_CRITICAL();
        s_connected = 0;
        s_connection_intent++;
        if (!s_disconnect_req && s_wifi_ready && s_ntp_ready && s_cred_ready) {
            s_connect_req = 1;
        }
        s_disconnect_req = 1;
        OS_EXIT_CRITICAL();
        /* Callback is still using s_ws_info. The owner joins it and frees
         * buffers before publishing completion or attempting a new connect. */
    }
}

//*********************************************************************************//
//                              连接 / 断开                                         //
//*********************************************************************************//

static int ws_do_connect(void)
{
    uint32_t intent;
    if (ws_do_disconnect() != 0) return -1;
    OS_ENTER_CRITICAL();
    intent = s_connection_intent;
    OS_EXIT_CRITICAL();
    if (!ws_connection_allowed(intent)) return -1;
    const device_cred_t *cred = cred_get();
    LOGI(TAG, "连接 WebSocket: endpoint=%s", cred->ws_endpoint);

    memset(&s_ws_info, 0, sizeof(s_ws_info));

    /* 注册函数指针 */
    s_ws_info._init        = websockets_client_socket_init;
    s_ws_info._exit        = websockets_client_socket_exit;
    s_ws_info._handshack   = webcockets_client_socket_handshack;
    s_ws_info._send        = websockets_client_socket_send;
    s_ws_info._recv_thread = websockets_client_socket_recv_thread;
    s_ws_info._heart_thread= NULL;   /* 应用层心跳，不使用内置 */
    s_ws_info._recv_cb     = ws_recv_callback;
    s_ws_info._recv        = NULL;
    s_ws_info.priv         = (void *)(uintptr_t)intent;

    /* 解析 endpoint，提取 host / port / path */
    const char *ep = cred->ws_endpoint;
    int use_ssl = 0;
    if (strncmp(ep, "wss://", 6) == 0) { ep += 6; use_ssl = 1; }
    else if (strncmp(ep, "ws://", 5) == 0) { ep += 5; }

    int port = use_ssl ? 443 : 80;

    /* 若云平台使用 HTTPS，则 ws:// 也应强制升级为 wss:// (port 443)
     * 原因：服务端通常只开 443，finalize 可能错误返回 ws:// */
    if (!use_ssl && port == 80) {
        if (strncmp(D_CLOUD_BASE_URL, "https://", 8) == 0) {
            LOGI(TAG, "云平台使用 HTTPS，ws:// 自动升级为 wss:// (port 443)");
            use_ssl = 1;
            port = 443;
        }
    }

    const char *colon = strchr(ep, ':');
    const char *slash = strchr(ep, '/');
    if (colon && (!slash || colon < slash)) {
        int hlen = (int)(colon - ep);
        if (hlen >= (int)sizeof(s_host_buf)) hlen = sizeof(s_host_buf) - 1;
        memcpy(s_host_buf, ep, hlen);
        s_host_buf[hlen] = '\0';
        port = atoi(colon + 1);
    } else if (slash) {
        int hlen = (int)(slash - ep);
        if (hlen >= (int)sizeof(s_host_buf)) hlen = sizeof(s_host_buf) - 1;
        memcpy(s_host_buf, ep, hlen);
        s_host_buf[hlen] = '\0';
    } else {
        strncpy(s_host_buf, ep, sizeof(s_host_buf) - 1);
        s_host_buf[sizeof(s_host_buf) - 1] = '\0';
    }

    memset(s_ws_path, 0, sizeof(s_ws_path));
    strncpy(s_ws_path, slash ? slash : "/device/ws", sizeof(s_ws_path) - 1);
    s_ws_path[sizeof(s_ws_path) - 1] = '\0';

    /* 构建带签名 Query 的握手路径（签名参数必须嵌入 URL） */
    char signed_path[320];
    if (build_handshake_path(s_ws_path, signed_path, sizeof(signed_path)) < 0) {
        LOGE(TAG, "构建握手路径失败");
        return -1;
    }

    memset(s_ws_url, 0, sizeof(s_ws_url));
    if (snprintf(s_ws_url, sizeof(s_ws_url), "%s://%s:%d%s",
                 use_ssl ? "wss" : "ws", s_host_buf, port, signed_path) >= (int)sizeof(s_ws_url)) {
        LOGE(TAG, "WebSocket URL 过长");
        return -1;
    }

    /* 填写连接参数 */
    s_ws_info.ip_or_url      = (u8 *)s_ws_url;
    s_ws_info.port           = (u16)port;
    s_ws_info.recv_time_out  = 1000;
    s_ws_info.websocket_mode = use_ssl ? WEBSOCKETS_MODE : WEBSOCKET_MODE;
    s_ws_info.origin_str     = "http://ac791-device";

    /* req_head.host（最大 32 字节） */
    strncpy((char *)s_ws_info.req_head.host, s_host_buf,
            sizeof(s_ws_info.req_head.host) - 1);
    s_ws_info.req_head.host[sizeof(s_ws_info.req_head.host) - 1] = '\0';
    /* req_head.file（兜底） */
    strncpy((char *)s_ws_info.req_head.file, signed_path,
            sizeof(s_ws_info.req_head.file) - 1);
    s_ws_info.req_head.file[sizeof(s_ws_info.req_head.file) - 1] = '\0';
    LOGI(TAG, "WebSocket connect info: host=%s port=%d ssl=%d path_len=%d",
         s_host_buf, port, use_ssl, (int)strlen(signed_path));

    /* 结构体版本校验 */
    if (websockets_struct_check(sizeof(struct websocket_struct)) == 0) {
        LOGE(TAG, "websocket 结构体版本不匹配");
        return -1;
    }

    /* 初始化 socket（TCP + TLS） */
    if (!ws_connection_allowed(intent)) return -1;
    s_resources_owned = 1U; /* SDK example pairs even failed init with _exit. */
    int ret = s_ws_info._init(&s_ws_info);
    if (ret == 0) {
        LOGE(TAG, "WebSocket init 失败");
        (void)ws_do_disconnect();
        return -1;
    }
    if (!ws_connection_allowed(intent)) { (void)ws_do_disconnect(); return -1; }

    /* WebSocket 握手 */
    ret = s_ws_info._handshack(&s_ws_info);
    if (ret == 0) {
        (void)ws_do_disconnect();
        s_handshake_fail_count++;
        LOGE(TAG, "WebSocket 握手失败 (连续 %d 次)", s_handshake_fail_count);
        if (s_handshake_fail_count >= WS_MAX_HANDSHAKE_FAILS) {
            LOGE(TAG, "握手连续失败 %d 次，凭证可能已失效，请检查 accessKey/accessSecret",
                 s_handshake_fail_count);
            d_event_post(EVT_WS_HANDSHAKE_FAILED, NULL, 0);
            s_handshake_fail_count = 0;  /* 重置，避免重复发事件 */
        }
        return -1;
    }

    if (!ws_connection_allowed(intent)) { (void)ws_do_disconnect(); return -1; }
    LOGI(TAG, "WebSocket 握手成功");
    os_mutex_pend(&s_send_mutex, 0);
    s_connected           = 1;
    os_mutex_post(&s_send_mutex);
    s_last_pong_tick      = os_time_get();
    s_last_hb_tick        = os_time_get();
    s_reconnect_backoff_s = D_WS_RECONNECT_INTERVAL_S;
    s_handshake_fail_count = 0;  /* 握手成功，重置失败计数 */

    /* 启动 recv 线程 */
    ret = thread_fork("ws_recv", 10, 4096, 0, &s_ws_info.recv_thread_id,
                      s_ws_info._recv_thread, &s_ws_info);
    if (ret != OS_NO_ERR) {
        (void)ws_do_disconnect();
        LOGE(TAG, "ws_recv 创建失败: %d", ret);
        return -1;
    }

    websockets_sleep(100); /* 给 recv 线程启动缓冲 */
    if (!ws_connection_allowed(intent)) { (void)ws_do_disconnect(); return -1; }
    return 0;
}

static int ws_do_disconnect(void)
{
    if (!s_resources_owned && !s_ws_info.recv_thread_id) return 0;
    /* Drain a send before closing its context. Never hold this mutex across
     * join: a receive callback can synchronously publish a command ACK. */
    os_mutex_pend(&s_send_mutex, 0);
    OS_ENTER_CRITICAL();
    s_connected = 0;
    s_connection_intent++;
    OS_EXIT_CRITICAL();
    os_mutex_post(&s_send_mutex);
    if (s_ws_info.recv_thread_id) {
        thread_kill(&s_ws_info.recv_thread_id, KILL_WAIT);
        if (s_ws_info.recv_thread_id) {
            LOGE(TAG, "接收线程未退出，保留连接资源");
            return -1;
        }
    }
    if (s_ws_info.websocket_valid == ESTABLISHED)
        websockets_client_notify_disconnet_to_server(&s_ws_info);
    if (s_resources_owned && s_ws_info._exit) {
        s_ws_info._exit(&s_ws_info);
    }
    s_resources_owned = 0U;
    memset(&s_ws_info, 0, sizeof(s_ws_info));
    OS_ENTER_CRITICAL();
    s_device_session_id = 0;
    OS_EXIT_CRITICAL();
    LOGI(TAG, "WebSocket 已断开");
    d_event_post(EVT_WS_DISCONNECTED, NULL, 0);
    return 0;
}

//*********************************************************************************//
//                              主任务（含应用层心跳）                              //
//*********************************************************************************//

static void ws_client_task(void *priv)
{
    (void)priv;
    LOGI(TAG, "WebSocket 任务启动");

    while (s_running) {
        int close_requested;
        OS_ENTER_CRITICAL();
        close_requested = s_disconnect_req;
        s_disconnect_req = 0;
        OS_EXIT_CRITICAL();
        /* Reclaim first, including when readiness was lost. Waiting for Wi-Fi
         * before cleanup used to strand the previous context indefinitely. */
        if (close_requested || (s_resources_owned &&
            (!s_connected || !s_wifi_ready || !s_ntp_ready || !s_cred_ready))) {
            if (ws_do_disconnect() != 0) { os_time_dly(100); continue; }
        }

        /* ─── 等待前置条件 ─── */
        if (!s_wifi_ready || !s_ntp_ready || !s_cred_ready) {
            os_time_dly(100);
            continue;
        }

        /* ─── 已连接：心跳 + 断开检查 ─── */
        if (s_connected) {
            /* 应用层心跳检查 */
            u32 now = os_time_get();
            u32 elapsed_sec = (now - s_last_pong_tick) / 100;

            if (elapsed_sec >= (u32)D_WS_HEARTBEAT_TIMEOUT_S) {
                LOGW(TAG, "心跳超时 (%us)，重连", elapsed_sec);
                (void)ws_do_disconnect();
                s_connect_req = 1;
                continue;
            }

            /* 定时发送心跳 */
            u32 hb_elapsed = (now - s_last_hb_tick) / 100;
            if (hb_elapsed >= (u32)s_hb_interval_sec) {
                ws_send_heartbeat();
                s_last_hb_tick = now;
            }

            os_time_dly(10);
            continue;
        }

        /* ─── 未连接：等待连接请求 ─── */
        if (!s_connect_req) {
            os_time_dly(100);
            continue;
        }

        LOGI(TAG, "等待网络稳定...");
        os_time_dly(200);

        if (!s_wifi_ready || !s_ntp_ready || !s_cred_ready || s_connected || s_disconnect_req) continue;
        /* A queued cleanup can interrupt this wait. Consume the connection
         * request only when actually starting it, so that retry is not lost. */
        OS_ENTER_CRITICAL();
        s_connect_req = 0;
        OS_EXIT_CRITICAL();

        int ret = ws_do_connect();
        if (ret != 0) {
            /* [指数退避] 5s → 10s → 20s → 40s → 60s（上限） */
            LOGW(TAG, "连接失败，%us 后重试", (unsigned)s_reconnect_backoff_s);
            os_time_dly(s_reconnect_backoff_s * 100);
            if (s_reconnect_backoff_s * 2 <= D_WS_RECONNECT_MAX_S) {
                s_reconnect_backoff_s *= 2;
            } else {
                s_reconnect_backoff_s = D_WS_RECONNECT_MAX_S;
            }
            OS_ENTER_CRITICAL();
            s_connect_req = s_running && s_wifi_ready && s_ntp_ready && s_cred_ready;
            OS_EXIT_CRITICAL();
        }
    }

    while (ws_do_disconnect() != 0) os_time_dly(100);
    LOGI(TAG, "WebSocket 任务退出");
}

//*********************************************************************************//
//                              事件回调                                            //
//*********************************************************************************//

static void on_wifi_event(const d_event_t *event, void *ctx)
{
    (void)ctx;
    if (!event) return;
    OS_ENTER_CRITICAL();
    if (event->id == EVT_WIFI_CONNECTED) {
        s_wifi_ready = 1;
        if (s_ntp_ready && s_cred_ready) s_connect_req = 1;
    } else if (event->id == EVT_WIFI_DISCONNECTED) {
        s_wifi_ready = 0;
        s_connected = 0;
        s_connection_intent++;
        s_connect_req = 0;
        s_disconnect_req = 1;
    }
    OS_EXIT_CRITICAL();
}

static void on_ntp_synced(const d_event_t *event, void *ctx)
{
    (void)ctx;
    if (!event || event->id != EVT_NTP_SYNCED) return;
    OS_ENTER_CRITICAL();
    s_ntp_ready = 1;
    /* 如果凭证也就绪，自动触发连接 */
    if (s_cred_ready) {
        s_connect_req = 1;
    }
    OS_EXIT_CRITICAL();
}

static void on_provision_ok(const d_event_t *event, void *ctx)
{
    (void)ctx;
    if (!event || event->id != EVT_PROVISION_OK) return;
    int ready = ws_runtime_is_platform_ws();
    OS_ENTER_CRITICAL();
    s_cred_ready = ready;
    if (!s_cred_ready) {
        s_connection_intent++;
        s_disconnect_req = 1;
        s_connected = 0;
        s_connect_req = 0;
        OS_EXIT_CRITICAL();
        LOGI(TAG, "当前 runtimeProvider 非 PLATFORM_WS,跳过 /device/ws");
        return;
    }
    if (s_ntp_ready) {
        s_connect_req = 1;
    }
    OS_EXIT_CRITICAL();
}

//*********************************************************************************//
//                              发送 API 实现                                       //
//*********************************************************************************//

d_err_t ws_send_heartbeat(void)
{
    return ws_send_envelope_ex("heartbeat", 0, NULL, "{\"source\":\"device\"}");
}

d_err_t ws_send_event_report(const char *event_type, const char *event_code,
                             const char *payload_json)
{
    char payload[512];
    if (payload_json && payload_json[0] == '{') {
        int inner_len = (int)strlen(payload_json);
        if (inner_len >= 2 && payload_json[inner_len - 1] == '}') {
            int copy_len = inner_len - 2;
            char inner[384];
            if (copy_len >= (int)sizeof(inner)) copy_len = sizeof(inner) - 1;
            memcpy(inner, payload_json + 1, copy_len);
            inner[copy_len] = '\0';
            if (inner[0]) {
                snprintf(payload, sizeof(payload),
                         "{\"eventType\":\"%s\",\"eventCode\":\"%s\",%s}",
                         event_type ? event_type : "",
                         event_code ? event_code : "",
                         inner);
            } else {
                snprintf(payload, sizeof(payload),
                         "{\"eventType\":\"%s\",\"eventCode\":\"%s\"}",
                         event_type ? event_type : "",
                         event_code ? event_code : "");
            }
        } else {
            snprintf(payload, sizeof(payload),
                     "{\"eventType\":\"%s\",\"eventCode\":\"%s\"}",
                     event_type ? event_type : "",
                     event_code ? event_code : "");
        }
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"eventType\":\"%s\",\"eventCode\":\"%s\"}",
                 event_type ? event_type : "",
                 event_code ? event_code : "");
    }
    return ws_send_envelope("event.report", payload);
}

d_err_t ws_send_command_ack(const char *command_id)
{
    if (!command_id) return D_ERR_INVALID_ARG;
    return ws_send_envelope_ex("command.ack", 0, command_id, "{}");
}

d_err_t ws_send_command_result(const char *command_id, int success,
                               const char *result_json)
{
    if (!command_id) return D_ERR_INVALID_ARG;
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"status\":\"%s\",\"success\":%s,\"result\":%s}",
             success ? "success" : "failed", success ? "true" : "false",
             (result_json && result_json[0]) ? result_json : "null");
    return ws_send_envelope_ex("command.result", 0, command_id, payload);
}

d_err_t ws_send_workflow_resume(const char *feedback)
{
    char escaped[512];
    json_escape(feedback ? feedback : "", escaped, sizeof(escaped));
    char payload[768];
    snprintf(payload, sizeof(payload), "{\"feedback\":\"%s\"}", escaped);
    return ws_send_envelope("workflow.resume", payload);
}

d_err_t ws_send_agent_switch(int agent_profile_id)
{
    if (agent_profile_id > 0) {
        s_agent_profile_id = agent_profile_id;
    }
    return ws_send_envelope_ex("agent.switch", agent_profile_id, NULL, "{}");
}

//*********************************************************************************//
//                              初始化                                              //
//*********************************************************************************//

d_err_t ws_service_init(void)
{
    int ret;

    LOGI(TAG, "WebSocket 服务初始化（云平台协议）");
    if (s_running) return D_OK;
    if (!s_send_mutex_ready) {
        if (os_mutex_create(&s_send_mutex) != OS_NO_ERR) return D_ERR_IO;
        s_send_mutex_ready = 1U;
    }

    /* 检查凭证状态 */
    if (ws_runtime_is_platform_ws()) {
        s_cred_ready = 1;
        LOGI(TAG, "凭证已激活");
    }

    /* 订阅事件 */
    if (d_event_subscribe(EVT_WIFI_CONNECTED, on_wifi_event, NULL) != 0) {
        LOGE(TAG, "EVT_WIFI_CONNECTED 订阅失败");
        return D_ERR_IO;
    }
    if (d_event_subscribe(EVT_WIFI_DISCONNECTED, on_wifi_event, NULL) != 0) {
        d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_event);
        LOGE(TAG, "EVT_WIFI_DISCONNECTED 订阅失败");
        return D_ERR_IO;
    }
    if (d_event_subscribe(EVT_NTP_SYNCED, on_ntp_synced, NULL) != 0) {
        d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_event);
        d_event_unsubscribe(EVT_WIFI_DISCONNECTED, on_wifi_event);
        LOGE(TAG, "EVT_NTP_SYNCED 订阅失败");
        return D_ERR_IO;
    }
    if (d_event_subscribe(EVT_PROVISION_OK, on_provision_ok, NULL) != 0) {
        d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_event);
        d_event_unsubscribe(EVT_WIFI_DISCONNECTED, on_wifi_event);
        d_event_unsubscribe(EVT_NTP_SYNCED, on_ntp_synced);
        LOGE(TAG, "EVT_PROVISION_OK 订阅失败");
        return D_ERR_IO;
    }

    /* 启动主任务 */
    s_running = 1;
    ret = thread_fork("ws_client", 8, 4096, 0, 0, ws_client_task, NULL);
    if (ret != OS_NO_ERR) {
        s_running = 0;
        d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_event);
        d_event_unsubscribe(EVT_WIFI_DISCONNECTED, on_wifi_event);
        d_event_unsubscribe(EVT_NTP_SYNCED, on_ntp_synced);
        d_event_unsubscribe(EVT_PROVISION_OK, on_provision_ok);
        LOGE(TAG, "ws_client 创建失败: %d", ret);
        return D_ERR_IO;
    }

    return D_OK;
}

#endif /* D_DEVICE_CLOUD_ENABLE */
