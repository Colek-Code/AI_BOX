/** @file ws_port_wl82.c @brief 显式持有 SDK 套接字与 TLS，不使用 SDK WebSocket 辅助封装。 */
#include "config/app_config.h"
#if D_FEATURE_WEBSOCKET
#include "ws_port.h"
#include "system/includes.h"
#include "wifi/wifi_connect.h"
#include "ntp/ntp.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include <string.h>
#include <time.h>

static DEFINE_SPINLOCK(s_lock);
static OS_MUTEX s_io;
static uint8_t s_ready, s_ntp_confirmed;
static int s_worker_id;
/**
 * @brief 读取当前 SDK 编译支持的客户端与服务端能力；完整合同见 ws_port.h。
 */
uint32_t ws_port_capabilities(void)
{
    uint32_t capabilities = WS_CAP_CLIENT_WS | WS_CAP_SERVER_WS | WS_CAP_CLIENT_WSS;
#if defined(MBEDTLS_SSL_SRV_C)
    capabilities |= WS_CAP_SERVER_WSS;
#endif
    return capabilities;
}
static struct {
    mbedtls_net_context listener, peer;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config tls;
    mbedtls_x509_crt roots, certificate;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_ssl_session session;
    mbedtls_x509_time valid_from, valid_to;
    const ws_service_config_t *config;
    const ws_url_t *url;
    uint8_t initialized, session_valid, resumed, certificate_seen;
} s_port;

/**
 * @brief 取得 WebSocket 服务元数据自旋锁；完整合同见 ws_port.h。
 */
void ws_port_lock(void) { spin_lock(&s_lock); }
/**
 * @brief 释放 WebSocket 服务元数据自旋锁；完整合同见 ws_port.h。
 */
void ws_port_unlock(void) { spin_unlock(&s_lock); }
/**
 * @brief 读取用于期限比较的单调毫秒时钟；完整合同见 ws_port.h。
 */
uint32_t ws_port_now(void) { return (uint32_t)timer_get_ms(); }
/**
 * @brief 按系统节拍让出执行；完整合同见 ws_port.h。
 */
void ws_port_wait(uint32_t ms) { os_time_dly((ms + 9U) / 10U); }
/**
 * @brief 有界等待 I/O 互斥量；完整合同见 ws_port.h。
 */
int ws_port_io_lock(uint32_t ms) { return os_mutex_pend(&s_io, (ms + 9U) / 10U) == OS_NO_ERR; }
/**
 * @brief 释放当前持有的 I/O 互斥量；完整合同见 ws_port.h。
 */
void ws_port_io_unlock(void) { os_mutex_post(&s_io); }

/**
 * @brief 在产品启动阶段一次性创建 I/O 互斥量；完整合同见 ws_port.h。
 */
d_err_t ws_port_init(void)
{
    /* 产品启动阶段串行初始化一次，之后再向调用者暴露服务。 */
    if (!s_ready) {
        if (os_mutex_create(&s_io) != OS_NO_ERR) return D_ERR_NO_MEMORY;
        s_ready = 1;
    }
    return D_OK;
}

/**
 * @brief 等待旧工作任务结束后创建新 WebSocket 工作任务；完整合同见 ws_port.h。
 */
d_err_t ws_port_start(void (*worker)(void *))
{
    /* 仅在旧工作任务发布最终 inactive 状态后调用。 */
    if (s_worker_id) thread_kill(&s_worker_id, KILL_WAIT);
    return thread_fork("d_ws", 10, 4096, 0, &s_worker_id, worker, NULL) == OS_NO_ERR ? D_OK : D_ERR_NO_MEMORY;
}

/**
 * @brief 检查 WiFi 已开启且 STA 已取得 DHCP 地址；完整合同见 ws_port.h。
 */
int ws_port_network_ready(void)
{ return wifi_is_on() && wifi_get_sta_connect_state() == WIFI_STA_NETWORK_STACK_DHCP_SUCC; }

/**
 * @brief 锁存 NTP 曾成功的事实，并检查当前 UTC 在可接受范围内。
 * @return 时间可信且处于 2024 年起至 INT32_MAX 范围返回非零。
 */
static int ws_trusted_time(void)
{
    int64_t utc = (int64_t)time(NULL);
    if (ntp_client_get_time_status() == 1U) s_ntp_confirmed = 1;
    return s_ntp_confirmed && utc >= 1704067200LL && utc <= INT32_MAX;
}

/**
 * @brief 按年月日时分秒比较两个证书时刻。
 * @param[in] a 非空时刻。
 * @param[in] b 非空参考时刻。
 * @return a 早于 b 返回 -1，相同返回 0，晚于返回 1。
 */
static int ws_time_compare(const mbedtls_x509_time *a, const mbedtls_x509_time *b)
{
#define WS_COMPARE(field) if (a->field != b->field) return a->field < b->field ? -1 : 1
    WS_COMPARE(year); WS_COMPARE(mon); WS_COMPARE(day);
    WS_COMPARE(hour); WS_COMPARE(min); WS_COMPARE(sec);
#undef WS_COMPARE
    return 0;
}

/**
 * @brief 累计证书链有效期交集，供后续会话复用检查。
 * @param[in] context 验证兼容上下文，本实现不使用。
 * @param[in] crt 当前证书。
 * @param[in] depth 证书链深度，本实现不使用。
 * @param[in] flags Mbed TLS 验证错误位，本函数保持不变。
 * @return 固定返回 0，不清除任何证书验证错误位。
 * @note 当前 SDK 未保留对端证书，复用握手仍必须遵守原链有效期。
 */
static int ws_verify(void *context, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void)context; (void)depth; (void)flags;
    if (!s_port.certificate_seen || ws_time_compare(&crt->valid_from, &s_port.valid_from) > 0)
        s_port.valid_from = crt->valid_from;
    if (!s_port.certificate_seen || ws_time_compare(&crt->valid_to, &s_port.valid_to) < 0)
        s_port.valid_to = crt->valid_to;
    s_port.certificate_seen = 1;
    return 0; /* 不清除任何证书验证错误位。 */
}

/**
 * @brief 检查已缓存会话与原证书链有效期当前仍有效。
 * @return 会话、可信时间与有效期全部满足返回非零。
 */
static int ws_session_current(void)
{
    return s_port.session_valid && ws_trusted_time() &&
        !mbedtls_x509_time_is_future(&s_port.valid_from) && !mbedtls_x509_time_is_past(&s_port.valid_to);
}

/**
 * @brief 初始化随机源、TLS 对象及可选监听套接字；完整合同见 ws_port.h。
 */
d_err_t ws_port_prepare(const ws_service_config_t *c, const ws_url_t *url)
{
    static const unsigned char personal[] = "dcode_websocket";
    char port[6];
    if (s_port.initialized) return D_ERR_BUSY;
    if (c->secure && c->role == WS_CLIENT && !ws_trusted_time()) return D_ERR_NOT_READY;
#if !defined(MBEDTLS_SSL_SRV_C)
    /* 产品固定的 3.4 归档没有服务端握手实现；
     * 在取得匹配原厂库前显式拒绝该模式。 */
    if (c->secure && c->role == WS_SERVER) return D_ERR_INVALID_ARG;
#endif
    s_port.config = c; s_port.url = url;
    mbedtls_net_init(&s_port.listener); mbedtls_net_init(&s_port.peer);
    mbedtls_ssl_init(&s_port.ssl); mbedtls_ssl_config_init(&s_port.tls);
    mbedtls_x509_crt_init(&s_port.roots); mbedtls_x509_crt_init(&s_port.certificate);
    mbedtls_pk_init(&s_port.key); mbedtls_entropy_init(&s_port.entropy);
    mbedtls_ctr_drbg_init(&s_port.random); mbedtls_ssl_session_init(&s_port.session);
    s_port.initialized = 1;
    if (mbedtls_ctr_drbg_seed(&s_port.random, mbedtls_entropy_func, &s_port.entropy,
                             personal, sizeof(personal) - 1) != 0) return D_ERR_IO;
    if (c->secure) {
        if (mbedtls_ssl_config_defaults(&s_port.tls,
            c->role == WS_CLIENT ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) return D_ERR_IO;
        mbedtls_ssl_conf_rng(&s_port.tls, mbedtls_ctr_drbg_random, &s_port.random);
        if (c->role == WS_CLIENT) {
            if (mbedtls_x509_crt_parse(&s_port.roots, c->ca, c->ca_length)) return D_ERR_INVALID_ARG;
            mbedtls_ssl_conf_authmode(&s_port.tls, MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_ca_chain(&s_port.tls, &s_port.roots, NULL);
            mbedtls_ssl_conf_verify(&s_port.tls, ws_verify, NULL);
        } else {
            if (mbedtls_x509_crt_parse(&s_port.certificate, c->certificate, c->certificate_length) ||
                mbedtls_pk_parse_key(&s_port.key, c->private_key, c->private_key_length, NULL, 0,
                                    mbedtls_ctr_drbg_random, &s_port.random) ||
                mbedtls_ssl_conf_own_cert(&s_port.tls, &s_port.certificate, &s_port.key)) return D_ERR_INVALID_ARG;
            mbedtls_ssl_conf_authmode(&s_port.tls, MBEDTLS_SSL_VERIFY_NONE); /* 本服务端模式没有要求客户端证书的合同。 */
        }
    }
    if (c->role == WS_SERVER) {
        snprintf(port, sizeof(port), "%u", (unsigned)c->listen_port);
        if (mbedtls_net_bind(&s_port.listener, "0.0.0.0", port, MBEDTLS_NET_PROTO_TCP)) return D_ERR_NETWORK;
        if (mbedtls_net_set_nonblock(&s_port.listener)) return D_ERR_IO;
    }
    return D_OK;
}

/**
 * @brief 配置非阻塞对端，并在 TLS 客户端设置主机名与可用缓存会话。
 * @return 成功返回 D_OK，套接字或 TLS 设置失败返回 D_ERR_IO。
 */
static d_err_t ws_peer_setup(void)
{
    if (mbedtls_net_set_nonblock(&s_port.peer)) return D_ERR_IO;
    if (!s_port.config->secure) return D_OK;
    mbedtls_ssl_free(&s_port.ssl); mbedtls_ssl_init(&s_port.ssl);
    if (mbedtls_ssl_setup(&s_port.ssl, &s_port.tls)) return D_ERR_IO;
    s_port.resumed = 0;
    if (s_port.config->role == WS_CLIENT) {
        if (mbedtls_ssl_set_hostname(&s_port.ssl, s_port.url->host)) return D_ERR_IO;
        if (s_port.config->resume_tls_session && ws_session_current()) {
            if (mbedtls_ssl_set_session(&s_port.ssl, &s_port.session) == 0) s_port.resumed = 1;
        }
    }
    s_port.certificate_seen = 0;
    mbedtls_ssl_set_bio(&s_port.ssl, &s_port.peer, mbedtls_net_send, mbedtls_net_recv, NULL);
    return D_OK;
}

/**
 * @brief 连接配置中的客户端地址并配置非阻塞对端；完整合同见 ws_port.h。
 */
d_err_t ws_port_connect(void)
{
    if (s_port.config->secure && !ws_trusted_time()) return D_ERR_NOT_READY;
    s_port.peer.connect_to_ms = s_port.config->io_timeout_ms;
    if (mbedtls_net_connect(&s_port.peer, s_port.url->host, s_port.url->port, MBEDTLS_NET_PROTO_TCP)) {
        s_port.peer.hdl = NULL; /* 当前 SDK 已释放失败连接。 */
        return D_ERR_NETWORK;
    }
    return ws_peer_setup();
}

/**
 * @brief 非阻塞接收一个监听连接并配置对端；完整合同见 ws_port.h。
 */
d_err_t ws_port_accept(void)
{
    int result = mbedtls_net_accept(&s_port.listener, &s_port.peer, NULL, 0, NULL);
    if (result == MBEDTLS_ERR_SSL_WANT_READ) return D_ERR_NOT_READY;
    if (result) return D_ERR_NETWORK;
    return ws_peer_setup();
}

/**
 * @brief 释放当前对端及其 TLS 连接，保留监听与会话配置；完整合同见 ws_port.h。
 */
d_err_t ws_port_close_peer(void)
{
    if (!s_port.initialized) return D_OK;
    mbedtls_net_free(&s_port.peer); mbedtls_net_init(&s_port.peer);
    mbedtls_ssl_free(&s_port.ssl); mbedtls_ssl_init(&s_port.ssl);
    return D_OK;
}

/**
 * @brief 释放对端、监听与全部 TLS 配置并清空端口状态；完整合同见 ws_port.h。
 */
d_err_t ws_port_close_all(void)
{
    if (!s_port.initialized) return D_OK;
    ws_port_close_peer();
    mbedtls_net_free(&s_port.listener);
    mbedtls_ssl_config_free(&s_port.tls);
    mbedtls_x509_crt_free(&s_port.roots); mbedtls_x509_crt_free(&s_port.certificate);
    mbedtls_pk_free(&s_port.key); mbedtls_ssl_session_free(&s_port.session);
    mbedtls_ctr_drbg_free(&s_port.random); mbedtls_entropy_free(&s_port.entropy);
    memset(&s_port, 0, sizeof(s_port));
    return D_OK;
}

/**
 * @brief 在取消探针与期限约束下完成一次 TLS 握手及客户端证书复核。
 * @param[in] cancelled 非空、非阻塞取消探针。
 * @return 成功返回 D_OK；取消、超时或 TLS 验证失败返回对应错误。
 */
static d_err_t ws_handshake_once(int (*cancelled)(void))
{
    uint32_t started = ws_port_now();
    int result;
    for (;;) {
        if (cancelled()) return D_ERR_NOT_READY;
        result = mbedtls_ssl_handshake(&s_port.ssl);
        if (!result) break;
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) return D_ERR_IO;
        if ((uint32_t)(ws_port_now() - started) >= s_port.config->io_timeout_ms) return D_ERR_TIMEOUT;
        ws_port_wait(10);
    }
    if (s_port.config->role == WS_CLIENT) {
        if (!ws_trusted_time() || mbedtls_ssl_get_verify_result(&s_port.ssl) ||
            (!s_port.certificate_seen && !s_port.resumed) ||
            mbedtls_x509_time_is_future(&s_port.valid_from) || mbedtls_x509_time_is_past(&s_port.valid_to)) return D_ERR_IO;
    }
    return cancelled() ? D_ERR_NOT_READY : D_OK;
}

/**
 * @brief 执行有界 TLS 握手；会话复用失败时至多重新连接一次；完整合同见 ws_port.h。
 */
d_err_t ws_port_tls_handshake(int (*cancelled)(void))
{
    d_err_t result;
    uint8_t reused;
    if (!s_port.config->secure) return cancelled() ? D_ERR_NOT_READY : D_OK;
    reused = s_port.resumed;
    result = ws_handshake_once(cancelled);
    if (result != D_OK && reused && !cancelled()) {
        /* 缓存会话被拒绝时只尝试一次全新连接，不放宽验证。 */
        s_port.session_valid = 0;
        ws_port_close_peer();
        result = ws_port_connect();
        if (result == D_OK) result = ws_handshake_once(cancelled);
    }
    if (result != D_OK) s_port.session_valid = 0;
    else if (s_port.config->role == WS_CLIENT && s_port.config->resume_tls_session) {
        mbedtls_ssl_session_free(&s_port.session); mbedtls_ssl_session_init(&s_port.session);
        s_port.session_valid = mbedtls_ssl_get_session(&s_port.ssl, &s_port.session) == 0;
    }
    return result;
}

/**
 * @brief 从当前明文或 TLS 对端非阻塞读取；完整合同见 ws_port.h。
 */
int ws_port_read(uint8_t *bytes, unsigned capacity)
{
    int result = s_port.config->secure ? mbedtls_ssl_read(&s_port.ssl, bytes, capacity) :
                                        mbedtls_net_recv(&s_port.peer, bytes, capacity);
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    return result > 0 ? result : -1;
}

/**
 * @brief 向当前明文或 TLS 对端非阻塞写入；完整合同见 ws_port.h。
 */
int ws_port_write(const uint8_t *bytes, unsigned length)
{
    int result = s_port.config->secure ? mbedtls_ssl_write(&s_port.ssl, bytes, length) :
                                        mbedtls_net_send(&s_port.peer, bytes, length);
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    return result > 0 ? result : -1;
}

/**
 * @brief 从本端已初始化的 DRBG 取得随机字节；完整合同见 ws_port.h。
 */
int ws_port_random(uint8_t *bytes, unsigned length)
{ return mbedtls_ctr_drbg_random(&s_port.random, bytes, length); }

/**
 * @brief 生成 16 字节随机量的 Base64 客户端握手 key；完整合同见 ws_port.h。
 */
int ws_port_key(char key[25])
{
    uint8_t random[16]; size_t length = 0;
    if (ws_port_random(random, sizeof(random)) || mbedtls_base64_encode((unsigned char *)key, 25,
        &length, random, sizeof(random)) || length != 24) return -1;
    key[24] = 0;
    return 0;
}

/**
 * @brief 按 WebSocket GUID 计算握手 accept；完整合同见 ws_port.h。
 */
int ws_port_accept_key(const char *key, char accept[29])
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char input[60]; uint8_t digest[20]; size_t length = 0;
    if (strlen(key) != 24) return -1;
    memcpy(input, key, 24); memcpy(input + 24, guid, sizeof(guid) - 1);
    if (mbedtls_sha1((const unsigned char *)input, sizeof(input), digest) ||
        mbedtls_base64_encode((unsigned char *)accept, 29, &length, digest, sizeof(digest)) || length != 28) return -1;
    accept[28] = 0;
    return 0;
}
#endif
