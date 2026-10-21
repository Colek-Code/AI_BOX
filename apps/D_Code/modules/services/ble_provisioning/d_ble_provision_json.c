/** @file d_ble_provision_json.c @brief Uses the SDK's existing cJSON, without global hooks. */
#include "d_ble_provision_json.h"
#include "cJSON.h"
#include <string.h>

static int d_ble_json_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

int d_ble_json_frame(const char *json, uint16_t length)
{
    uint16_t index;
    uint8_t depth = 0U, quoted = 0U, escaped = 0U, started = 0U;
    if (json == NULL || length >= D_BLE_JSON_CAPACITY) return -1;
    for (index = 0U; index < length; index++) {
        char value = json[index];
        if (value == '\0') return -1;
        if (quoted) {
            if ((unsigned char)value < 0x20U) return -1;
            if (escaped) {
                /* cJSON strings use NUL termination; reject an embedded NUL
                 * rather than silently changing the SSID/password prefix. */
                if (value == 'u' && length - index >= 5 &&
                    memcmp(json + index + 1U, "0000", 4U) == 0) return -1;
                escaped = 0U;
            } else if (value == '\\') escaped = 1U;
            else if (value == '"') quoted = 0U;
            continue;
        }
        if (d_ble_json_space(value)) continue;
        if (!started) {
            if (value != '{') return -1;
            started = 1U;
        } else if (depth == 0U) return -1; /* One command per frame. */
        if (value == '"') quoted = 1U;
        else if (value == '{' || value == '[') {
            if (++depth > 4U) return -1; /* Bound cJSON recursion on the BT stack. */
        } else if (value == '}' || value == ']') {
            if (depth == 0U) return -1;
            depth--;
        }
    }
    return started && depth == 0U && !quoted ? 1 : 0;
}

static int d_ble_json_copy(const cJSON *item, char *out, unsigned capacity)
{
    size_t length;
    if (!cJSON_IsString(item) || item->valuestring == NULL) return -1;
    length = strlen(item->valuestring);
    if (length >= capacity) return -1;
    memcpy(out, item->valuestring, length + 1U);
    return 0;
}

int d_ble_json_decode(const char *json, uint16_t length, d_ble_json_command_t *command)
{
    cJSON *root, *item;
    d_ble_json_command_t parsed;
    char name[16] = "wifi_cfg";
    uint8_t seen = 0U;
    int result = -1;
    if (command == NULL || d_ble_json_frame(json, length) != 1 || json[length] != '\0') return -1;
    root = cJSON_ParseWithOpts(json, NULL, 1);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }
    memset(&parsed, 0, sizeof(parsed));
    for (item = root->child; item != NULL; item = item->next) {
        uint8_t bit = 0U;
        char *destination = NULL;
        unsigned capacity = 0U;
        if (item->string == NULL) goto done;
        if (strcmp(item->string, "cmd") == 0) {
            bit = 1U; destination = name; capacity = sizeof(name);
        } else if (strcmp(item->string, "ssid") == 0) {
            bit = 2U; destination = parsed.ssid; capacity = sizeof(parsed.ssid);
        } else if (strcmp(item->string, "pwd") == 0) {
            bit = 4U; destination = parsed.password; capacity = sizeof(parsed.password);
        }
        if (bit != 0U) {
            if ((seen & bit) != 0U || d_ble_json_copy(item, destination, capacity) != 0) goto done;
            seen |= bit;
        }
    }
    if (strcmp(name, "wifi_cfg") == 0) {
        if (parsed.ssid[0] == '\0') goto done;
        parsed.kind = D_BLE_JSON_WIFI;
    } else if (strcmp(name, "get_status") == 0) parsed.kind = D_BLE_JSON_STATUS;
    else if (strcmp(name, "get_mac") == 0) parsed.kind = D_BLE_JSON_MAC;
    else parsed.kind = D_BLE_JSON_UNKNOWN;
    *command = parsed;
    result = 0;
done:
    cJSON_Delete(root);
    return result;
}
