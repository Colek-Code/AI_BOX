/** @file ota_protocol.h @brief 独立于 SDK 的有界 HTTPS 响应与 UFW 前缀校验。 */
#ifndef D_OTA_PROTOCOL_H
#define D_OTA_PROTOCOL_H
#include <stdint.h>
#include "framework/d_error.h"

#define OTA_URL_CAPACITY 1024U
#define OTA_ETAG_CAPACITY 192U
#define OTA_HEADER_CAPACITY 2048U
#define OTA_PACKET_SIZE 4096U
#define OTA_PACKAGE_PREFIX 64U
typedef struct {
    char host[254];
    char port[6];
    char target[OTA_URL_CAPACITY];
} ota_url_t;
typedef struct {
    uint32_t length;
    uint16_t status;
    uint8_t has_length;
    char etag[OTA_ETAG_CAPACITY]; /* 仅保留带引号的强 ETag。 */
} ota_http_response_t;
typedef struct {
    uint32_t length;
    uint32_t core_length;
    uint16_t body_crc;
    uint16_t core_crc;
} ota_package_t;

/**
 * @brief 解析有界 HTTPS URL，不接受用户信息、片段或 IPv6 字面量。
 * @param[in] url 零结尾 HTTPS URL，长度小于 OTA_URL_CAPACITY。
 * @param[out] out 主机、端口与请求目标输出。
 * @return 有效返回 D_OK；格式、端口或容量无效返回 D_ERR_INVALID_ARG。
 */
d_err_t ota_url_parse(const char *url, ota_url_t *out);
/**
 * @brief 解析完整 HTTP 响应头并校验长度与编码约束。
 * @param[in] headers 含末尾 CRLFCRLF 的零结尾响应头。
 * @param[out] out 状态码、长度和可用强 ETag 输出；失败时可能已部分写入。
 * @return 有效返回 D_OK；格式、重复字段或不支持的编码返回 D_ERR_INVALID_ARG。
 * @note 仅保留强引号 ETag；不支持 Transfer-Encoding。此函数不单独要求状态为 200。
 */
d_err_t ota_http_parse(const char *headers, ota_http_response_t *out);
/**
 * @brief 校验 UFW 前缀记录、头 CRC 与声明长度。
 * @param[in] prefix 至少 OTA_PACKAGE_PREFIX 字节的包前缀。
 * @param[in] received_length 完整接收包的字节数。
 * @param[out] out 验证通过后的包长度与正文 CRC 输出。
 * @return 有效返回 D_OK；格式、CRC 或长度不匹配返回 D_ERR_INVALID_ARG。
 * @note 只验证前缀与记录关系，正文和 app_core 的实际 CRC 由调用者继续核对。
 */
d_err_t ota_package_parse(const uint8_t prefix[OTA_PACKAGE_PREFIX],
                          uint32_t received_length, ota_package_t *out);
/**
 * @brief 以多项式 0x1021 增量计算 CRC16。
 * @param[in] initial 此前累计 CRC 或初始值。
 * @param[in] bytes 至少 length 字节的输入。
 * @param[in] length 输入字节数。
 * @return 本段更新后的 CRC16，可作为下一段 initial。
 */
uint16_t ota_crc16(uint16_t initial, const uint8_t *bytes, uint32_t length);
#endif
