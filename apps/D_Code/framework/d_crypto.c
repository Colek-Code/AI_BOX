/**
 * @file    d_crypto.c
 * @brief   D_Code 签名与密码学工具实现
 * @note    依赖 mbedtls（SDK 内置）、硬件 RNG (rand32)
 */

#include "config/app_config.h"

#ifdef CONFIG_MBEDTLS_ENABLE

#include "d_crypto.h"
#include "d_log.h"
#include "framework/compat/d_stdio_compat.h"

#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#include <string.h>
#include "system/includes.h"    /* rand32(), os_time_get() */
#include <stdlib.h>              /* srand() */

#define TAG "CRYPTO"
#define D_CRYPTO_RSA_SIGNATURE_MAX_BYTES 512U

//*********************************************************************************//
//                              内部工具                                            //
//*********************************************************************************//

/* 单字节转两位小写 hex */
static void byte_to_hex(unsigned char b, char *out)
{
    static const char s_hex[] = "0123456789abcdef";
    out[0] = s_hex[(b >> 4) & 0x0F];
    out[1] = s_hex[b & 0x0F];
}

/* 二进制数组转小写 hex 字符串 */
static void bin_to_hex(const unsigned char *bin, int bin_len, char *hex)
{
    int i;
    for (i = 0; i < bin_len; i++) {
        byte_to_hex(bin[i], &hex[i * 2]);
    }
    hex[bin_len * 2] = '\0';
}

//*********************************************************************************//
//                              NTP 时间管理                                       //
//*********************************************************************************//

static volatile unsigned long long s_ntp_epoch_sec = 0;
static volatile u32 s_ntp_local_tick = 0;   /* NTP 校时时刻的本地 tick */
static volatile int s_ntp_synced = 0;

void d_crypto_set_ntp_time(unsigned long long epoch_sec)
{
    s_ntp_epoch_sec  = epoch_sec;
    s_ntp_local_tick = os_time_get();
    s_ntp_synced     = 1;
    LOGI(TAG, "NTP 校时: epoch=%llu", epoch_sec);
}

int d_crypto_timestamp_inited(void)
{
    return s_ntp_synced;
}

unsigned long long d_get_timestamp_ms(void)
{
    if (s_ntp_synced) {
        /* NTP 基准 + 本地 tick 差值（tick 单位 10ms） */
        u32 elapsed_tick = os_time_get() - s_ntp_local_tick;
        unsigned long long ms = (unsigned long long)s_ntp_epoch_sec * 1000ULL
                              + (unsigned long long)elapsed_tick * 10ULL;
        return ms;
    }

    /* 保留既有签名兼容值；它不是当前时间，调用方须另查校时状态。 */
    LOGW(TAG, "NTP 未校时，返回固定兼容时间戳，当前时间不可信");
    return 1712000000000ULL;
}

//*********************************************************************************//
//                              HMAC-SHA256                                         //
//*********************************************************************************//

d_err_t d_hmac_sha256(const unsigned char *key, int key_len,
                      const unsigned char *data, int data_len,
                      char *out_hex)
{
    if (!key || key_len <= 0 || !data || data_len <= 0 || !out_hex) {
        return D_ERR_INVALID_ARG;
    }

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        LOGE(TAG, "SHA256 md_info 不可用");
        return D_ERR_IO;
    }

    unsigned char hmac[32];
    int ret = mbedtls_md_hmac(md_info, key, (size_t)key_len,
                              data, (size_t)data_len, hmac);
    if (ret != 0) {
        LOGE(TAG, "mbedtls_md_hmac 失败: -0x%04X", (unsigned int)-ret);
        return D_ERR_IO;
    }

    bin_to_hex(hmac, 32, out_hex);
    return D_OK;
}

//*********************************************************************************//
//                              RSA-SHA256 签名                                    //
//*********************************************************************************//

/* mbedtls RNG 回调：使用硬件 rand32() */
static int hw_rng_callback(void *ctx, unsigned char *output, size_t len)
{
    (void)ctx;
    size_t i;
    for (i = 0; i + 3 < len; i += 4) {
        u32 r = rand32();
        memcpy(&output[i], &r, 4);
    }
    if (i < len) {
        u32 r = rand32();
        memcpy(&output[i], &r, len - i);
    }
    return 0;
}

d_err_t d_rsa_sha256_sign(const char *pkey_pem,
                          const unsigned char *data, int data_len,
                          char *out_hex, size_t out_capacity)
{
    if (!pkey_pem || !data || data_len <= 0 || !out_hex || out_capacity == 0) {
        return D_ERR_INVALID_ARG;
    }

    d_err_t result = D_ERR_IO;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    /* 解析 PEM 私钥 */
    int ret = mbedtls_pk_parse_key(&pk,
                                   (const unsigned char *)pkey_pem,
                                   strlen(pkey_pem) + 1,
                                   NULL, 0,
                                   hw_rng_callback, NULL);
    if (ret != 0) {
        LOGE(TAG, "pk_parse_key 失败: -0x%04X", (unsigned int)-ret);
        goto cleanup;
    }

    /* 先核对密钥及输出容量，签名成功之前不改动调用方缓冲区。 */
    size_t key_bytes = mbedtls_pk_get_len(&pk);
    if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA) || key_bytes == 0 ||
        key_bytes > D_CRYPTO_RSA_SIGNATURE_MAX_BYTES ||
        out_capacity < key_bytes * 2U + 1U) {
        result = D_ERR_INVALID_ARG;
        goto cleanup;
    }

    /* 计算 SHA256 摘要 */
    unsigned char hash[32];
    ret = mbedtls_sha256(data, (size_t)data_len, hash, 0);
    if (ret != 0) {
        LOGE(TAG, "sha256 失败: -0x%04X", (unsigned int)-ret);
        goto cleanup;
    }

    /* RSA 签名 */
    unsigned char sig[D_CRYPTO_RSA_SIGNATURE_MAX_BYTES];
    size_t sig_len = 0;
    ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256,
                          hash, 32,
                          sig, sizeof(sig), &sig_len,
                          hw_rng_callback, NULL);
    if (ret != 0) {
        LOGE(TAG, "pk_sign 失败: -0x%04X", (unsigned int)-ret);
        goto cleanup;
    }
    if (sig_len != key_bytes || sig_len > sizeof(sig)) {
        LOGE(TAG, "RSA 签名长度无效: %u", (unsigned int)sig_len);
        goto cleanup;
    }

    bin_to_hex(sig, (int)sig_len, out_hex);
    result = D_OK;

cleanup:
    mbedtls_pk_free(&pk);
    return result;
}

//*********************************************************************************//
//                              随机 nonce                                         //
//*********************************************************************************//

void d_generate_nonce(char *buf, int len)
{
    if (!buf || len <= 1) return;

    /* 首次调用时用 MAC + tick 混合熵源初始化随机序列 */
    static int s_seeded = 0;
    if (!s_seeded) {
        s_seeded = 1;
        u8 mac[6] = {0};
        extern void wifi_get_mac(u8 *mac);  /* SDK 提供，返回 6 字节数组 */
        wifi_get_mac(mac);
        /* 将 MAC 折叠为 32 位：mac[0..2] XOR mac[3..5]。 */
        u32 mac_seed = ((u32)mac[0] << 16 | (u32)mac[1] << 8 | mac[2])
                     ^ ((u32)mac[3] << 16 | (u32)mac[4] << 8 | mac[5]);
        u32 tick_seed = os_time_get();
        /* srand 接受 unsigned int，种子混合设备 MAC 与启动 tick。 */
        srand((unsigned int)(mac_seed ^ tick_seed));
        LOGD(TAG, "nonce seed: mac=0x%06x tick=%u", (unsigned)mac_seed, (unsigned)tick_seed);
    }

    int hex_chars = len - 1;  /* 预留 '\0' */
    int i;
    for (i = 0; i < hex_chars; i += 8) {
        u32 r = rand32();
        char tmp[9];
        snprintf(tmp, sizeof(tmp), "%08x", r);
        int copy_len = (hex_chars - i >= 8) ? 8 : (hex_chars - i);
        memcpy(&buf[i], tmp, copy_len);
    }
    buf[hex_chars] = '\0';
}

//*********************************************************************************//
//                              canonical string                                   //
//*********************************************************************************//

int d_build_canonical(const char *parts[], int count, char *out, int out_size)
{
    if (!parts || count <= 0 || !out || out_size <= 0) {
        return -1;
    }

    int pos = 0;
    int i;
    for (i = 0; i < count; i++) {
        const char *p = parts[i] ? parts[i] : "";
        int plen = (int)strlen(p);

        /* 分隔符 '|'（第一个元素前不加） */
        if (i > 0) {
            if (pos + 1 >= out_size) return -1;
            out[pos++] = '|';
        }

        if (pos + plen >= out_size) return -1;
        memcpy(&out[pos], p, plen);
        pos += plen;
    }

    out[pos] = '\0';
    return pos;
}

#endif /* CONFIG_MBEDTLS_ENABLE */
