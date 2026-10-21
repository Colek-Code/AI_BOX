/** @file ota_protocol.c @brief Validate before any SDK/Flash side effect. */
#include "ota_protocol.h"
#include <string.h>

static unsigned ota_bounded_length(const char *text, unsigned capacity)
{
    unsigned length = 0U;
    if (text == NULL) return capacity;
    while (length < capacity && text[length]) length++;
    return length;
}

static int ota_ascii_equal(const char *text, unsigned length, const char *expected)
{
    unsigned index;
    if (strlen(expected) != length) return 0;
    for (index = 0U; index < length; index++) {
        unsigned char value = (unsigned char)text[index];
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        if (value != (unsigned char)expected[index]) return 0;
    }
    return 1;
}

d_err_t ota_url_parse(const char *url, ota_url_t *out)
{
    unsigned length = ota_bounded_length(url, OTA_URL_CAPACITY);
    unsigned index, host_end, target_start, port_start = 0U;
    uint32_t port = 0U;
    if (out == NULL || length < 9U || length >= OTA_URL_CAPACITY ||
        memcmp(url, "https://", 8U) != 0) return D_ERR_INVALID_ARG;
    for (index = 8U; index < length; index++) {
        unsigned char value = (unsigned char)url[index];
        if (value <= 0x20U || value >= 0x7fU || value == '#' || value == '\\')
            return D_ERR_INVALID_ARG;
    }
    target_start = 8U;
    while (target_start < length && url[target_start] != '/' && url[target_start] != '?')
        target_start++;
    host_end = target_start;
    for (index = 8U; index < target_start; index++) {
        char value = url[index];
        if (value == ':') {
            if (port_start) return D_ERR_INVALID_ARG;
            host_end = index;
            port_start = index + 1U;
        } else if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                     (value >= '0' && value <= '9') || value == '.' || value == '-'))
            return D_ERR_INVALID_ARG;
    }
    if (host_end == 8U || host_end - 8U >= sizeof(out->host)) return D_ERR_INVALID_ARG;
    if (port_start) {
        if (port_start == target_start || target_start - port_start >= sizeof(out->port))
            return D_ERR_INVALID_ARG;
        for (index = port_start; index < target_start; index++) {
            if (url[index] < '0' || url[index] > '9') return D_ERR_INVALID_ARG;
            port = port * 10U + (unsigned)(url[index] - '0');
        }
        if (port == 0U || port > 65535U) return D_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->host, url + 8U, host_end - 8U);
    if (port_start) memcpy(out->port, url + port_start, target_start - port_start);
    else memcpy(out->port, "443", 4U);
    if (target_start == length) memcpy(out->target, "/", 2U);
    else if (url[target_start] == '?') {
        out->target[0] = '/';
        memcpy(out->target + 1U, url + target_start, length - target_start);
    } else memcpy(out->target, url + target_start, length - target_start);
    return D_OK;
}

static int ota_decimal(const char *text, unsigned length, uint32_t *out)
{
    uint32_t value = 0U;
    unsigned index;
    if (length == 0U) return 0;
    for (index = 0U; index < length; index++) {
        unsigned digit = (unsigned char)text[index] - '0';
        if (digit > 9U || value > (UINT32_MAX - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    *out = value;
    return 1;
}

d_err_t ota_http_parse(const char *headers, ota_http_response_t *out)
{
    unsigned length = ota_bounded_length(headers, OTA_HEADER_CAPACITY);
    const char *line, *end, *colon, *value, *last;
    uint32_t status;
    uint8_t etag_seen = 0U;
    if (out == NULL || length < 19U || length >= OTA_HEADER_CAPACITY ||
        (memcmp(headers, "HTTP/1.1 ", 9U) && memcmp(headers, "HTTP/1.0 ", 9U)) ||
        !ota_decimal(headers + 9U, 3U, &status) || headers[12] != ' ' ||
        memcmp(headers + length - 4U, "\r\n\r\n", 4U)) return D_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->status = (uint16_t)status;
    line = strstr(headers, "\r\n");
    if (!line) return D_ERR_INVALID_ARG;
    line += 2U;
    while (*line != '\r') {
        end = strstr(line, "\r\n");
        colon = strchr(line, ':');
        if (!end || !colon || colon == line || colon >= end) return D_ERR_INVALID_ARG;
        for (last = line; last < colon; last++)
            if (*last <= ' ' || (unsigned char)*last >= 0x7fU) return D_ERR_INVALID_ARG;
        value = colon + 1U;
        while (value < end && (*value == ' ' || *value == '\t')) value++;
        last = end;
        while (last > value && (last[-1] == ' ' || last[-1] == '\t')) last--;
        if (ota_ascii_equal(line, (unsigned)(colon - line), "content-length")) {
            if (out->has_length || !ota_decimal(value, (unsigned)(last - value), &out->length))
                return D_ERR_INVALID_ARG;
            out->has_length = 1U;
        } else if (ota_ascii_equal(line, (unsigned)(colon - line), "transfer-encoding")) {
            return D_ERR_INVALID_ARG;
        } else if (ota_ascii_equal(line, (unsigned)(colon - line), "content-encoding")) {
            if (!ota_ascii_equal(value, (unsigned)(last - value), "identity")) return D_ERR_INVALID_ARG;
        } else if (ota_ascii_equal(line, (unsigned)(colon - line), "etag")) {
            unsigned size = (unsigned)(last - value), index;
            if (etag_seen++) return D_ERR_INVALID_ARG;
            if (size >= 2U && value[0] == '"' && value[size - 1U] == '"') {
                if (size >= sizeof(out->etag)) return D_ERR_INVALID_ARG;
                for (index = 1U; index + 1U < size; index++)
                    if ((unsigned char)value[index] <= 0x20U || value[index] == '"' ||
                        (unsigned char)value[index] >= 0x7fU) return D_ERR_INVALID_ARG;
                memcpy(out->etag, value, size);
            }
        }
        line = end + 2U;
    }
    return D_OK;
}

uint16_t ota_crc16(uint16_t initial, const uint8_t *bytes, uint32_t length)
{
    uint32_t index;
    unsigned bit;
    for (index = 0U; index < length; index++) {
        initial ^= (uint16_t)bytes[index] << 8U;
        for (bit = 0U; bit < 8U; bit++)
            initial = (uint16_t)((initial << 1U) ^ ((initial & 0x8000U) ? 0x1021U : 0U));
    }
    return initial;
}

static uint16_t ota_u16(const uint8_t *bytes)
{ return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8U)); }
static uint32_t ota_u32(const uint8_t *bytes)
{ return bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U); }

d_err_t ota_package_parse(const uint8_t prefix[OTA_PACKAGE_PREFIX],
                          uint32_t received_length, ota_package_t *out)
{
    if (!prefix || !out || received_length <= OTA_PACKAGE_PREFIX ||
        ota_crc16(0U, prefix + 2U, 30U) != ota_u16(prefix) ||
        ota_crc16(0U, prefix + 34U, 30U) != ota_u16(prefix + 32U) ||
        memcmp(prefix + 16U, "update_data\0", 12U) ||
        memcmp(prefix + 48U, "app_core\0", 9U) ||
        ota_u16(prefix + 14U) != 1U || ota_u16(prefix + 46U) != 0U ||
        ota_u32(prefix + 4U) != 32U || ota_u32(prefix + 36U) != 64U ||
        ota_u32(prefix + 8U) != received_length ||
        ota_u32(prefix + 40U) != received_length - OTA_PACKAGE_PREFIX)
        return D_ERR_INVALID_ARG;
    out->length = received_length;
    out->core_length = received_length - OTA_PACKAGE_PREFIX;
    out->body_crc = ota_u16(prefix + 2U);
    out->core_crc = ota_u16(prefix + 34U);
    return D_OK;
}
