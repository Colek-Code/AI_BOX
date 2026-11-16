/** @file ws_http.h @brief 有界 Upgrade 头解析与格式化，保留头后的帧字节。 */
#ifndef D_WS_HTTP_H
#define D_WS_HTTP_H
#include <stdint.h>
#define WS_HTTP_CAPACITY 2048U
#define WS_URL_CAPACITY 1024U
typedef struct {
    char host[128], path[WS_URL_CAPACITY], port[6];
    uint8_t secure;
} ws_url_t;
typedef struct { char bytes[WS_HTTP_CAPACITY]; unsigned used; uint8_t complete; } ws_http_t;
/**
 * @brief 解析受限的 ws 或 wss URL。
 * @param[in] url 以零结尾的完整 URL，不支持用户信息、片段或 IPv6 字面量。
 * @param[out] out 主机、路径、端口与安全模式输出；失败时内容可能已改。
 * @return 成功返回 0；协议、主机、端口、字符或容量无效返回 -1。
 */
int ws_url_parse(const char *url, ws_url_t *out);
/**
 * @brief 清空 Upgrade 头累积状态。
 * @param[out] http 非空解析对象。
 */
void ws_http_init(ws_http_t *http);
/**
 * @brief 有界累积 HTTP 头，并保留头后 WebSocket 字节给调用者。
 * @param[in,out] http 已初始化的累积状态。
 * @param[in] data 本次输入；长度为 0 时允许为空。
 * @param[in] length 输入字节数。
 * @param[out] consumed 非空输出，本次消费数，不含后续帧字节。
 * @return 头完整返回 1，尚不完整返回 0，字符或容量无效返回 -1。
 */
int ws_http_feed(ws_http_t *http, const uint8_t *data, unsigned length, unsigned *consumed);
/**
 * @brief 格式化 HTTP/1.1 WebSocket 客户端 Upgrade 请求。
 * @param[in] url 已解析 URL。
 * @param[in] key 客户端已生成的握手 key。
 * @param[in] origin 以零结尾的 Origin 值，空串表示省略。
 * @param[in] agent 以零结尾的 User-Agent 值，空串表示省略。
 * @param[out] out 请求输出缓冲。
 * @param[in] capacity 缓冲容量，包含末尾零。
 * @return 成功返回请求字节数，不含末尾零；参数或容量不足返回 -1。
 */
int ws_http_client_request(const ws_url_t *url, const char *key, const char *origin,
                           const char *agent, char *out, unsigned capacity);
/**
 * @brief 核对服务端 101 应答、Upgrade 字段及预期 accept。
 * @param[in] header 已完整、零结尾的 HTTP 应答头。
 * @param[in] accept 本次握手预期的 accept 值。
 * @return 全部满足返回 0；状态、重复头、扩展或 accept 不符返回 -1。
 */
int ws_http_validate_client(const char *header, const char *accept);
/**
 * @brief 校验客户端请求路径和握手头，提取规范 key。
 * @param[in] header 已完整、零结尾的请求头。
 * @param[in] path 要求精确匹配的服务路径。
 * @param[out] key 至少 25 字节的 key 输出。
 * @return 请求有效返回 0，否则返回 -1。
 */
int ws_http_server_key(const char *header, const char *path, char key[25]);
/**
 * @brief 格式化服务端 101 Upgrade 应答。
 * @param[in] accept 已生成的非空 accept 值。
 * @param[out] out 输出缓冲。
 * @param[in] capacity 缓冲容量，包含末尾零。
 * @return 成功返回字节数，不含末尾零；容量不足返回 -1。
 */
int ws_http_server_response(const char *accept, char *out, unsigned capacity);
#endif
