/** @file ws_service.c @brief 工作任务持有传输资源，显式启动与维护准入分离。 */
#include "config/app_config.h"
#if D_FEATURE_WEBSOCKET
#include "ws_service.h"
#include "ws_port.h"
#include "ws_http.h"
#include <string.h>

static struct {
    ws_service_config_t config;
    ws_service_snapshot_t status;
    ws_url_t url;
    char endpoint[WS_URL_CAPACITY], origin[128], agent[128];
    ws_http_t http;
    ws_decoder_t decoder;
    uint8_t receive[2048];
    char request[WS_HTTP_CAPACITY], key[25], accept[29];
    uint8_t initialized, configured, callback_active, peer_close, close_sent, prepared;
    uint8_t ping_waiting, ping_value[4];
    uint8_t release_pending, local_close;
    uint32_t worker_generation, ping_at, last_ping, close_started;
} s_ws;

static void ws_worker(void *unused);
/**
 * @brief 读取当前构建可用的 WS/WSS 角色能力；完整合同见 ws_service.h。
 */
uint32_t ws_service_capabilities(void) { return ws_port_capabilities(); }

/**
 * @brief 填充客户端传输的默认配置；完整合同见 ws_service.h。
 */
void ws_service_defaults(ws_service_config_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->role = WS_CLIENT; c->url = ""; c->origin = ""; c->user_agent = "D_Code";
    c->io_timeout_ms = 10000; c->maximum_message = 1024U * 1024U;
    c->receive_slice = 1024; c->fragment_size = 2048;
    c->ping_interval_s = 30; c->pong_timeout_s = 120;
}

/**
 * @brief 初始化端口同步对象并将服务标记为可配置；完整合同见 ws_service.h。
 */
d_err_t ws_service_init(void)
{
    d_err_t result = ws_port_init();
    if (result != D_OK) return result;
    ws_port_lock(); s_ws.initialized = 1; ws_port_unlock();
    return D_OK;
}

/**
 * @brief 有界复制零结尾字符串，空输入按空串处理。
 * @param[out] out 目标缓冲。
 * @param[in] cap 缓冲容量。
 * @param[in] in 可空输入字符串。
 * @return 成功返回 0；容量不足返回 -1。
 */
static int ws_copy(char *out, unsigned cap, const char *in)
{
    unsigned n;
    if (!in) in = "";
    n = (unsigned)strlen(in);
    if (n >= cap) return -1;
    memcpy(out, in, n + 1);
    return 0;
}

/**
 * @brief 在传输空闲时绑定单实例配置；完整合同见 ws_service.h。
 */
d_err_t ws_service_configure(const ws_service_config_t *c)
{
    ws_url_t url;
    if (!c || !c->url || (c->role != WS_CLIENT && c->role != WS_SERVER) || c->io_timeout_ms < 100 || c->io_timeout_ms > 60000 ||
        !c->maximum_message || !c->receive_slice || c->receive_slice > sizeof(s_ws.receive) ||
        !c->fragment_size || c->fragment_size > 16384 ||
        (c->ping_interval_s && !c->pong_timeout_s) ||
        strlen(c->url) >= sizeof(s_ws.endpoint) ||
        (c->origin && (strlen(c->origin) >= sizeof(s_ws.origin) || strchr(c->origin,'\r') || strchr(c->origin,'\n'))) ||
        (c->user_agent && (strlen(c->user_agent) >= sizeof(s_ws.agent) || strchr(c->user_agent,'\r') || strchr(c->user_agent,'\n')))) return D_ERR_INVALID_ARG;
    memset(&url, 0, sizeof(url));
    if (c->role == WS_CLIENT) {
        if (ws_url_parse(c->url, &url)) return D_ERR_INVALID_ARG;
        if (url.secure && (!c->ca || !c->ca_length)) return D_ERR_INVALID_ARG;
    } else {
        const char *p = c->url;
        if (!c->listen_port || *p != '/') return D_ERR_INVALID_ARG;
        while (*p) { if ((unsigned char)*p <= 32 || *p == '#' || (unsigned char)*p == 127) return D_ERR_INVALID_ARG; ++p; }
        if (c->secure && (!c->certificate || !c->certificate_length || !c->private_key || !c->private_key_length)) return D_ERR_INVALID_ARG;
        url.secure = !!c->secure;
    }
    if (!(ws_port_capabilities() & (c->role == WS_CLIENT ?
        (url.secure ? WS_CAP_CLIENT_WSS : WS_CAP_CLIENT_WS) :
        (url.secure ? WS_CAP_SERVER_WSS : WS_CAP_SERVER_WS)))) return D_ERR_NOT_READY;
    ws_port_lock();
    if (!s_ws.initialized) { ws_port_unlock(); return D_ERR_NOT_READY; }
    if (s_ws.status.worker_active || s_ws.status.resources_owned || s_ws.status.maintenance) {
        ws_port_unlock(); return D_ERR_BUSY;
    }
    s_ws.config = *c; s_ws.url = url;
    (void)ws_copy(s_ws.endpoint, sizeof(s_ws.endpoint), c->url);
    (void)ws_copy(s_ws.origin, sizeof(s_ws.origin), c->origin);
    (void)ws_copy(s_ws.agent, sizeof(s_ws.agent), c->user_agent);
    s_ws.config.url = s_ws.endpoint; s_ws.config.origin = s_ws.origin; s_ws.config.user_agent = s_ws.agent;
    s_ws.config.secure = url.secure; s_ws.configured = 1;
    s_ws.status.state = WS_STOPPED; s_ws.status.error = D_OK;
    ws_port_unlock();
    return D_OK;
}

/**
 * @brief 检查工作代次是否因停止、维护或新代次失效。
 * @return 已撤销返回非零。
 */
static int ws_cancelled(void)
{
    int result;
    ws_port_lock();
    result = !s_ws.status.requested || s_ws.status.maintenance ||
             s_ws.status.generation != s_ws.worker_generation;
    ws_port_unlock();
    return result;
}

/**
 * @brief 复制事件元数据后在锁外同步通知用户回调。
 * @param[in] kind 状态、数据或控制事件。
 * @param[in] type WebSocket 操作码。
 * @param[in] flags 帧与消息边界标志。
 * @param[in] data 仅在回调期间借用的数据。
 * @param[in] length 数据字节数。
 * @note callback_active 阻止回调中重入同步发送。
 */
static void ws_emit(ws_event_kind_t kind, uint8_t type, uint8_t flags,
                    const uint8_t *data, uint32_t length)
{
    ws_event_t event;
    ws_port_lock();
    memset(&event, 0, sizeof(event)); event.kind = kind; event.state = s_ws.status.state;
    event.generation = s_ws.worker_generation; event.error = s_ws.status.error;
    event.type = type; event.flags = flags; event.data = data; event.length = length;
    s_ws.callback_active = 1;
    ws_port_unlock();
    if (s_ws.config.callback) s_ws.config.callback(&event, s_ws.config.user);
    ws_port_lock(); s_ws.callback_active = 0; ws_port_unlock();
}

/**
 * @brief 状态或错误变化时更新快照并发布状态事件。
 * @param[in] state 目标状态。
 * @param[in] error 对应错误码。
 */
static void ws_state(ws_state_t state, d_err_t error)
{
    ws_port_lock();
    if (s_ws.status.state == state && s_ws.status.error == error) { ws_port_unlock(); return; }
    s_ws.status.state = state; s_ws.status.error = error; ws_port_unlock();
    ws_emit(WS_EVENT_STATE, 0, 0, NULL, 0);
}

/**
 * @brief 创建一代工作任务并受理连接意图；完整合同见 ws_service.h。
 */
d_err_t ws_service_start(void)
{
    d_err_t result;
    ws_port_lock();
    if (!s_ws.initialized || !s_ws.configured) { ws_port_unlock(); return D_ERR_NOT_READY; }
    if (s_ws.status.maintenance || s_ws.status.worker_active || s_ws.status.resources_owned) {
        ws_port_unlock(); return D_ERR_BUSY;
    }
    ++s_ws.status.generation; s_ws.status.requested = 1; s_ws.status.worker_active = 1;
    s_ws.status.error = D_OK;
    ws_port_unlock();
    result = ws_port_start(ws_worker);
    if (result != D_OK) {
        ws_port_lock(); s_ws.status.requested = 0; s_ws.status.worker_active = 0;
        s_ws.status.state = WS_FAILED; s_ws.status.error = result; ws_port_unlock();
    }
    return result;
}

/**
 * @brief 撤销连接意图并使当前代次失效；完整合同见 ws_service.h。
 */
d_err_t ws_service_stop(void)
{
    ws_port_lock(); s_ws.status.requested = 0; ++s_ws.status.generation;
    ws_port_unlock(); return D_OK;
}

/**
 * @brief 在元数据锁内复制当前传输状态；完整合同见 ws_service.h。
 */
void ws_service_get_snapshot(ws_service_snapshot_t *out)
{ if (out) { ws_port_lock(); *out = s_ws.status; ws_port_unlock(); } }

/**
 * @brief 请求停止，并仅在工作任务和资源均释放后解除配置；完整合同见 ws_service.h。
 */
d_err_t ws_service_deinit(void)
{
    ws_service_stop(); ws_port_lock();
    if (s_ws.status.worker_active || s_ws.status.resources_owned) { ws_port_unlock(); return D_ERR_BUSY; }
    s_ws.configured = 0; s_ws.initialized = 0; ws_port_unlock(); return D_OK;
}

/**
 * @brief 为产品协调者增加维护占用并撤销当前连接代次；完整合同见 ws_service.h。
 */
d_err_t ws_service_maintenance_begin(ws_hold_owner_t owner)
{
    if (owner != WS_HOLD_OTA && owner != WS_HOLD_POWER && owner != WS_HOLD_EXTERNAL) return D_ERR_INVALID_ARG;
    ws_port_lock(); s_ws.status.maintenance |= owner; s_ws.release_pending &= ~owner;
    ++s_ws.status.generation; ws_port_unlock();
    return D_OK;
}
/**
 * @brief 检查资源已释放且工作任务已暂停或退出；完整合同见 ws_service.h。
 */
d_err_t ws_service_maintenance_poll(uint8_t *ready)
{
    if (!ready) return D_ERR_INVALID_ARG;
    ws_port_lock();
    *ready = !s_ws.status.resources_owned && (!s_ws.status.worker_active || s_ws.status.state == WS_PAUSED);
    ws_port_unlock(); return D_OK;
}
/**
 * @brief 清理完成后解除指定维护所有者位；完整合同见 ws_service.h。
 */
d_err_t ws_service_maintenance_release(ws_hold_owner_t owner)
{
    if (owner != WS_HOLD_OTA && owner != WS_HOLD_POWER && owner != WS_HOLD_EXTERNAL) return D_ERR_INVALID_ARG;
    ws_port_lock();
    if (s_ws.status.resources_owned || (s_ws.status.worker_active && s_ws.status.state != WS_PAUSED)) {
        ws_port_unlock(); return D_ERR_BUSY;
    }
    s_ws.status.maintenance &= ~owner; s_ws.release_pending &= ~owner;
    ws_port_unlock(); return D_OK;
}

/**
 * @brief 请求撤销协调者占用，资源清理后再真正解除；完整合同见 ws_service.h。
 */
d_err_t ws_service_maintenance_cancel(ws_hold_owner_t owner)
{
    if (owner != WS_HOLD_OTA && owner != WS_HOLD_POWER && owner != WS_HOLD_EXTERNAL) return D_ERR_INVALID_ARG;
    ws_port_lock();
    if (s_ws.status.resources_owned) s_ws.release_pending |= owner;
    else s_ws.status.maintenance &= ~owner;
    ws_port_unlock(); return D_OK;
}

/**
 * @brief 检查指定代次仍被请求且未被维护占用撤销。
 * @param[in] generation 发送操作捕获的连接代次。
 * @return 有效返回非零。
 */
static int ws_generation_valid(uint32_t generation)
{
    int valid;
    ws_port_lock(); valid = s_ws.status.requested && !s_ws.status.maintenance &&
        generation == s_ws.status.generation; ws_port_unlock();
    return valid;
}

/**
 * @brief 在 I/O 所有权内完整写出数据，每次进展只续期 I/O 进度期限。
 * @param[in] bytes 输入字节。
 * @param[in] length 待写字节数。
 * @param[in] generation 操作对应的连接代次。
 * @return 全部写出返回 D_OK；代次撤销、网络或无进展超时返回对应错误。
 * @note 调用者持有 I/O 互斥量；可能只写出部分数据后失败。
 */
static d_err_t ws_write(const uint8_t *bytes, uint32_t length, uint32_t generation)
{
    uint32_t sent = 0, progressed = ws_port_now();
    while (sent < length) {
        int n;
        if (!ws_generation_valid(generation)) return D_ERR_NOT_READY;
        n = ws_port_write(bytes + sent, length - sent);
        if (n < 0 || (uint32_t)n > length - sent) return D_ERR_NETWORK;
        if (n) { sent += (uint32_t)n; progressed = ws_port_now(); }
        else {
            if ((uint32_t)(ws_port_now() - progressed) >= s_ws.config.io_timeout_ms) return D_ERR_TIMEOUT;
            ws_port_wait(1);
        }
    }
    return D_OK;
}

/**
 * @brief 生成帧头并以有界暂存区分段掩码写出负载。
 * @param[in] type 帧操作码。
 * @param[in] fin 非零表示消息末帧。
 * @param[in] data 负载输入，零长度时允许为空。
 * @param[in] length 负载字节数。
 * @param[in] generation 连接代次。
 * @return 帧写入结果；随机、参数或 I/O 失败返回对应错误。
 * @note 调用者持有 I/O 互斥量；客户端每帧生成随机掩码。
 */
static d_err_t ws_frame_write(uint8_t type, uint8_t fin, const uint8_t *data,
                              uint32_t length, uint32_t generation)
{
    uint8_t header[14], mask[4], scratch[256];
    uint32_t offset = 0;
    unsigned size, n, i;
    d_err_t result;
    int masked = s_ws.config.role == WS_CLIENT;
    if (masked && ws_port_random(mask, sizeof(mask))) return D_ERR_IO;
    size = ws_frame_header(header, type, fin, length, masked ? mask : NULL);
    if (!size) return D_ERR_INVALID_ARG;
    result = ws_write(header, size, generation);
    while (result == D_OK && offset < length) {
        n = length - offset; if (n > sizeof(scratch)) n = sizeof(scratch);
        for (i = 0; i < n; ++i) scratch[i] = data[offset + i] ^ (masked ? mask[(offset + i) & 3U] : 0);
        result = ws_write(scratch, n, generation); offset += n;
    }
    return result;
}

/**
 * @brief 取得 I/O 所有权后由工作任务发送单个控制帧。
 * @param[in] type 控制操作码。
 * @param[in] data 控制负载。
 * @param[in] length 负载字节数。
 * @return 实际发送结果；取得锁超时返回 D_ERR_TIMEOUT。
 */
static d_err_t ws_control(uint8_t type, const uint8_t *data, uint32_t length)
{
    d_err_t result;
    if (!ws_port_io_lock(s_ws.config.io_timeout_ms)) return D_ERR_TIMEOUT;
    result = ws_frame_write(type, 1, data, length, s_ws.worker_generation);
    ws_port_io_unlock();
    return result;
}

/**
 * @brief 在同一 I/O 互斥区生成随机心跳并发送 PING。
 * @return 发送结果或随机源、锁超时错误。
 */
static d_err_t ws_heartbeat(void)
{
    d_err_t result;
    if (!ws_port_io_lock(s_ws.config.io_timeout_ms)) return D_ERR_TIMEOUT;
    /* DRBG 与 TLS 上下文和应用发送共用同一 I/O 序列化。 */
    if (ws_port_random(s_ws.ping_value, sizeof(s_ws.ping_value))) result = D_ERR_IO;
    else result = ws_frame_write(WS_PING, 1, s_ws.ping_value, sizeof(s_ws.ping_value), s_ws.worker_generation);
    ws_port_io_unlock();
    return result;
}

/**
 * @brief 由应用任务有界同步发送一条消息或控制帧；完整合同见 ws_service.h。
 */
d_err_t ws_service_send(uint8_t type, const void *data, uint32_t length)
{
    uint32_t generation, offset = 0;
    d_err_t result = D_OK;
    if ((type != WS_TEXT && type != WS_BINARY && type != WS_PING && type != WS_PONG && type != WS_CLOSE) ||
        (!data && length) || (type >= 8 && length > 125) ||
        (type == WS_CLOSE && !ws_close_payload_valid(data, length))) return D_ERR_INVALID_ARG;
    if (type == WS_TEXT && !ws_text_valid(data, length)) return D_ERR_INVALID_ARG;
    ws_port_lock();
    if (s_ws.callback_active) { ws_port_unlock(); return D_ERR_BUSY; }
    if (s_ws.status.state != WS_OPEN || s_ws.close_sent || s_ws.peer_close || !s_ws.status.requested || s_ws.status.maintenance) {
        ws_port_unlock(); return D_ERR_NOT_READY;
    }
    if (type < 8 && length > s_ws.config.maximum_message) { ws_port_unlock(); return D_ERR_INVALID_ARG; }
    generation = s_ws.status.generation;
    ws_port_unlock();
    if (!ws_port_io_lock(s_ws.config.io_timeout_ms)) return D_ERR_TIMEOUT;
    ws_port_lock();
    if (s_ws.status.state != WS_OPEN || s_ws.close_sent || s_ws.peer_close || generation != s_ws.status.generation)
        result = D_ERR_NOT_READY;
    ws_port_unlock();
    if (result == D_OK) {
        do {
            uint32_t n = length - offset;
            if (type < 8 && s_ws.config.fragment_send && n > s_ws.config.fragment_size) n = s_ws.config.fragment_size;
            result = ws_frame_write(offset ? 0 : type, offset + n == length,
                data ? (const uint8_t *)data + offset : NULL, n, generation);
            offset += n;
        } while (result == D_OK && offset < length);
        ws_port_lock();
        if (result == D_OK) {
            s_ws.status.sent_bytes += length;
            if (type == WS_CLOSE) {
                s_ws.close_sent = 1; s_ws.local_close = 1;
                s_ws.close_started = ws_port_now();
                s_ws.status.state = WS_CLOSING;
            }
        } else s_ws.peer_close = 1; /* 部分帧写入失败后该连接不能继续使用。 */
        ws_port_unlock();
    }
    ws_port_io_unlock();
    return result;
}

/**
 * @brief 同步发送指定内容的 PING 控制帧；完整合同见 ws_service.h。
 */
d_err_t ws_service_ping(const void *data, uint32_t length)
{ return ws_service_send(WS_PING, data, length); }

/**
 * @brief 发送带关闭码及 UTF-8 原因的 CLOSE 帧；完整合同见 ws_service.h。
 */
d_err_t ws_service_close(uint16_t code, const char *reason)
{
    uint8_t bytes[125]; unsigned length = reason ? (unsigned)strlen(reason) : 0;
    if (length > 123) return D_ERR_INVALID_ARG;
    bytes[0] = (uint8_t)(code >> 8); bytes[1] = (uint8_t)code;
    if (length) memcpy(bytes + 2, reason, length);
    return ws_service_send(WS_CLOSE, bytes, length + 2);
}

/**
 * @brief 处理 PING、PONG 和 CLOSE，再同步发布借用数据事件。
 * @param[in] user 解码器上下文，本实现不使用。
 * @param[in] type 收到的操作码。
 * @param[in] flags 分段边界标志。
 * @param[in] bytes 仅在本回调内借用的字节。
 * @param[in] length 本段字节数。
 * @return 正常返回 0；已取消、自动控制帧发送失败返回 -1。
 */
static int ws_received(void *user, uint8_t type, uint8_t flags, const uint8_t *bytes, uint32_t length)
{
    (void)user;
    if (ws_cancelled()) return -1;
    if (type == WS_PING && ws_control(WS_PONG, bytes, length) != D_OK) return -1;
    if (type == WS_PONG && length == sizeof(s_ws.ping_value) && !memcmp(bytes, s_ws.ping_value, length))
        s_ws.ping_waiting = 0;
    if (type == WS_CLOSE) {
        uint8_t send_close;
        ws_port_lock(); send_close = !s_ws.close_sent; s_ws.peer_close = 1; ws_port_unlock();
        if (send_close && ws_control(WS_CLOSE, bytes, length) != D_OK) return -1;
        ws_port_lock(); s_ws.close_sent = 1; ws_port_unlock();
    }
    ws_port_lock(); s_ws.status.received_bytes += length; ws_port_unlock();
    ws_emit(type >= 8 ? WS_EVENT_CONTROL : WS_EVENT_DATA, type, flags, bytes, length);
    return 0;
}

/**
 * @brief 执行双向 HTTP Upgrade 并处理同次读取中紧随头部的帧。
 * @return 成功进入 OPEN 返回 D_OK；取消、期限、协议或 I/O 失败返回对应错误。
 */
static d_err_t ws_upgrade(void)
{
    uint32_t started = ws_port_now();
    d_err_t result;
    int n, complete = 0;
    ws_http_init(&s_ws.http);
    if (s_ws.config.role == WS_CLIENT) {
        if (ws_port_key(s_ws.key) || ws_port_accept_key(s_ws.key, s_ws.accept)) return D_ERR_IO;
        n = ws_http_client_request(&s_ws.url, s_ws.key, s_ws.origin, s_ws.agent, s_ws.request, sizeof(s_ws.request));
        if (n < 0) return D_ERR_INVALID_ARG;
        result = ws_write((const uint8_t *)s_ws.request, (unsigned)n, s_ws.worker_generation);
        if (result != D_OK) return result;
    }
    while (!complete) {
        unsigned used;
        if (ws_cancelled()) return D_ERR_NOT_READY;
        if ((uint32_t)(ws_port_now() - started) >= s_ws.config.io_timeout_ms) return D_ERR_TIMEOUT;
        n = ws_port_read(s_ws.receive, s_ws.config.receive_slice);
        if (n < 0 || (unsigned)n > s_ws.config.receive_slice) return D_ERR_NETWORK;
        if (!n) { ws_port_wait(1); continue; }
        complete = ws_http_feed(&s_ws.http, s_ws.receive, (unsigned)n, &used);
        if (complete < 0) return D_ERR_INVALID_ARG;
        if (!complete) continue;
        if (s_ws.config.role == WS_CLIENT) {
            if (ws_http_validate_client(s_ws.http.bytes, s_ws.accept)) return D_ERR_INVALID_ARG;
        } else {
            int size;
            if (ws_http_server_key(s_ws.http.bytes, s_ws.endpoint, s_ws.key) ||
                ws_port_accept_key(s_ws.key, s_ws.accept)) return D_ERR_INVALID_ARG;
            size = ws_http_server_response(s_ws.accept, s_ws.request, sizeof(s_ws.request));
            if (size < 0) return D_ERR_IO;
            result = ws_write((const uint8_t *)s_ws.request, (unsigned)size, s_ws.worker_generation);
            if (result != D_OK) return result;
        }
        if (ws_cancelled()) return D_ERR_NOT_READY;
        ws_decoder_init(&s_ws.decoder, s_ws.config.role == WS_SERVER,
                        s_ws.config.maximum_message, ws_received, NULL);
        ws_port_lock();
        if (!s_ws.status.requested || s_ws.status.maintenance || s_ws.status.generation != s_ws.worker_generation) {
            ws_port_unlock(); return D_ERR_NOT_READY;
        }
        s_ws.peer_close = 0; s_ws.close_sent = 0; s_ws.local_close = 0; ++s_ws.status.connections;
        s_ws.status.state = WS_OPEN; s_ws.status.error = D_OK; ws_port_unlock();
        s_ws.ping_waiting = 0; s_ws.last_ping = ws_port_now();
        ws_emit(WS_EVENT_STATE, 0, 0, NULL, 0);
        /* Upgrade 与首个帧可能在同一次 TCP 读取中到达。 */
        if (ws_cancelled()) return D_ERR_NOT_READY;
        if ((unsigned)n > used && ws_decoder_feed(&s_ws.decoder, s_ws.receive + used, (unsigned)n - used))
            return D_ERR_INVALID_ARG;
    }
    return D_OK;
}

/**
 * @brief 取得 I/O 所有权后关闭对端或全部传输资源。
 * @param[in] all 非零释放全部资源，0 只关闭对端。
 * @return 关闭结果；暂未取得锁返回 D_ERR_BUSY。
 * @note 仅全部关闭成功后清除 resources_owned。
 */
static d_err_t ws_close_owned(int all)
{
    d_err_t result;
    if (!ws_port_io_lock(1000)) return D_ERR_BUSY;
    result = all ? ws_port_close_all() : ws_port_close_peer();
    ws_port_io_unlock();
    if (result == D_OK && all) {
        s_ws.prepared = 0;
        ws_port_lock(); s_ws.status.resources_owned = 0; ws_port_unlock();
    }
    return result;
}

/**
 * @brief 在重连退避期限内响应取消。
 * @param[in] milliseconds 本次退避毫秒数。
 */
static void ws_backoff(uint32_t milliseconds)
{
    uint32_t started = ws_port_now();
    while (!ws_cancelled() && (uint32_t)(ws_port_now() - started) < milliseconds) ws_port_wait(10);
}

/**
 * @brief 推进已连接对端的接收、关闭握手和心跳期限。
 * @return 正常关闭返回 D_OK；取消、协议、锁或网络期限错误返回对应值。
 */
static d_err_t ws_peer_loop(void)
{
    for (;;) {
        int n, closed, closing;
        uint32_t now;
        if (ws_cancelled()) return D_ERR_NOT_READY;
        ws_port_lock(); closed = s_ws.peer_close; closing = s_ws.close_sent;
        if (s_ws.local_close && (uint32_t)(ws_port_now() - s_ws.close_started) >= s_ws.config.io_timeout_ms) closed = 1;
        ws_port_unlock();
        if (closed) return D_OK;
        if (!ws_port_io_lock(1000)) return D_ERR_BUSY;
        n = ws_port_read(s_ws.receive, s_ws.config.receive_slice);
        ws_port_io_unlock();
        if (n < 0 || (unsigned)n > s_ws.config.receive_slice) return D_ERR_NETWORK;
        if (n && ws_decoder_feed(&s_ws.decoder, s_ws.receive, (unsigned)n)) return D_ERR_INVALID_ARG;
        if (ws_cancelled()) return D_ERR_NOT_READY;
        /* 接收回调可能在读取前快照之后完成关闭，需再次读取状态。 */
        ws_port_lock(); closed = s_ws.peer_close; closing = s_ws.close_sent; ws_port_unlock();
        if (closed) return D_OK;
        now = ws_port_now();
        if (!closing && s_ws.ping_waiting && (uint32_t)(now - s_ws.ping_at) >= (uint32_t)s_ws.config.pong_timeout_s * 1000U)
            return D_ERR_TIMEOUT;
        if (!closing && !s_ws.ping_waiting && s_ws.config.ping_interval_s &&
            (uint32_t)(now - s_ws.last_ping) >= (uint32_t)s_ws.config.ping_interval_s * 1000U) {
            if (ws_heartbeat() != D_OK) return D_ERR_NETWORK;
            s_ws.ping_at = s_ws.last_ping = now; s_ws.ping_waiting = 1;
        }
        if (!n) ws_port_wait(1);
    }
}

/**
 * @brief 持续编排联网、连接、握手、维护暂停及资源清理。
 * @param[in] unused 任务兼容参数，本实现不使用。
 * @note 退出前反复完成受检清理，最后才发布 worker_active 为 0。
 */
static void ws_worker(void *unused)
{
    uint32_t retry_ms = 5000;
    d_err_t result = D_OK;
    (void)unused;
    for (;;) {
        uint8_t requested, held;
        ws_port_lock();
        requested = s_ws.status.requested; held = s_ws.status.maintenance;
        s_ws.worker_generation = s_ws.status.generation;
        ws_port_unlock();
        if (!requested) break;
        if (held) {
            ws_state(WS_CLOSING, D_OK);
            if (ws_close_owned(1) != D_OK) { ws_port_wait(10); continue; }
            ws_state(WS_PAUSED, D_OK);
            do {
                ws_port_wait(10);
                ws_port_lock();
                s_ws.status.maintenance &= ~s_ws.release_pending; s_ws.release_pending = 0;
                held = s_ws.status.maintenance; requested = s_ws.status.requested; ws_port_unlock();
            } while (held && requested);
            continue;
        }
        if (s_ws.config.role == WS_CLIENT && !ws_port_network_ready()) {
            ws_state(WS_WAIT_NETWORK, D_OK); ws_port_wait(100); continue;
        }
        if (ws_cancelled()) continue;
        if (!s_ws.prepared) {
            ws_port_lock(); s_ws.status.resources_owned = 1; ws_port_unlock();
            result = ws_port_prepare(&s_ws.config, &s_ws.url);
            if (result != D_OK) {
                ws_state(WS_CLOSING, result);
                while (ws_close_owned(1) != D_OK) ws_port_wait(10);
                if (result == D_ERR_NOT_READY) { ws_state(WS_WAIT_NETWORK, result); ws_port_wait(100); continue; }
                break;
            }
            s_ws.prepared = 1;
        }
        if (ws_cancelled()) continue;
        ws_state(s_ws.config.role == WS_CLIENT ? WS_CONNECTING : WS_LISTENING, D_OK);
        /* 状态回调可非阻塞撤销当前代次。 */
        if (ws_cancelled()) continue;
        result = s_ws.config.role == WS_CLIENT ? ws_port_connect() : ws_port_accept();
        if (s_ws.config.role == WS_SERVER && result == D_ERR_NOT_READY) { ws_port_wait(10); continue; }
        if (result == D_OK && !ws_cancelled()) {
            ws_port_lock();
            if (!s_ws.status.requested || s_ws.status.maintenance) { ws_port_unlock(); continue; }
            s_ws.worker_generation = ++s_ws.status.generation;
            ws_port_unlock();
            ws_state(WS_UPGRADING, D_OK);
            if (ws_cancelled()) continue;
            result = ws_port_tls_handshake(ws_cancelled);
            if (result == D_OK && !ws_cancelled()) result = ws_upgrade();
            if (result == D_OK && !ws_cancelled()) { retry_ms = 5000; result = ws_peer_loop(); }
        }
        ws_state(WS_CLOSING, result);
        while (ws_close_owned(0) != D_OK) ws_port_wait(10);
        ws_port_lock();
        if (s_ws.local_close && s_ws.config.role == WS_CLIENT) s_ws.status.requested = 0;
        s_ws.local_close = 0;
        ws_port_unlock();
        if (ws_cancelled()) continue;
        if (s_ws.config.role == WS_CLIENT) {
            ws_state(WS_BACKOFF, result); ws_backoff(retry_ms);
            retry_ms = retry_ms < 30000 ? retry_ms * 2 : 60000;
        } else ws_port_wait(10);
    }
    ws_state(WS_CLOSING, result);
    while (ws_close_owned(1) != D_OK) ws_port_wait(10);
    ws_port_lock(); s_ws.status.requested = 0;
    s_ws.status.maintenance &= ~s_ws.release_pending; s_ws.release_pending = 0; ws_port_unlock();
    ws_state(result == D_OK || result == D_ERR_NOT_READY ? WS_STOPPED : WS_FAILED, result);
    /* 返回前最后一次访问共享状态；新启动先回收旧所有者，再替换工作任务。 */
    ws_port_lock(); s_ws.status.worker_active = 0; ws_port_unlock();
}
#endif
