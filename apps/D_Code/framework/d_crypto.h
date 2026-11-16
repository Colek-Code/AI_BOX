/**
 * @file    d_crypto.h
 * @brief   D_Code 签名与密码学工具
 * @note    framework 层公共设施，不依赖任何业务模块
 *
 *   - HMAC-SHA256 签名（factory_secret 模式）
 *   - RSA-SHA256  签名（device_cert 模式）
 *   - nonce 随机字符串生成
 *   - 毫秒级时间戳获取（NTP 校时后）
 *   - canonical string 拼接
 */

#ifndef D_CRYPTO_H
#define D_CRYPTO_H

#include "d_error.h"
#include <stddef.h>

//*********************************************************************************//
//                              HMAC-SHA256                                         //
//*********************************************************************************//

/**
 * @brief 计算 HMAC-SHA256，并输出小写十六进制字符串。
 * @param[in] key 密钥字节，调用期间有效。
 * @param[in] key_len 密钥字节数，必须大于 0。
 * @param[in] data 待签名数据，调用期间有效。
 * @param[in] data_len 数据字节数，必须大于 0。
 * @param[out] out_hex 至少 65 字节的缓冲区，用于 64 个十六进制字符和结尾 NUL。
 * @return D_OK 表示成功；D_ERR_INVALID_ARG 表示空指针或非正长度；D_ERR_IO 表示算法不可用或运算失败。
 * @note 同步运算；不保存输入指针。输出容量由调用方保证，接口没有容量参数。失败时不会写入输出。
 */
d_err_t d_hmac_sha256(const unsigned char *key, int key_len,
                      const unsigned char *data, int data_len,
                      char *out_hex);

//*********************************************************************************//
//                              RSA-SHA256 签名                                    //
//*********************************************************************************//

/**
 * @brief 用 PEM 中的 RSA 私钥同步签名，并输出小写十六进制字符串。
 * @param[in] pkey_pem 以 NUL 结尾的未加密 PEM 私钥。
 * @param[in] data 待签名数据，调用期间有效。
 * @param[in] data_len 数据字节数，必须大于 0。
 * @param[out] out_hex 输出缓冲区；失败时内容不变。
 * @param[in] out_capacity 输出字节容量，至少为 RSA 模数字节数的两倍加一。
 * @return D_OK 表示成功；D_ERR_INVALID_ARG 表示参数、密钥类型、模数长度或容量无效；D_ERR_IO 表示解析或签名失败。
 * @note 支持模数不超过 512 字节；RSA-2048 至少需要 513 字节，RSA-4096 至少需要 1025 字节。
 * @note 函数负责释放临时 mbedTLS 密钥上下文，不接管调用方的私钥或数据。
 */
d_err_t d_rsa_sha256_sign(const char *pkey_pem,
                          const unsigned char *data, int data_len,
                          char *out_hex, size_t out_capacity);

//*********************************************************************************//
//                              随机数 / 时间戳                                    //
//*********************************************************************************//

/**
 * @brief 生成以 NUL 结尾的小写十六进制随机字符串。
 * @param[out] buf 至少 len 字节的输出缓冲区。
 * @param[in] len 含结尾 NUL 的容量；大于 1 时生成 len-1 个字符。
 * @note buf 为 NULL 或 len 不大于 1 时直接返回，不写缓冲区。首次使用会以设备 MAC 和启动 tick 初始化随机种子；不提供并发首次初始化保护。
 */
void d_generate_nonce(char *buf, int len);

/**
 * @brief 读取以最近一次 NTP 基准及本地 tick 差值计算的毫秒时间戳。
 * @return 已校时时返回推进的时间戳；未校时时返回固定兼容值 1712000000000，不能据此认定时间可信。
 * @note 本地 tick 单位按 10 ms 计算；不读取 RTC 或编译日期，可信状态另查 d_crypto_timestamp_inited。
 */
unsigned long long d_get_timestamp_ms(void);

/**
 * @brief 查询本次启动是否已设置 NTP 基准。
 * @return 已调用 d_crypto_set_ntp_time 时为 1，否则为 0。
 */
int d_crypto_timestamp_inited(void);

/**
 * @brief 保存 NTP 秒级基准及当前本地 tick。
 * @param[in] epoch_sec 调用方已校验的 NTP 秒级时间戳。
 * @note 在自旋锁内更新基准；本函数不自行校验时间范围。
 */
void d_crypto_set_ntp_time(unsigned long long epoch_sec);

//*********************************************************************************//
//                              规范签名原文                                       //
//*********************************************************************************//

/**
 * @brief 按顺序用竖线拼接签名原文。
 * @param[in] parts 字符串数组；单个 NULL 元素按空字符串处理。
 * @param[in] count 数组元素个数，必须大于 0。
 * @param[out] out 调用方提供的拼接缓冲区。
 * @param[in] out_size 缓冲区字节容量，包含结尾 NUL。
 * @return 成功时为写入字节数，不含 NUL；无效参数或容量不足时为 -1。
 * @note 同步复制且不保存输入；失败时可能已写入部分内容，不保证输出以 NUL 结尾。
 */
int d_build_canonical(const char *parts[], int count, char *out, int out_size);

#endif /* D_CRYPTO_H */
