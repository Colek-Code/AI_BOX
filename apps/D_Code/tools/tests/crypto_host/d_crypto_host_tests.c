/** @file d_crypto_host_tests.c @brief 真实 d_crypto 容量、失败收尾与校时时间合同的主机测试；SDK 运算为注入替身。 */
/* 运行真实 RSA 容量与时间合同，SDK 运算由夹具注入。 */
#include "d_crypto_sdk_stub.h"
#include "framework/d_crypto.h"
#include <string.h>

static size_t s_key_bytes;
static size_t s_signature_bytes;
static int s_is_rsa;
static int s_parse_result;
static int s_hash_result;
static int s_sign_result;
static int s_init_calls;
static int s_free_calls;
static int s_hash_calls;
static int s_sign_calls;
static u32 s_tick;
static int s_checks;
static int s_failures;

#define CHECK(condition) do { ++s_checks; if (!(condition)) { \
    ++s_failures; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

/**
 * @brief 设置合成密钥上下文的初始化标记并统计调用。
 * @param[out] context 非空合成上下文，仅写 initialized=1。
 */
void mbedtls_pk_init(mbedtls_pk_context *context)
{ context->initialized = 1; ++s_init_calls; }
/**
 * @brief 检查上下文已初始化后清空标记并统计释放；不释放真实密钥资源。
 * @param[in,out] context 非空合成上下文。
 */
void mbedtls_pk_free(mbedtls_pk_context *context)
{ CHECK(context->initialized == 1); context->initialized = 0; ++s_free_calls; }
/**
 * @brief 检查 PEM 长度包含结尾零、无密码及 RNG 非空，再返回注入解析结果。
 * @param[in] context 兼容上下文，不读写。
 * @param[in] key 非空零结尾密钥文本，供 strlen 检查。
 * @param[in] key_len 应等于文本长度加 1。
 * @param[in] password 兼容密码指针，不读取。
 * @param[in] password_len 应为零。
 * @param[in] rng 应非空，本入口不调用。
 * @param[in] rng_context 兼容 RNG 上下文，不读取。
 * @return s_parse_result；不解析真实密钥，也不改写 context。
 */
int mbedtls_pk_parse_key(mbedtls_pk_context *context, const unsigned char *key,
                        size_t key_len, const unsigned char *password,
                        size_t password_len,
                        int (*rng)(void *, unsigned char *, size_t), void *rng_context)
{
    (void)context; (void)password; (void)rng_context;
    CHECK(key != NULL && key_len == strlen((const char *)key) + 1U);
    CHECK(password_len == 0 && rng != NULL);
    return s_parse_result;
}
/**
 * @brief 读取夹具注入的密钥字节数。
 * @param[in] context 兼容上下文，不读取。
 * @return s_key_bytes。
 */
size_t mbedtls_pk_get_len(const mbedtls_pk_context *context)
{ (void)context; return s_key_bytes; }
/**
 * @brief 检查请求算法为 RSA，再返回注入的密钥类型标记。
 * @param[in] context 兼容上下文，不读取。
 * @param[in] type 应为 MBEDTLS_PK_RSA。
 * @return s_is_rsa。
 */
int mbedtls_pk_can_do(const mbedtls_pk_context *context, mbedtls_pk_type_t type)
{ (void)context; CHECK(type == MBEDTLS_PK_RSA); return s_is_rsa; }
/**
 * @brief 检查三字节输入与 SHA256 选项，将合成摘要填为 0x5A 并统计。
 * @param[in] input 应非空，不读取正文。
 * @param[in] len 应为 3。
 * @param[out] hash 至少 32 字节输出。
 * @param[in] is224 应为零。
 * @return s_hash_result；包括注入失败时，32 字节 hash 也已经填入。
 */
int mbedtls_sha256(const unsigned char *input, size_t len, unsigned char *hash,
                   int is224)
{
    ++s_hash_calls;
    CHECK(input != NULL && len == 3 && is224 == 0);
    memset(hash, 0x5A, 32);
    return s_hash_result;
}
/**
 * @brief 核对摘要和 512 字节容量，调用生产 RNG 适配器后填入有界合成签名。
 * @param[in] context 兼容上下文，不读取。
 * @param[in] type 应为 MBEDTLS_MD_SHA256。
 * @param[in] hash 至少 1 字节可读摘要，首字节应为 0x5A。
 * @param[in] hash_len 应为 32。
 * @param[out] signature 至少 capacity 字节输出，最多写 min(s_signature_bytes,capacity) 字节。
 * @param[in] capacity 生产调用应为 512；容量不匹配会记录检查失败。
 * @param[out] signature_len 非空输出，写入未截断的 s_signature_bytes。
 * @param[in] rng 有效 RNG 回调，容量检查成立时调用并要求返回 0。
 * @param[in] rng_context 透传给 RNG 的上下文。
 * @return s_sign_result；签名字节和 signature_len 在注入失败时也已写入。
 * @note 本替身产生递增字节序列，不执行 RSA 运算；CHECK 记录失败后继续执行。
 */
int mbedtls_pk_sign(mbedtls_pk_context *context, mbedtls_md_type_t type,
                   const unsigned char *hash, size_t hash_len,
                   unsigned char *signature, size_t capacity, size_t *signature_len,
                   int (*rng)(void *, unsigned char *, size_t), void *rng_context)
{
    unsigned char random_bytes[7];
    size_t count = s_signature_bytes < capacity ? s_signature_bytes : capacity;
    size_t index;
    (void)context;
    ++s_sign_calls;
    CHECK(type == MBEDTLS_MD_SHA256 && hash_len == 32 && hash[0] == 0x5A);
    CHECK(capacity == 512 && rng(rng_context, random_bytes, sizeof(random_bytes)) == 0);
    for (index = 0; index < count; ++index) signature[index] = (unsigned char)index;
    *signature_len = s_signature_bytes;
    return s_sign_result;
}
/**
 * @brief 借用固定 available=1 的静态摘要描述，不按算法类型筛选。
 * @param[in] type 兼容算法类型，不读取。
 * @return 持续有效的静态描述指针。
 */
const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type)
{ static mbedtls_md_info_t info = {1}; (void)type; return &info; }
/**
 * @brief 为链接生产 HMAC 入口提供零摘要替身，不计算真实 HMAC。
 * @param[in] info 兼容描述，不读取。
 * @param[in] key 兼容密钥，不读取。
 * @param[in] key_len 兼容密钥长度，不读取。
 * @param[in] input 兼容正文，不读取。
 * @param[in] input_len 兼容正文长度，不读取。
 * @param[out] output 至少 32 字节输出，全部置零。
 * @return 固定为 0。
 */
int mbedtls_md_hmac(const mbedtls_md_info_t *info, const unsigned char *key,
                   size_t key_len, const unsigned char *input, size_t input_len,
                   unsigned char *output)
{
    (void)info; (void)key; (void)key_len; (void)input; (void)input_len;
    memset(output, 0, 32); return 0;
}
/**
 * @brief 读取夹具控制的合成 tick，生产时间换算按每 tick 10 ms 使用。
 * @return s_tick。
 */
u32 os_time_get(void) { return s_tick; }
/**
 * @brief 提供固定 RNG 字节来源，不模拟硬件熵。
 * @return 固定为 0x12345678U。
 */
u32 rand32(void) { return 0x12345678U; }
/**
 * @brief 为生产 nonce 入口的链接依赖填入全零合成 MAC，不读取网卡。
 * @param[out] mac 至少 6 字节输出。
 */
void wifi_get_mac(u8 *mac) { memset(mac, 0, 6); }

/**
 * @brief 设置密钥与签名尺寸，恢复成功结果和 RSA 标记，并清零 SDK 调用计数。
 * @param[in] key_bytes 合成密钥字节数，同时作为默认签名长度。
 */
static void reset_sdk(size_t key_bytes)
{
    s_key_bytes = key_bytes; s_signature_bytes = key_bytes; s_is_rsa = 1;
    s_parse_result = 0; s_hash_result = 0; s_sign_result = 0;
    s_init_calls = 0; s_free_calls = 0; s_hash_calls = 0; s_sign_calls = 0;
}

/**
 * @brief 调用真实 RSA 签名入口，检查返回、资源配对、调用次数及输出两侧哨兵。
 * @param[in] capacity 传给生产接口的输出容量，夹具实际提供 1025 字节。
 * @param[in] expected 预期返回；成功检查完整小写十六进制，失败检查全部字节未变。
 * @param[in] expected_sign_calls 预期 SDK 签名调用次数。
 */
static void check_output(size_t capacity, d_err_t expected, int expected_sign_calls)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char guarded[1027];
    char *output = (char *)(guarded + 1);
    size_t index;
    memset(guarded, 0xA5, sizeof(guarded));
    CHECK(d_rsa_sha256_sign("private-key", (const unsigned char *)"abc", 3,
                            output, capacity) == expected);
    CHECK(s_init_calls == 1 && s_free_calls == 1);
    CHECK(s_sign_calls == expected_sign_calls);
    CHECK(guarded[0] == 0xA5 && guarded[1026] == 0xA5);
    if (expected == D_OK) {
        for (index = 0; index < s_signature_bytes; ++index) {
            unsigned char byte = (unsigned char)index;
            CHECK(output[index * 2] == hex[byte >> 4]);
            CHECK(output[index * 2 + 1] == hex[byte & 15]);
        }
        CHECK(output[s_signature_bytes * 2] == '\0');
        for (index = s_signature_bytes * 2 + 1; index < 1025; ++index)
            CHECK((unsigned char)output[index] == 0xA5);
    } else {
        for (index = 0; index < sizeof(guarded); ++index) CHECK(guarded[index] == 0xA5);
    }
}

/**
 * @brief 覆盖 256/512 字节密钥的恰足、富余和不足容量，以及零长、过大和非 RSA 密钥。
 */
static void capacity_cases(void)
{
    reset_sdk(256); check_output(513, D_OK, 1);
    reset_sdk(256); check_output(1025, D_OK, 1);
    reset_sdk(256); check_output(512, D_ERR_INVALID_ARG, 0); CHECK(s_hash_calls == 0);
    reset_sdk(512); check_output(1025, D_OK, 1);
    reset_sdk(512); check_output(513, D_ERR_INVALID_ARG, 0); CHECK(s_hash_calls == 0);
    reset_sdk(512); check_output(1024, D_ERR_INVALID_ARG, 0); CHECK(s_hash_calls == 0);
    reset_sdk(0); check_output(1025, D_ERR_INVALID_ARG, 0);
    reset_sdk(513); check_output(1025, D_ERR_INVALID_ARG, 0);
    reset_sdk(256); s_is_rsa = 0; check_output(513, D_ERR_INVALID_ARG, 0);
}

/**
 * @brief 覆盖无效参数、解析/摘要/签名失败及异常签名长度，检查失败不改调用方输出。
 */
static void failure_cases(void)
{
    char output[513];
    const unsigned char *input = (const unsigned char *)"abc";
    size_t index;
    reset_sdk(256); memset(output, 0xA5, sizeof(output));
    CHECK(d_rsa_sha256_sign(NULL, input, 3, output, sizeof(output)) == D_ERR_INVALID_ARG);
    CHECK(d_rsa_sha256_sign("key", NULL, 3, output, sizeof(output)) == D_ERR_INVALID_ARG);
    CHECK(d_rsa_sha256_sign("key", input, 0, output, sizeof(output)) == D_ERR_INVALID_ARG);
    CHECK(d_rsa_sha256_sign("key", input, -1, output, sizeof(output)) == D_ERR_INVALID_ARG);
    CHECK(d_rsa_sha256_sign("key", input, 3, NULL, sizeof(output)) == D_ERR_INVALID_ARG);
    CHECK(d_rsa_sha256_sign("key", input, 3, output, 0) == D_ERR_INVALID_ARG);
    CHECK(s_init_calls == 0 && s_free_calls == 0);
    for (index = 0; index < sizeof(output); ++index) CHECK((unsigned char)output[index] == 0xA5);
    reset_sdk(256); s_parse_result = -1; check_output(513, D_ERR_IO, 0);
    reset_sdk(256); s_hash_result = -2; check_output(513, D_ERR_IO, 0);
    reset_sdk(256); s_sign_result = -3; check_output(513, D_ERR_IO, 1);
    reset_sdk(256); s_signature_bytes = 0; check_output(513, D_ERR_IO, 1);
    reset_sdk(256); s_signature_bytes = 255; check_output(513, D_ERR_IO, 1);
    reset_sdk(256); s_signature_bytes = 512; check_output(513, D_ERR_IO, 1);
    reset_sdk(512); s_signature_bytes = 513; check_output(1025, D_ERR_IO, 1);
}

/**
 * @brief 验证未校时固定兼容值、NTP 可信状态、每 tick 10 ms 推进及 32 位 tick 回绕。
 */
static void timestamp_cases(void)
{
    CHECK(d_crypto_timestamp_inited() == 0);
    CHECK(d_get_timestamp_ms() == 1712000000000ULL);
    s_tick = 400;
    CHECK(d_get_timestamp_ms() == 1712000000000ULL);
    d_crypto_set_ntp_time(1800000000ULL);
    CHECK(d_crypto_timestamp_inited() == 1);
    CHECK(d_get_timestamp_ms() == 1800000000000ULL);
    s_tick = 425;
    CHECK(d_get_timestamp_ms() == 1800000000250ULL);
    s_tick = 0xFFFFFFF0U;
    d_crypto_set_ntp_time(1800000100ULL);
    s_tick = 0x20U;
    CHECK(d_get_timestamp_ms() == 1800000100480ULL);
}

/**
 * @brief 依次执行时间、容量和失败合同检查，输出检查总数。
 * @return 所有 CHECK 通过时为 0，否则为 1。
 */
int main(void)
{
    timestamp_cases(); capacity_cases(); failure_cases();
    printf("crypto contract tests: %d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
