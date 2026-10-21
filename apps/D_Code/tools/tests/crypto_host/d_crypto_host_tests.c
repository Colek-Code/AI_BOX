/* RSA capacity and timestamp contract tests, with SDK operations injected. */
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

void mbedtls_pk_init(mbedtls_pk_context *context)
{ context->initialized = 1; ++s_init_calls; }
void mbedtls_pk_free(mbedtls_pk_context *context)
{ CHECK(context->initialized == 1); context->initialized = 0; ++s_free_calls; }
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
size_t mbedtls_pk_get_len(const mbedtls_pk_context *context)
{ (void)context; return s_key_bytes; }
int mbedtls_pk_can_do(const mbedtls_pk_context *context, mbedtls_pk_type_t type)
{ (void)context; CHECK(type == MBEDTLS_PK_RSA); return s_is_rsa; }
int mbedtls_sha256(const unsigned char *input, size_t len, unsigned char *hash,
                   int is224)
{
    ++s_hash_calls;
    CHECK(input != NULL && len == 3 && is224 == 0);
    memset(hash, 0x5A, 32);
    return s_hash_result;
}
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
const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type)
{ static mbedtls_md_info_t info = {1}; (void)type; return &info; }
int mbedtls_md_hmac(const mbedtls_md_info_t *info, const unsigned char *key,
                   size_t key_len, const unsigned char *input, size_t input_len,
                   unsigned char *output)
{
    (void)info; (void)key; (void)key_len; (void)input; (void)input_len;
    memset(output, 0, 32); return 0;
}
u32 os_time_get(void) { return s_tick; }
u32 rand32(void) { return 0x12345678U; }
void wifi_get_mac(u8 *mac) { memset(mac, 0, 6); }

static void reset_sdk(size_t key_bytes)
{
    s_key_bytes = key_bytes; s_signature_bytes = key_bytes; s_is_rsa = 1;
    s_parse_result = 0; s_hash_result = 0; s_sign_result = 0;
    s_init_calls = 0; s_free_calls = 0; s_hash_calls = 0; s_sign_calls = 0;
}

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

int main(void)
{
    timestamp_cases(); capacity_cases(); failure_cases();
    printf("crypto contract tests: %d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
