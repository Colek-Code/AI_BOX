/** @file qwen_credential_format.h @brief SDK 归属记录与独立全托管存储共用的序列化约定。 */
#ifndef QWEN_CREDENTIAL_FORMAT_H
#define QWEN_CREDENTIAL_FORMAT_H
#include <stddef.h>
#include <stdint.h>
#define QWEN_AUTH_OWNER_SIZE 48U
#define QWEN_AUTH_VM_OWNER 33U
/** @brief 计算既有归属记录的反射 CRC32，接口由接入库提供。 */
uint32_t qwen_auth_crc32(const uint8_t *bytes, size_t size);
/** @brief 验证 QOWN 魔数、版本、保留位和 CRC，成功返回保存的模式，其他情况返回 -1。 */
int qwen_auth_owner_mode(const uint8_t bytes[QWEN_AUTH_OWNER_SIZE]);
#endif
