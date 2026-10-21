/**
 * @file    qwen_wss.c
 * @brief   Qwen WSS 连接管理实现
 * @author  D_Code
 * @date    2026-05-19
 */

#include <stdbool.h>

#define BOOL_DEFINE_CONFLICT

#include "config/app_config.h"

#if D_FEATURE_QWEN

#ifndef CONFIG_PLATFORM_AC7911
#define CONFIG_PLATFORM_AC7911 1
#endif

#include "qwen_wss.h"
#include "qwen_net.h"
#include "framework/d_log.h"
#include "framework/compat/d_stdio_compat.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "web_socket/websocket_api.h"
#include "web_socket/websocket_ssl.h"
#include "sock_api/sock_api.h"
#include "sdk/include/c_mmi.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include "mbedtls/ssl.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define TAG                             "QWEN_WSS"
#define QWEN_WSS_SEND_BUF_SIZE          2048U
#define QWEN_WSS_HANDSHAKE_BUF_SIZE     2048U
#define QWEN_WSS_RESPONSE_BUF_SIZE      1024U
#define QWEN_WSS_URL_BUF_SIZE           1280U
#define QWEN_WSS_RECV_BUF_SIZE          (16U * 1024U)
#define QWEN_WSS_TASK_STACK             4096
#define QWEN_WSS_TASK_PRIO              10
#define QWEN_WSS_KEY_MAX_LEN            40U
#define QWEN_WSS_ACCEPT_MAX_LEN         32U
#define QWEN_WSS_SHA1_LEN               20U
#define QWEN_WSS_GUID                   "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static const char s_qwen_wss_ca_pem[] =
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\r\n"
    "A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\r\n"
    "Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\r\n"
    "MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\r\n"
    "A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\r\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\r\n"
    "RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\r\n"
    "gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\r\n"
    "KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\r\n"
    "QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\r\n"
    "XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\r\n"
    "DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\r\n"
    "LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\r\n"
    "RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\r\n"
    "jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\r\n"
    "6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\r\n"
    "mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\r\n"
    "Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\r\n"
    "WD9f\r\n"
    "-----END CERTIFICATE-----\r\n";

typedef struct {
    struct websocket_struct ws_info;
    OS_MUTEX op_mutex;
    int send_thread_id;
    uint8_t is_connected;
    uint8_t closing;
    uint32_t connection_generation;
    qwen_wss_disconnect_callback_t disconnect_callback;
    char url[QWEN_WSS_URL_BUF_SIZE];
} qwen_wss_context_t;

typedef struct {
    const char *host;
    const char *api;
    const char *header;
    char *ws_key;
    uint32_t ws_key_size;
} qwen_wss_request_param_t;

static qwen_wss_context_t s_qwen_wss_ctx;
static volatile uint32_t s_transport_generation;

static int qwen_wss_is_empty(const char *value)
{
    return ((value == NULL) || (value[0] == '\0'));
}

static void qwen_wss_mark_disconnected(void)
{
    s_qwen_wss_ctx.is_connected = 0U;
}

static void qwen_wss_notify_unexpected_disconnect(void)
{
    if (s_qwen_wss_ctx.is_connected == 0U) {
        return;
    }

    qwen_wss_mark_disconnected();
    if (s_qwen_wss_ctx.disconnect_callback != NULL) {
        s_qwen_wss_ctx.disconnect_callback(s_qwen_wss_ctx.connection_generation);
    }
}

static int qwen_wss_get_port_value(const char *port_str)
{
    int port_value;

    if (qwen_wss_is_empty(port_str) != 0) {
        return -1;
    }

    port_value = atoi(port_str);
    if ((port_value <= 0) || (port_value > 65535)) {
        return -1;
    }

    return port_value;
}

static int qwen_wss_append(char *buffer, uint32_t size, uint32_t *offset, const char *format, ...)
{
    int write_len;
    va_list args;

    if ((buffer == NULL) || (offset == NULL) || (format == NULL) || (*offset >= size)) {
        return -1;
    }

    va_start(args, format);
    write_len = vsnprintf(buffer + *offset, size - *offset, format, args);
    va_end(args);
    if ((write_len < 0) || ((uint32_t)write_len >= (size - *offset))) {
        return -1;
    }

    *offset += (uint32_t)write_len;
    return 0;
}

static int qwen_wss_make_key(char *key, uint32_t key_size)
{
    uint8_t nonce[16];
    size_t out_len;
    uint32_t index;

    if ((key == NULL) || (key_size < 32U)) {
        return -1;
    }

    for (index = 0U; index < sizeof(nonce); index++) {
        nonce[index] = (uint8_t)(util_random() & 0xFFU);
    }

    out_len = 0U;
    if (mbedtls_base64_encode((unsigned char *)key, key_size, &out_len, nonce, sizeof(nonce)) != 0) {
        return -1;
    }

    key[out_len] = '\0';
    return 0;
}

static uint32_t qwen_wss_trim_line_endings(const char *text)
{
    uint32_t text_len;

    if (text == NULL) {
        return 0U;
    }

    text_len = (uint32_t)strlen(text);
    while ((text_len > 0U) &&
           ((text[text_len - 1U] == '\r') || (text[text_len - 1U] == '\n'))) {
        text_len--;
    }

    return text_len;
}

static char qwen_wss_ascii_lower(char value)
{
    if ((value >= 'A') && (value <= 'Z')) {
        return (char)(value + ('a' - 'A'));
    }

    return value;
}

static uint8_t qwen_wss_equal_ignore_case(const char *left, const char *right, uint32_t size)
{
    uint32_t index;

    if ((left == NULL) || (right == NULL)) {
        return 0U;
    }

    for (index = 0U; index < size; index++) {
        if (qwen_wss_ascii_lower(left[index]) != qwen_wss_ascii_lower(right[index])) {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t qwen_wss_contains_ignore_case(const char *text, uint32_t text_size, const char *needle)
{
    uint32_t needle_size;
    uint32_t offset;

    if ((text == NULL) || (needle == NULL)) {
        return 0U;
    }

    needle_size = (uint32_t)strlen(needle);
    if ((needle_size == 0U) || (needle_size > text_size)) {
        return 0U;
    }

    for (offset = 0U; offset <= (text_size - needle_size); offset++) {
        if (qwen_wss_equal_ignore_case(text + offset, needle, needle_size) != 0U) {
            return 1U;
        }
    }

    return 0U;
}

static const char *qwen_wss_find_header_value(const char *response, const char *header_name, uint32_t *value_size)
{
    const char *line;
    const char *line_end;
    const char *value_start;
    uint32_t header_name_size;
    uint32_t line_size;

    if ((response == NULL) || (header_name == NULL) || (value_size == NULL)) {
        return NULL;
    }

    header_name_size = (uint32_t)strlen(header_name);
    line = response;
    while (line[0] != '\0') {
        line_end = strstr(line, "\r\n");
        line_size = (uint32_t)((line_end != NULL) ? (line_end - line) : strlen(line));
        if ((line_size > header_name_size) && (line[header_name_size] == ':') &&
            (qwen_wss_equal_ignore_case(line, header_name, header_name_size) != 0U)) {
            value_start = line + header_name_size + 1U;
            while ((value_start < (line + line_size)) &&
                   ((value_start[0] == ' ') || (value_start[0] == '\t'))) {
                value_start++;
            }
            *value_size = (uint32_t)((line + line_size) - value_start);
            return value_start;
        }
        if (line_end == NULL) {
            break;
        }
        line = line_end + 2U;
    }

    return NULL;
}

static uint8_t qwen_wss_header_contains(const char *response, const char *header_name, const char *needle)
{
    const char *value;
    uint32_t value_size;

    value_size = 0U;
    value = qwen_wss_find_header_value(response, header_name, &value_size);
    if (value == NULL) {
        return 0U;
    }

    return qwen_wss_contains_ignore_case(value, value_size, needle);
}

static uint8_t qwen_wss_header_equals(const char *response, const char *header_name, const char *expected_value)
{
    const char *value;
    uint32_t value_size;
    uint32_t expected_size;

    value_size = 0U;
    value = qwen_wss_find_header_value(response, header_name, &value_size);
    if ((value == NULL) || (expected_value == NULL)) {
        return 0U;
    }

    while ((value_size > 0U) && ((value[value_size - 1U] == ' ') || (value[value_size - 1U] == '\t'))) {
        value_size--;
    }

    expected_size = (uint32_t)strlen(expected_value);
    if (value_size != expected_size) {
        return 0U;
    }

    return (memcmp(value, expected_value, expected_size) == 0) ? 1U : 0U;
}

static int qwen_wss_append_header_block(char *request, uint32_t size, uint32_t *offset, const char *header)
{
    uint32_t header_len;

    if ((request == NULL) || (offset == NULL) || (*offset >= size)) {
        return -1;
    }

    header_len = qwen_wss_trim_line_endings(header);
    if (header_len > 0U) {
        if (qwen_wss_append(request, size, offset, "%.*s", (int)header_len, header) != 0) {
            return -1;
        }
    }

    return qwen_wss_append(request, size, offset, "\r\n\r\n");
}

static int qwen_wss_build_request(char *request,
                                  uint32_t request_size,
                                  const qwen_wss_request_param_t *request_param)
{
    uint32_t offset;

    if ((request == NULL) || (request_param == NULL) || (request_param->ws_key == NULL) ||
        qwen_wss_is_empty(request_param->host) || qwen_wss_is_empty(request_param->api) ||
        qwen_wss_is_empty(request_param->header)) {
        return -1;
    }

    if (qwen_wss_make_key(request_param->ws_key, request_param->ws_key_size) != 0) {
        return -1;
    }

    offset = 0U;
    if (qwen_wss_append(request, request_size, &offset, "GET %s HTTP/1.1\r\n", request_param->api) != 0) return -1;
    if (qwen_wss_append(request, request_size, &offset, "Host: %s\r\n", request_param->host) != 0) return -1;
    if (qwen_wss_append(request, request_size, &offset, "Upgrade: websocket\r\n") != 0) return -1;
    if (qwen_wss_append(request, request_size, &offset, "Connection: Upgrade\r\n") != 0) return -1;
    if (qwen_wss_append(request, request_size, &offset, "Sec-WebSocket-Key: %s\r\n", request_param->ws_key) != 0) return -1;
    if (qwen_wss_append(request, request_size, &offset, "Sec-WebSocket-Version: 13\r\n") != 0) return -1;
    return qwen_wss_append_header_block(request, request_size, &offset, request_param->header);
}

static int qwen_wss_run_tls_handshake(struct websocket_struct *ws_info)
{
    int ret;
    const char *host;

    if (ws_info == NULL) {
        return -1;
    }

    host = qwen_net_get_wss_host();
    if (qwen_wss_is_empty(host) != 0) {
        return -1;
    }

    websocket_mbedtls_client_set_timeout(&ws_info->websockets_mbtls_info,
                                         D_QWEN_HTTP_TIMEOUT_MS,
                                         D_QWEN_HTTP_TIMEOUT_MS);
    ret = mbedtls_ssl_set_hostname(&ws_info->websockets_mbtls_info.ssl, host);
    if (ret != 0) {
        LOGE(TAG, "tls set hostname failed: ret=%d host=%s", ret, host);
        return -1;
    }

    ret = websockets_mbtls_client_connect(&ws_info->websockets_mbtls_info, (u8 *)host, (int)ws_info->port);
    if (ret == 0) {
        LOGE(TAG, "tls connect failed: host=%s port=%u", host, (unsigned)ws_info->port);
        return -1;
    }

    if (websockets_mbtls_client_handshack(&ws_info->websockets_mbtls_info) == 0) {
        return -1;
    }

    return 0;
}

static int qwen_wss_ssl_write_all(struct websocket_struct *ws_info, const uint8_t *payload, uint32_t payload_size)
{
    int write_len;
    uint32_t offset;

    if ((ws_info == NULL) || (payload == NULL) || (payload_size == 0U)) {
        return -1;
    }

    offset = 0U;
    while (offset < payload_size) {
        write_len = mbedtls_ssl_write(&ws_info->websockets_mbtls_info.ssl, payload + offset, payload_size - offset);
        if (write_len > 0) {
            offset += (uint32_t)write_len;
            continue;
        }
        if ((write_len == MBEDTLS_ERR_SSL_WANT_READ) || (write_len == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            continue;
        }
        return write_len;
    }

    return 0;
}

static int qwen_wss_ssl_read_response(struct websocket_struct *ws_info, char *response, uint32_t response_size)
{
    int read_len;
    uint32_t offset;

    if ((ws_info == NULL) || (response == NULL) || (response_size < 2U)) {
        return -1;
    }

    memset(response, 0, response_size);
    offset = 0U;
    while (offset < (response_size - 1U)) {
        read_len = mbedtls_ssl_read(&ws_info->websockets_mbtls_info.ssl,
                                    (unsigned char *)(response + offset),
                                    response_size - 1U - offset);
        if (read_len > 0) {
            offset += (uint32_t)read_len;
            response[offset] = '\0';
            if (strstr(response, "\r\n\r\n") != NULL) {
                return (int)offset;
            }
            continue;
        }
        if ((read_len == MBEDTLS_ERR_SSL_WANT_READ) || (read_len == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            continue;
        }
        return read_len;
    }

    return -1;
}

static int qwen_wss_calc_accept(const char *ws_key, char *accept, uint32_t accept_size)
{
    mbedtls_sha1_context sha1_ctx;
    unsigned char digest[QWEN_WSS_SHA1_LEN];
    size_t out_len;
    int ret;

    if ((ws_key == NULL) || (accept == NULL) || (accept_size < QWEN_WSS_ACCEPT_MAX_LEN)) {
        return -1;
    }

    mbedtls_sha1_init(&sha1_ctx);
    ret = mbedtls_sha1_starts(&sha1_ctx);
    if (ret == 0) {
        ret = mbedtls_sha1_update(&sha1_ctx, (const unsigned char *)ws_key, strlen(ws_key));
    }
    if (ret == 0) {
        ret = mbedtls_sha1_update(&sha1_ctx, (const unsigned char *)QWEN_WSS_GUID, strlen(QWEN_WSS_GUID));
    }
    if (ret == 0) {
        ret = mbedtls_sha1_finish(&sha1_ctx, digest);
    }
    mbedtls_sha1_free(&sha1_ctx);
    if (ret != 0) {
        return -1;
    }

    out_len = 0U;
    if (mbedtls_base64_encode((unsigned char *)accept, accept_size, &out_len,
                              digest, sizeof(digest)) != 0) {
        return -1;
    }
    accept[out_len] = '\0';
    return 0;
}

static int qwen_wss_validate_handshake_response(const char *response, const char *ws_key)
{
    char expected_accept[QWEN_WSS_ACCEPT_MAX_LEN];
    char accept_header[QWEN_WSS_ACCEPT_MAX_LEN + 32U];

    if ((response == NULL) || (ws_key == NULL)) {
        return -1;
    }

    if (strstr(response, "101 Switching Protocols") == NULL) {
        LOGE(TAG, "handshake status invalid: %.12s", response);
        return -1;
    }
    if (qwen_wss_header_contains(response, "Upgrade", "websocket") == 0U) {
        LOGE(TAG, "handshake upgrade missing");
        return -1;
    }
    if (qwen_wss_header_contains(response, "Connection", "upgrade") == 0U) {
        LOGE(TAG, "handshake connection missing");
        return -1;
    }
    if (qwen_wss_calc_accept(ws_key, expected_accept, sizeof(expected_accept)) != 0) {
        LOGE(TAG, "handshake accept calc failed");
        return -1;
    }

    snprintf(accept_header, sizeof(accept_header), "%s", expected_accept);
    if (qwen_wss_header_equals(response, "Sec-WebSocket-Accept", accept_header) == 0U) {
        LOGE(TAG, "handshake accept invalid");
        return -1;
    }

    return 0;
}

static int qwen_wss_custom_handshack(struct websocket_struct *ws_info)
{
    char request[QWEN_WSS_HANDSHAKE_BUF_SIZE];
    char response[QWEN_WSS_RESPONSE_BUF_SIZE];
    char ws_key[QWEN_WSS_KEY_MAX_LEN];
    qwen_wss_request_param_t request_param;
    const char *header;
    const char *host;
    const char *api;
    uint32_t request_len;
    int ret;

    if (ws_info == NULL) {
        return 0;
    }

    header = qwen_net_get_wss_header();
    host = qwen_net_get_wss_host();
    api = qwen_net_get_wss_api();
    request_param.host = host;
    request_param.api = api;
    request_param.header = header;
    request_param.ws_key = ws_key;
    request_param.ws_key_size = sizeof(ws_key);
    if (qwen_wss_build_request(request, sizeof(request), &request_param) != 0) {
        LOGE(TAG, "build request failed");
        return 0;
    }

    ret = qwen_wss_run_tls_handshake(ws_info);
    if (ret != 0) {
        LOGE(TAG, "tls handshake failed");
        return 0;
    }

    request_len = (uint32_t)strlen(request);
    ret = qwen_wss_ssl_write_all(ws_info, (const uint8_t *)request, request_len);
    if (ret != 0) {
        LOGE(TAG, "write handshake failed: ret=%d size=%u", ret, (unsigned)request_len);
        return 0;
    }

    ret = qwen_wss_ssl_read_response(ws_info, response, sizeof(response));
    if (ret <= 0) {
        LOGE(TAG, "read handshake response failed: ret=%d", ret);
        return 0;
    }

    if (qwen_wss_validate_handshake_response(response, ws_key) != 0) {
        LOGE(TAG, "unexpected handshake response[%d]", ret);
        return 0;
    }

    websocket_msg_clear(ws_info);
    ws_info->websocket_valid = ESTABLISHED;
    return 1;
}

static int qwen_wss_exit_notify(struct websocket_struct *ws_info)
{
    (void)ws_info;
    qwen_wss_notify_unexpected_disconnect();
    return 0;
}

static void qwen_wss_recv_callback(u8 *payload, u32 payload_size, u8 type, void *priv)
{
    uint8_t opcode;

    (void)priv;
    if ((type == WCT_DISCONN) || (type == WCT_CLOSE_OK)) {
        LOGW(TAG, "recv disconnect event type=%u", (unsigned)type);
        qwen_wss_notify_unexpected_disconnect();
        return;
    }

    opcode = (uint8_t)(type & 0x0FU);
    if (payload_size != 0U) {
        c_mmi_analyze_recv_data(opcode, payload, payload_size);
    }
}

static void qwen_wss_register_client(struct websocket_struct *ws_info)
{
    memset(ws_info, 0, sizeof(*ws_info));
    ws_info->_init = websockets_client_socket_init;
    ws_info->_exit = websockets_client_socket_exit;
    ws_info->_handshack = qwen_wss_custom_handshack;
    ws_info->_send = websockets_client_socket_send;
    ws_info->_recv_thread = websockets_client_socket_recv_thread;
    ws_info->_recv_cb = qwen_wss_recv_callback;
    ws_info->_recv = NULL;
    ws_info->_exit_notify = qwen_wss_exit_notify;
    ws_info->websocket_mode = WEBSOCKETS_MODE;
    ws_info->recv_time_out = 1000U;
    ws_info->recv_buf = NULL;
    ws_info->recv_buf_size = QWEN_WSS_RECV_BUF_SIZE;
    ws_info->origin_str = "http://ac791-device";
}

static int qwen_wss_fill_connect_info(struct websocket_struct *ws_info,
                                       char *url,
                                       uint32_t url_size,
                                       const char *host,
                                       int port_value,
                                       const char *api)
{
    int write_len;

    if ((ws_info == NULL) || (url == NULL) || (url_size < 8U) ||
        qwen_wss_is_empty(host) || qwen_wss_is_empty(api) || (port_value <= 0)) {
        return -1;
    }

    write_len = snprintf(url, url_size, "wss://%s:%d%s", host, port_value, api);
    if ((write_len <= 0) || ((uint32_t)write_len >= url_size)) {
        return -1;
    }

    ws_info->ip_or_url = (u8 *)url;
    ws_info->port = (u16)port_value;
    strncpy((char *)ws_info->req_head.file, api, sizeof(ws_info->req_head.file) - 1U);
    strncpy((char *)ws_info->req_head.host, host, sizeof(ws_info->req_head.host) - 1U);
    ws_info->req_head.file[sizeof(ws_info->req_head.file) - 1U] = '\0';
    ws_info->req_head.host[sizeof(ws_info->req_head.host) - 1U] = '\0';
    return 0;
}

static int qwen_wss_send_frame(uint8_t opcode, uint8_t *payload, uint32_t payload_size)
{
    int send_len;

    if (os_mutex_pend(&s_qwen_wss_ctx.op_mutex, 0) != OS_NO_ERR) {
        return -1;
    }

    if ((s_qwen_wss_ctx.is_connected == 0U) || (s_qwen_wss_ctx.ws_info.websocket_valid != ESTABLISHED)) {
        os_mutex_post(&s_qwen_wss_ctx.op_mutex);
        return -1;
    }

    send_len = s_qwen_wss_ctx.ws_info._send(&s_qwen_wss_ctx.ws_info, payload, (int)payload_size, (char)opcode);
    os_mutex_post(&s_qwen_wss_ctx.op_mutex);
    return send_len;
}

static void qwen_wss_send_task(void *priv)
{
    uint8_t opcode;
    uint8_t send_buf[QWEN_WSS_SEND_BUF_SIZE];
    uint32_t send_size;

    (void)priv;
    while (s_qwen_wss_ctx.is_connected != 0U) {
        send_size = c_mmi_get_send_data(&opcode, send_buf, sizeof(send_buf));
        if (send_size == 0U) {
            websockets_sleep(10);
            continue;
        }
        if (qwen_wss_send_frame(opcode, send_buf, send_size) <= 0) {
            LOGE(TAG, "send frame failed: opcode=%u size=%u",
                 (unsigned)opcode, (unsigned)send_size);
            qwen_wss_notify_unexpected_disconnect();
            break;
        }
    }
}

static d_err_t qwen_wss_start_threads(struct websocket_struct *ws_info)
{
    if (thread_fork("qwen_ws_recv", QWEN_WSS_TASK_PRIO, QWEN_WSS_TASK_STACK, 0,
                    &ws_info->recv_thread_id, ws_info->_recv_thread, ws_info) != OS_NO_ERR) {
        return D_ERR_IO;
    }

    if (thread_fork("qwen_ws_send", QWEN_WSS_TASK_PRIO, QWEN_WSS_TASK_STACK, 0,
                    &s_qwen_wss_ctx.send_thread_id, qwen_wss_send_task, NULL) != OS_NO_ERR) {
        thread_kill(&ws_info->recv_thread_id, KILL_WAIT);
        ws_info->recv_thread_id = 0;
        return D_ERR_IO;
    }

    return D_OK;
}

static void qwen_wss_stop_threads(void)
{
    if (s_qwen_wss_ctx.send_thread_id != 0) {
        thread_kill(&s_qwen_wss_ctx.send_thread_id, KILL_WAIT);
        s_qwen_wss_ctx.send_thread_id = 0;
    }

    if (s_qwen_wss_ctx.ws_info.recv_thread_id != 0) {
        thread_kill(&s_qwen_wss_ctx.ws_info.recv_thread_id, KILL_WAIT);
        s_qwen_wss_ctx.ws_info.recv_thread_id = 0;
    }
}

static void qwen_wss_signal_stop(void)
{
    struct websocket_struct *ws = &s_qwen_wss_ctx.ws_info;
    void *socket;

    if (ws->websocket_valid == NOT_ESTABLISHED) return;
    ws->websocket_valid = INVALID_ESTABLISHED;
    socket = ws->websocket_mode == WEBSOCKETS_MODE ?
             (void *)ws->websockets_mbtls_info.server_fd.hdl : ws->sk_fd;
    /* Wake socket I/O without freeing buffers that the receive task still owns. */
    if (socket != NULL) sock_set_quit(socket);
}

static void qwen_wss_close_socket(void)
{
    if (s_qwen_wss_ctx.ws_info.websocket_valid == NOT_ESTABLISHED) {
        return;
    }

    websockets_client_notify_disconnet_to_server(&s_qwen_wss_ctx.ws_info);
    if (s_qwen_wss_ctx.ws_info._exit != NULL) {
        s_qwen_wss_ctx.ws_info._exit(&s_qwen_wss_ctx.ws_info);
    }
    s_qwen_wss_ctx.ws_info.websocket_valid = NOT_ESTABLISHED;
}

static d_err_t qwen_wss_open_locked(const char *host, int port_value, const char *api)
{
    d_err_t err;

    qwen_wss_register_client(&s_qwen_wss_ctx.ws_info);
    if (qwen_wss_fill_connect_info(&s_qwen_wss_ctx.ws_info, s_qwen_wss_ctx.url,
                                    sizeof(s_qwen_wss_ctx.url), host, port_value, api) != 0) {
        LOGE(TAG, "build url failed");
        return D_ERR_INVALID_ARG;
    }
    websockets_mbtls_client_set_ca(&s_qwen_wss_ctx.ws_info.websockets_mbtls_info,
                                   (char *)s_qwen_wss_ca_pem, sizeof(s_qwen_wss_ca_pem));
    if (websockets_struct_check(sizeof(struct websocket_struct)) == 0) {
        LOGE(TAG, "websocket struct check failed");
        return D_ERR_IO;
    }
    if (s_qwen_wss_ctx.ws_info._init(&s_qwen_wss_ctx.ws_info) == 0) {
        LOGE(TAG, "socket init failed");
        return D_ERR_NETWORK;
    }
    if (s_qwen_wss_ctx.ws_info._handshack(&s_qwen_wss_ctx.ws_info) == 0) {
        LOGE(TAG, "handshake failed");
        s_qwen_wss_ctx.ws_info._exit(&s_qwen_wss_ctx.ws_info);
        return D_ERR_NETWORK;
    }

    s_qwen_wss_ctx.is_connected = 1U;
    err = qwen_wss_start_threads(&s_qwen_wss_ctx.ws_info);
    if (err != D_OK) {
        qwen_wss_mark_disconnected();
        qwen_wss_close_socket();
        return err;
    }

    LOGI(TAG, "wss connected recv_buf_size=%u", (unsigned)s_qwen_wss_ctx.ws_info.recv_buf_size);

    return D_OK;
}

/**
 * @brief 建立 Qwen WSS 连接并启动收发线程
 * @return D_OK 表示成功，其他错误码表示失败
 */
d_err_t qwen_wss_connect(void)
{
    const char *host;
    const char *port_str;
    const char *api;
    int port_value;
    d_err_t err;

    host = qwen_net_get_wss_host();
    port_str = qwen_net_get_wss_port();
    api = qwen_net_get_wss_api();
    if (qwen_wss_is_empty(host) || qwen_wss_is_empty(port_str) || qwen_wss_is_empty(api)) {
        LOGE(TAG, "connect params not ready");
        return D_ERR_NOT_READY;
    }

    port_value = qwen_wss_get_port_value(port_str);
    if (port_value < 0) {
        LOGE(TAG, "invalid port: %s", port_str);
        return D_ERR_INVALID_ARG;
    }

    if (!os_mutex_valid(&s_qwen_wss_ctx.op_mutex)) {
        os_mutex_create(&s_qwen_wss_ctx.op_mutex);
    }
    if (!os_mutex_valid(&s_qwen_wss_ctx.op_mutex)) {
        return D_ERR_IO;
    }
    if (os_mutex_pend(&s_qwen_wss_ctx.op_mutex, 0) != OS_NO_ERR) {
        return D_ERR_BUSY;
    }
    if (s_qwen_wss_ctx.closing) {
        os_mutex_post(&s_qwen_wss_ctx.op_mutex);
        return D_ERR_BUSY;
    }
    if (s_qwen_wss_ctx.is_connected != 0U) {
        LOGI(TAG, "already connected");
        os_mutex_post(&s_qwen_wss_ctx.op_mutex);
        return D_OK;
    }

    s_transport_generation++;
    s_qwen_wss_ctx.connection_generation = s_transport_generation;
    err = qwen_wss_open_locked(host, port_value, api);
    os_mutex_post(&s_qwen_wss_ctx.op_mutex);
    if (err == D_OK) {
        websockets_sleep(100);
    } else {
        LOGE(TAG, "connect failed: %d", err);
    }
    return err;
}

void qwen_wss_set_disconnect_callback(qwen_wss_disconnect_callback_t callback)
{
    s_qwen_wss_ctx.disconnect_callback = callback;
}

/**
 * @brief 断开 Qwen WSS 连接并释放运行时资源
 */
void qwen_wss_disconnect(void)
{
    (void)qwen_wss_disconnect_checked();
}

d_err_t qwen_wss_disconnect_checked(void)
{
    s_transport_generation++;
    if (!os_mutex_valid(&s_qwen_wss_ctx.op_mutex)) {
        qwen_wss_mark_disconnected();
        qwen_wss_stop_threads();
        return D_OK;
    }
    if (os_mutex_pend(&s_qwen_wss_ctx.op_mutex, 0) != OS_NO_ERR) {
        return D_ERR_BUSY;
    }
    if (s_qwen_wss_ctx.closing) {
        os_mutex_post(&s_qwen_wss_ctx.op_mutex);
        return D_ERR_BUSY;
    }
    s_qwen_wss_ctx.closing = 1U;
    qwen_wss_mark_disconnected();
    qwen_wss_signal_stop();
    os_mutex_post(&s_qwen_wss_ctx.op_mutex);
    /* _exit frees TLS and receive buffers; every user must have exited first. */
    qwen_wss_stop_threads();
    if (os_mutex_pend(&s_qwen_wss_ctx.op_mutex, 0) != OS_NO_ERR) {
        return D_ERR_BUSY;
    }
    LOGI(TAG, "transport joined: recv=%d send=%d; release socket",
         s_qwen_wss_ctx.ws_info.recv_thread_id, s_qwen_wss_ctx.send_thread_id);
    qwen_wss_close_socket();
    s_qwen_wss_ctx.closing = 0U;
    os_mutex_post(&s_qwen_wss_ctx.op_mutex);
    LOGI(TAG, "disconnected");
    return D_OK;
}

/**
 * @brief 查询当前 WSS 是否已连接
 * @return 1 表示已连接，0 表示未连接
 */
uint32_t qwen_wss_generation(void)
{
    return s_transport_generation;
}

int qwen_wss_is_connected(void)
{
    if ((s_qwen_wss_ctx.is_connected != 0U) && (s_qwen_wss_ctx.ws_info.websocket_valid == ESTABLISHED)) {
        return 1;
    }

    return 0;
}

#endif
