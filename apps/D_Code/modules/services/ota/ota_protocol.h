/** @file ota_protocol.h @brief Bounded, SDK-independent HTTPS/UFW validation. */
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
    char etag[OTA_ETAG_CAPACITY]; /* Only a strong quoted ETag is retained. */
} ota_http_response_t;
typedef struct {
    uint32_t length;
    uint32_t core_length;
    uint16_t body_crc;
    uint16_t core_crc;
} ota_package_t;

d_err_t ota_url_parse(const char *url, ota_url_t *out);
/** Input includes the terminal CRLFCRLF and is NUL terminated. */
d_err_t ota_http_parse(const char *headers, ota_http_response_t *out);
d_err_t ota_package_parse(const uint8_t prefix[OTA_PACKAGE_PREFIX],
                          uint32_t received_length, ota_package_t *out);
uint16_t ota_crc16(uint16_t initial, const uint8_t *bytes, uint32_t length);
#endif
