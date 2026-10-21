/** @file ota_https.c @brief Private strict TLS transport; SDK https_cli is not used. */
#include "config/app_config.h"
#if D_FEATURE_OTA
#include "ota_port.h"
#include "config/certs/ota_isrg_roots.h"
#include "system/includes.h"
#include "framework/d_log.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha256.h"
#include <string.h>

#define OTA_IO_TIMEOUT_MS 10000U
static struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt roots;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_sha256_context hash;
    ota_url_t url;
    char wire[OTA_HEADER_CAPACITY]; /* Request/header storage reused serially. */
    uint8_t initialized;
    uint8_t hash_initialized;
    int hash_error;
} s_https;
static const char s_ota_roots[] = D_OTA_ISRG_ROOTS_PEM;

void ota_transport_close(void)
{
    if (s_https.hash_initialized) {
        mbedtls_sha256_free(&s_https.hash);
        s_https.hash_initialized = 0U;
    }
    if (!s_https.initialized) return;
    /* No unbounded close-notify exchange. No TLS object escapes this owner. */
    mbedtls_net_free(&s_https.net);
    mbedtls_ssl_free(&s_https.ssl);
    mbedtls_ssl_config_free(&s_https.config);
    mbedtls_x509_crt_free(&s_https.roots);
    mbedtls_ctr_drbg_free(&s_https.random);
    mbedtls_entropy_free(&s_https.entropy);
    s_https.initialized = 0U;
}

static d_err_t ota_https_connect(void)
{
    int result;
    uint32_t started;
    static const unsigned char personal[] = "dcode_ota";
    if (ota_port_ready() != D_OK) return D_ERR_NOT_READY;
    mbedtls_net_init(&s_https.net);
    mbedtls_ssl_init(&s_https.ssl);
    mbedtls_ssl_config_init(&s_https.config);
    mbedtls_x509_crt_init(&s_https.roots);
    mbedtls_entropy_init(&s_https.entropy);
    mbedtls_ctr_drbg_init(&s_https.random);
    s_https.initialized = 1U;
    if (mbedtls_ctr_drbg_seed(&s_https.random, mbedtls_entropy_func, &s_https.entropy,
        personal, sizeof(personal) - 1U) != 0 ||
        mbedtls_x509_crt_parse(&s_https.roots, (const unsigned char *)s_ota_roots,
            sizeof(s_ota_roots)) != 0 ||
        mbedtls_ssl_config_defaults(&s_https.config, MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) return D_ERR_IO;
    mbedtls_ssl_conf_authmode(&s_https.config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&s_https.config, &s_https.roots, NULL);
    mbedtls_ssl_conf_rng(&s_https.config, mbedtls_ctr_drbg_random, &s_https.random);
    mbedtls_ssl_conf_read_timeout(&s_https.config, OTA_IO_TIMEOUT_MS);
    if (mbedtls_ssl_setup(&s_https.ssl, &s_https.config) != 0 ||
        mbedtls_ssl_set_hostname(&s_https.ssl, s_https.url.host) != 0) return D_ERR_IO;
    mbedtls_net_set_timeout(&s_https.net, OTA_IO_TIMEOUT_MS, OTA_IO_TIMEOUT_MS);
    s_https.net.connect_to_ms = OTA_IO_TIMEOUT_MS;
    mbedtls_ssl_set_bio(&s_https.ssl, &s_https.net, mbedtls_net_send,
                      mbedtls_net_recv, mbedtls_net_recv_timeout);
    if (mbedtls_net_connect(&s_https.net, s_https.url.host, s_https.url.port,
                            MBEDTLS_NET_PROTO_TCP) != 0) {
        /* This SDK frees each failed socket but leaves hdl pointing at the
         * last freed one. No socket is owned on failure; prevent our close
         * path from passing that stale pointer to mbedtls_net_free again. */
        s_https.net.hdl = NULL;
        return D_ERR_NETWORK;
    }
    started = ota_port_now();
    while ((result = mbedtls_ssl_handshake(&s_https.ssl)) != 0) {
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            LOGE("OTA_TLS", "handshake failed: %d", result);
            return D_ERR_IO;
        }
        if ((uint32_t)(ota_port_now() - started) >= OTA_IO_TIMEOUT_MS) return D_ERR_TIMEOUT;
        ota_port_wait(10U);
    }
    {
        uint32_t flags = mbedtls_ssl_get_verify_result(&s_https.ssl);
        if (flags) LOGE("OTA_TLS", "certificate verification flags=0x%x", (unsigned)flags);
        return flags == 0U ? D_OK : D_ERR_IO;
    }
}

static d_err_t ota_https_send(unsigned length)
{
    unsigned sent = 0U;
    uint32_t progressed = ota_port_now();
    while (sent < length) {
        int count = mbedtls_ssl_write(&s_https.ssl, (const unsigned char *)s_https.wire + sent, length - sent);
        if (count > 0 && (unsigned)count <= length - sent) {
            sent += (unsigned)count; progressed = ota_port_now();
        } else if (count == MBEDTLS_ERR_SSL_WANT_READ || count == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if ((uint32_t)(ota_port_now() - progressed) >= OTA_IO_TIMEOUT_MS) return D_ERR_TIMEOUT;
            ota_port_wait(10U);
        } else return D_ERR_NETWORK;
    }
    return D_OK;
}

int ota_transport_read(uint8_t *bytes, uint16_t capacity)
{
    uint32_t started = ota_port_now();
    if (!s_https.initialized || !bytes || !capacity) return -1;
    for (;;) {
        int count = mbedtls_ssl_read(&s_https.ssl, bytes, capacity);
        if (count == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (count != MBEDTLS_ERR_SSL_WANT_READ && count != MBEDTLS_ERR_SSL_WANT_WRITE) return count;
        if ((uint32_t)(ota_port_now() - started) >= OTA_IO_TIMEOUT_MS) return -1;
        ota_port_wait(10U);
    }
}

d_err_t ota_transport_open(const char *url, uint8_t head, const char *etag,
                           ota_http_response_t *response)
{
    unsigned used = 0U;
    uint32_t started;
    int length;
    d_err_t result;
    ota_transport_close();
    if (!response || ota_url_parse(url, &s_https.url) != D_OK) return D_ERR_INVALID_ARG;
    result = ota_https_connect();
    if (result != D_OK) return result;
    length = snprintf(s_https.wire, sizeof(s_https.wire),
        "%s %s HTTP/1.1\r\nHost: %s:%s\r\nConnection: close\r\nAccept-Encoding: identity\r\n%s%s%s\r\n",
        head ? "HEAD" : "GET", s_https.url.target, s_https.url.host, s_https.url.port,
        etag && etag[0] ? "If-Match: " : "", etag && etag[0] ? etag : "",
        etag && etag[0] ? "\r\n" : "");
    if (length <= 0 || (unsigned)length >= sizeof(s_https.wire)) return D_ERR_INVALID_ARG;
    result = ota_https_send((unsigned)length);
    if (result != D_OK) return result;
    started = ota_port_now();
    while (used + 1U < sizeof(s_https.wire)) {
        if ((uint32_t)(ota_port_now() - started) >= OTA_IO_TIMEOUT_MS) return D_ERR_TIMEOUT;
        if (ota_transport_read((uint8_t *)s_https.wire + used, 1U) != 1) return D_ERR_NETWORK;
        if (s_https.wire[used] == 0) return D_ERR_INVALID_ARG;
        used++;
        if (used >= 4U && !memcmp(s_https.wire + used - 4U, "\r\n\r\n", 4U)) {
            s_https.wire[used] = 0;
            return ota_http_parse(s_https.wire, response);
        }
    }
    return D_ERR_INVALID_ARG;
}

void ota_transport_hash_begin(void)
{
    if (s_https.hash_initialized) mbedtls_sha256_free(&s_https.hash);
    mbedtls_sha256_init(&s_https.hash);
    s_https.hash_initialized = 1U;
    s_https.hash_error = mbedtls_sha256_starts(&s_https.hash, 0);
}
void ota_transport_hash_update(const uint8_t *bytes, uint32_t length)
{
    if (!s_https.hash_error) s_https.hash_error = mbedtls_sha256_update(&s_https.hash, bytes, length);
}
d_err_t ota_transport_hash_finish(uint8_t digest[32])
{
    int result = s_https.hash_error;
    if (!result) result = mbedtls_sha256_finish(&s_https.hash, digest);
    mbedtls_sha256_free(&s_https.hash);
    s_https.hash_initialized = 0U;
    return result ? D_ERR_IO : D_OK;
}
#endif
