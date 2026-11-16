/** @file d_crypto_sdk_stub.h @brief crypto 主机夹具的最小 SDK ABI；签名辅助函数来自生产 C，替身不验收密码学算法。 */
/* 仅声明主机 SDK 边界，签名辅助函数本身仍使用生产 C。 */
#ifndef D_CRYPTO_SDK_STUB_H
#define D_CRYPTO_SDK_STUB_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int mbedtls_md_type_t;
typedef int mbedtls_pk_type_t;
#define DEFINE_SPINLOCK(name) int name
/**
 * @brief 保持 SDK 锁调用兼容的空操作，不保护并发。
 * @param[in] lock 兼容锁对象，不读取或写入。
 */
static inline void spin_lock(int *lock) { (void)lock; }
/**
 * @brief 保持 SDK 解锁调用兼容的空操作，不改变锁状态。
 * @param[in] lock 兼容锁对象，不读取或写入。
 */
static inline void spin_unlock(int *lock) { (void)lock; }
typedef struct { int initialized; } mbedtls_pk_context;
typedef struct { int available; } mbedtls_md_info_t;

#define MBEDTLS_MD_SHA256 4
#define MBEDTLS_PK_RSA 1

/**
 * @brief 设置合成密钥上下文的初始化标记并统计调用。
 * @param[out] context 非空合成上下文，仅写 initialized=1。
 */
void mbedtls_pk_init(mbedtls_pk_context *context);
/**
 * @brief 检查上下文已初始化后清空标记并统计释放；不释放真实密钥资源。
 * @param[in,out] context 非空合成上下文。
 */
void mbedtls_pk_free(mbedtls_pk_context *context);
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
                        int (*rng)(void *, unsigned char *, size_t), void *rng_context);
/**
 * @brief 读取夹具注入的密钥字节数。
 * @param[in] context 兼容上下文，不读取。
 * @return s_key_bytes。
 */
size_t mbedtls_pk_get_len(const mbedtls_pk_context *context);
/**
 * @brief 检查请求算法为 RSA，再返回注入的密钥类型标记。
 * @param[in] context 兼容上下文，不读取。
 * @param[in] type 应为 MBEDTLS_PK_RSA。
 * @return s_is_rsa。
 */
int mbedtls_pk_can_do(const mbedtls_pk_context *context, mbedtls_pk_type_t type);
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
                   int (*rng)(void *, unsigned char *, size_t), void *rng_context);
/**
 * @brief 检查三字节输入与 SHA256 选项，将合成摘要填为 0x5A 并统计。
 * @param[in] input 应非空，不读取正文。
 * @param[in] len 应为 3。
 * @param[out] hash 至少 32 字节输出。
 * @param[in] is224 应为零。
 * @return s_hash_result；包括注入失败时，32 字节 hash 也已经填入。
 */
int mbedtls_sha256(const unsigned char *input, size_t len, unsigned char *hash,
                   int is224);
/**
 * @brief 借用固定 available=1 的静态摘要描述，不按算法类型筛选。
 * @param[in] type 兼容算法类型，不读取。
 * @return 持续有效的静态描述指针。
 */
const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type);
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
                   unsigned char *output);
/**
 * @brief 读取夹具控制的合成 tick，生产时间换算按每 tick 10 ms 使用。
 * @return s_tick。
 */
u32 os_time_get(void);
/**
 * @brief 提供固定 RNG 字节来源，不模拟硬件熵。
 * @return 固定为 0x12345678U。
 */
u32 rand32(void);
/**
 * @brief 声明宿主运行库的格式化输出，供 CHECK 和测试汇总使用。
 * @param[in] format 有效格式串，后续变参与格式一致。
 * @return 宿主运行库返回的字符数，失败时为负值。
 */
int printf(const char *format, ...);
/**
 * @brief 声明宿主运行库的有界格式化输出，供生产 nonce 入口链接使用。
 * @param[out] output 容量为 capacity 的可写缓冲；capacity 为零时不写。
 * @param[in] capacity 缓冲总容量。
 * @param[in] format 有效格式串，后续变参与格式一致。
 * @return 所需字符数，不含结尾零；负值表示宿主编码失败，结果不小于 capacity 表示截断。
 */
int snprintf(char *output, size_t capacity, const char *format, ...);

#endif
