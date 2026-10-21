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
 * @brief  HMAC-SHA256 计算，输出小写 hex 字符串
 *
 * @param key       密钥
 * @param key_len   密钥长度
 * @param data      待签名数据
 * @param data_len  数据长度
 * @param out_hex   输出缓冲区，至少 65 字节（64 hex + '\0'）
 * @return D_OK 成功，D_ERR_INVALID_ARG 参数无效
 */
d_err_t d_hmac_sha256(const unsigned char *key, int key_len,
                      const unsigned char *data, int data_len,
                      char *out_hex);

//*********************************************************************************//
//                              RSA-SHA256 签名                                    //
//*********************************************************************************//

/**
 * @brief  RSA-SHA256 签名，输出小写 hex 字符串
 *
 * @param pkey_pem  PEM 格式私钥（NUL 结尾）
 * @param data      待签名数据
 * @param data_len  数据长度
 * @param out_hex   输出缓冲区，失败时保持原内容
 * @param out_capacity 缓冲区字节数，包含结尾 '\0'；须至少为 RSA 模数长度的两倍加一
 * @note   支持模数不超过 512 字节的 RSA 私钥；RSA-2048 需 513 字节，RSA-4096 需 1025 字节。
 * @return D_OK 成功；D_ERR_INVALID_ARG 参数、密钥类型/长度或容量无效；D_ERR_IO 签名失败
 */
d_err_t d_rsa_sha256_sign(const char *pkey_pem,
                          const unsigned char *data, int data_len,
                          char *out_hex, size_t out_capacity);

//*********************************************************************************//
//                              随机数 / 时间戳                                    //
//*********************************************************************************//

/**
 * @brief  生成随机 nonce 字符串（十六进制，NUL 结尾）
 *
 * @param buf   输出缓冲区
 * @param len   缓冲区大小（含 '\0'），实际输出 len-1 个 hex 字符
 */
void d_generate_nonce(char *buf, int len);

/**
 * @brief  获取签名用毫秒时间戳：NTP 秒级基准加本地 tick 差值（每 tick 10 ms）
 * @return 校时后返回推进的时间戳；未校时返回固定值 1712000000000，不能证明当前时间可信
 * @note   本接口不读取 RTC 或编译日期；本次启动是否收到 NTP 由 d_crypto_timestamp_inited() 查询。
 */
unsigned long long d_get_timestamp_ms(void);

/** @return 本次启动已调用 d_crypto_set_ntp_time() 时为 1，否则为 0。 */
int d_crypto_timestamp_inited(void);

/**
 * @brief  通知加密模块 NTP 校时已完成
 * @param  epoch_sec  NTP 返回的秒级时间戳
 */
void d_crypto_set_ntp_time(unsigned long long epoch_sec);

//*********************************************************************************//
//                              canonical string                                   //
//*********************************************************************************//

/**
 * @brief  拼接 canonical string：part0|part1|...|partN
 *
 * @param parts     字符串数组
 * @param count     数组元素数
 * @param out       输出缓冲区
 * @param out_size  缓冲区大小
 * @return 实际写入字节数（不含 '\0'），-1 表示缓冲区不足
 */
int d_build_canonical(const char *parts[], int count, char *out, int out_size);

#endif /* D_CRYPTO_H */
