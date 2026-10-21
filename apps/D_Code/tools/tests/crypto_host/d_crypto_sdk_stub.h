/* Host-only SDK boundary declarations; the signing helper itself is production C. */
#ifndef D_CRYPTO_SDK_STUB_H
#define D_CRYPTO_SDK_STUB_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int mbedtls_md_type_t;
typedef int mbedtls_pk_type_t;
typedef struct { int initialized; } mbedtls_pk_context;
typedef struct { int available; } mbedtls_md_info_t;

#define MBEDTLS_MD_SHA256 4
#define MBEDTLS_PK_RSA 1

void mbedtls_pk_init(mbedtls_pk_context *context);
void mbedtls_pk_free(mbedtls_pk_context *context);
int mbedtls_pk_parse_key(mbedtls_pk_context *context, const unsigned char *key,
                        size_t key_len, const unsigned char *password,
                        size_t password_len,
                        int (*rng)(void *, unsigned char *, size_t), void *rng_context);
size_t mbedtls_pk_get_len(const mbedtls_pk_context *context);
int mbedtls_pk_can_do(const mbedtls_pk_context *context, mbedtls_pk_type_t type);
int mbedtls_pk_sign(mbedtls_pk_context *context, mbedtls_md_type_t type,
                   const unsigned char *hash, size_t hash_len,
                   unsigned char *signature, size_t capacity, size_t *signature_len,
                   int (*rng)(void *, unsigned char *, size_t), void *rng_context);
int mbedtls_sha256(const unsigned char *input, size_t len, unsigned char *hash,
                   int is224);
const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type);
int mbedtls_md_hmac(const mbedtls_md_info_t *info, const unsigned char *key,
                   size_t key_len, const unsigned char *input, size_t input_len,
                   unsigned char *output);
u32 os_time_get(void);
u32 rand32(void);
int printf(const char *format, ...);
int snprintf(char *output, size_t capacity, const char *format, ...);

#endif
