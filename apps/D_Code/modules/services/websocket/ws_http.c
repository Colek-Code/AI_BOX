/** @file ws_http.c @brief 严格校验 HTTP/1.1 WebSocket Upgrade，不依赖平台封装。 */
#include "ws_http.h"
#include "framework/compat/d_stdio_compat.h"
#include <string.h>

/**
 * @brief 按 ASCII 不区分大小写比较固定长度字段。
 * @param[in] a 第一段至少 n 字节的字符。
 * @param[in] b 第二段至少 n 字节的字符。
 * @param[in] n 比较字节数。
 * @return 相等返回 1，否则返回 0。
 */
static int ws_equal(const char *a, const char *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) {
        unsigned x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

/**
 * @brief 检查零结尾字段不含控制字符。
 * @param[in] s 待检查字符串。
 * @return 非空且没有控制字符返回 1，否则返回 0。
 */
static int ws_clean(const char *s)
{
    if (!s) return 0;
    while (*s) { if ((unsigned char)*s < 32 || (unsigned char)*s == 127) return 0; ++s; }
    return 1;
}

/**
 * @brief 解析受限的 ws 或 wss URL；完整合同见 ws_http.h。
 */
int ws_url_parse(const char *url, ws_url_t *out)
{
    const char *p, *end, *colon, *q;
    unsigned port = 0, n;
    if (!url || !out || !ws_clean(url) || strlen(url) >= WS_URL_CAPACITY) return -1;
    memset(out, 0, sizeof(*out));
    if (!strncmp(url, "wss://", 6)) { p = url + 6; out->secure = 1; }
    else if (!strncmp(url, "ws://", 5)) p = url + 5;
    else return -1;
    if (strchr(p, '@') || strchr(p, '#') || strchr(p, ' ') || strchr(p, '[') || strchr(p, '\\')) return -1;
    end = p;
    while (*end && *end != '/' && *end != '?') ++end;
    colon = NULL;
    for (q = p; q < end; ++q) if (*q == ':') { if (colon) return -1; colon = q; }
    n = (unsigned)((colon ? colon : end) - p);
    if (!n || n >= sizeof(out->host)) return -1;
    for (q = p; q < p + n; ++q)
        if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
              (*q >= '0' && *q <= '9') || *q == '-' || *q == '.')) return -1;
    memcpy(out->host, p, n);
    if (colon) {
        if (colon + 1 == end) return -1;
        for (q = colon + 1; q < end; ++q) {
            if (*q < '0' || *q > '9') return -1;
            port = port * 10U + (unsigned)(*q - '0');
            if (port > 65535U) return -1;
        }
        if (!port) return -1;
    } else port = out->secure ? 443 : 80;
    snprintf(out->port, sizeof(out->port), "%u", port);
    if (!*end) strcpy(out->path, "/");
    else if (*end == '?') {
        if (strlen(end) + 2 > sizeof(out->path)) return -1;
        out->path[0] = '/'; strcpy(out->path + 1, end);
    } else strcpy(out->path, end);
    return 0;
}

/**
 * @brief 清空 Upgrade 头累积状态；完整合同见 ws_http.h。
 */
void ws_http_init(ws_http_t *h) { memset(h, 0, sizeof(*h)); }

/**
 * @brief 有界累积 HTTP 头，并保留头后 WebSocket 字节给调用者；完整合同见 ws_http.h。
 */
int ws_http_feed(ws_http_t *h, const uint8_t *data, unsigned length, unsigned *used)
{
    if (!used) return -1;
    *used = 0;
    if (!h || (!data && length)) return -1;
    while (*used < length && !h->complete) {
        uint8_t c = data[(*used)++];
        if (!c || (c < 32 && c != '\r' && c != '\n' && c != '\t') || c == 127 ||
            h->used + 1 >= sizeof(h->bytes)) return -1;
        if (c == '\n' && (!h->used || h->bytes[h->used - 1] != '\r')) return -1;
        if (h->used && h->bytes[h->used - 1] == '\r' && c != '\n') return -1;
        h->bytes[h->used++] = (char)c;
        h->bytes[h->used] = 0;
        if (h->used >= 4 && !memcmp(h->bytes + h->used - 4, "\r\n\r\n", 4)) h->complete = 1;
    }
    return h->complete;
}

/**
 * @brief 读取唯一的指定头字段，拒绝折叠或重复关键头。
 * @param[in] header 非空完整请求或应答头。
 * @param[in] name 目标字段名。
 * @param[out] out 字段值输出，去除两侧空白。
 * @param[in] capacity 正数输出容量。
 * @return 找到一个返回 1，没有返回 0；格式、重复或容量错误返回 -1。
 */
static int ws_header(const char *header, const char *name, char *out, unsigned capacity)
{
    const char *p = strstr(header, "\r\n");
    unsigned matches = 0, key = (unsigned)strlen(name);
    if (!p) return -1;
    p += 2; out[0] = 0;
    while (*p && *p != '\r') {
        const char *end = strstr(p, "\r\n"), *colon = strchr(p, ':'), *v;
        unsigned n;
        if (!end || !colon || colon >= end || *p == ' ' || *p == '\t') return -1;
        if ((unsigned)(colon - p) == key && ws_equal(p, name, key)) {
            if (++matches != 1) return -1;
            v = colon + 1;
            while (v < end && (*v == ' ' || *v == '\t')) ++v;
            while (end > v && (end[-1] == ' ' || end[-1] == '\t')) --end;
            n = (unsigned)(end - v);
            if (n >= capacity) return -1;
            memcpy(out, v, n); out[n] = 0;
        }
        p = strstr(p, "\r\n") + 2;
    }
    return (int)matches;
}

/**
 * @brief 在逗号分隔列表中按 ASCII 不区分大小写匹配 token。
 * @param[in] value 零结尾字段值。
 * @param[in] token 零结尾目标 token。
 * @return 找到返回 1，否则返回 0。
 */
static int ws_token(const char *value, const char *token)
{
    unsigned n = (unsigned)strlen(token);
    while (*value) {
        const char *end;
        while (*value == ' ' || *value == '\t' || *value == ',') ++value;
        end = value; while (*end && *end != ',') ++end;
        while (end > value && (end[-1] == ' ' || end[-1] == '\t')) --end;
        if ((unsigned)(end - value) == n && ws_equal(value, token, n)) return 1;
        value = strchr(value, ','); if (!value) break;
        ++value;
    }
    return 0;
}

/**
 * @brief 检查 Upgrade、Connection 与无实体正文约束。
 * @param[in] header 完整零结尾 HTTP 头。
 * @return 有效返回 0，否则返回 -1。
 */
static int ws_upgrade(const char *header)
{
    char value[256];
    if (ws_header(header, "Upgrade", value, sizeof(value)) != 1 || !ws_token(value, "websocket")) return -1;
    if (ws_header(header, "Connection", value, sizeof(value)) != 1 || !ws_token(value, "Upgrade")) return -1;
    if (ws_header(header, "Transfer-Encoding", value, sizeof(value)) != 0) return -1;
    if (ws_header(header, "Content-Length", value, sizeof(value)) < 0 ||
        (value[0] && strcmp(value, "0"))) return -1;
    return 0;
}

/**
 * @brief 格式化 HTTP/1.1 WebSocket 客户端 Upgrade 请求；完整合同见 ws_http.h。
 */
int ws_http_client_request(const ws_url_t *u, const char *key, const char *origin,
                           const char *agent, char *out, unsigned cap)
{
    int n;
    if (!u || !key || !ws_clean(origin) || !ws_clean(agent) || !out) return -1;
    n = snprintf(out, cap, "GET %s HTTP/1.1\r\nHost: %s:%s\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n%s%s%s%s%s%s\r\n",
        u->path, u->host, u->port, key, origin[0] ? "Origin: " : "", origin,
        origin[0] ? "\r\n" : "", agent[0] ? "User-Agent: " : "", agent, agent[0] ? "\r\n" : "");
    return n > 0 && (unsigned)n < cap ? n : -1;
}

/**
 * @brief 核对服务端 101 应答、Upgrade 字段及预期 accept；完整合同见 ws_http.h。
 */
int ws_http_validate_client(const char *header, const char *accept)
{
    char value[256];
    if (strncmp(header, "HTTP/1.1 101", 12) || (header[12] != ' ' && header[12] != '\r') || ws_upgrade(header)) return -1;
    if (ws_header(header, "Sec-WebSocket-Accept", value, sizeof(value)) != 1 || strcmp(value, accept)) return -1;
    if (ws_header(header, "Sec-WebSocket-Extensions", value, sizeof(value)) != 0 ||
        ws_header(header, "Sec-WebSocket-Protocol", value, sizeof(value)) != 0) return -1;
    return 0;
}

/**
 * @brief 校验客户端请求路径和握手头，提取规范 key；完整合同见 ws_http.h。
 */
int ws_http_server_key(const char *header, const char *path, char key[25])
{
    static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char value[256];
    const char *end;
    unsigned i;
    if (strncmp(header, "GET ", 4)) return -1;
    end = strchr(header + 4, ' ');
    if (!end || (unsigned)(end - header - 4) != strlen(path) ||
        memcmp(header + 4, path, strlen(path)) || strncmp(end, " HTTP/1.1\r\n", 11) || ws_upgrade(header)) return -1;
    if (ws_header(header, "Host", value, sizeof(value)) != 1 || !value[0]) return -1;
    if (ws_header(header, "Sec-WebSocket-Version", value, sizeof(value)) != 1 || strcmp(value, "13")) return -1;
    if (ws_header(header, "Sec-WebSocket-Key", key, 25) != 1 || strlen(key) != 24 || strcmp(key + 22, "==")) return -1;
    for (i = 0; i < 22; ++i) if (!strchr(base64, key[i])) return -1;
    if ((strchr(base64, key[21]) - base64) & 15) return -1;
    return 0;
}

/**
 * @brief 格式化服务端 101 Upgrade 应答；完整合同见 ws_http.h。
 */
int ws_http_server_response(const char *accept, char *out, unsigned capacity)
{
    int n = snprintf(out, capacity, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    return n > 0 && (unsigned)n < capacity ? n : -1;
}
