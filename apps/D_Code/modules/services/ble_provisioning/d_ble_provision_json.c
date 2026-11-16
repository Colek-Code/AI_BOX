/** @file d_ble_provision_json.c @brief 使用 SDK 现有 cJSON 解码，不改全局分配钩子。 */
#include "d_ble_provision_json.h"
#include "cJSON.h"
#include <string.h>

/**
 * @brief 判断 JSON 允许的四类空白字符。
 * @param[in] value 待检查字符。
 * @return 匹配空格、制表、回车或换行返回非零。
 */
static int d_ble_json_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

/**
 * @brief 检查有界接收片段是否包含一个完整 JSON 对象；完整合同见 d_ble_provision_json.h。
 */
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
                /* cJSON 字符串以零结束；拒绝内嵌零转义，
                 * 避免静默改变 SSID 或密码的有效前缀。 */
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
        } else if (depth == 0U) return -1; /* 每帧只允许一个命令。 */
        if (value == '"') quoted = 1U;
        else if (value == '{' || value == '[') {
            if (++depth > 4U) return -1; /* 限制蓝牙任务栈上的 cJSON 递归深度。 */
        } else if (value == '}' || value == ']') {
            if (depth == 0U) return -1;
            depth--;
        }
    }
    return started && depth == 0U && !quoted ? 1 : 0;
}

/**
 * @brief 将 JSON 字符串完整复制到有界缓冲，不截断。
 * @param[in] item 待复制 JSON 项。
 * @param[out] out 非空目标缓冲。
 * @param[in] capacity 包含末尾零的容量。
 * @return 成功返回 0；字段非字符串或容量不足返回 -1。
 */
static int d_ble_json_copy(const cJSON *item, char *out, unsigned capacity)
{
    size_t length;
    if (!cJSON_IsString(item) || item->valuestring == NULL) return -1;
    length = strlen(item->valuestring);
    if (length >= capacity) return -1;
    memcpy(out, item->valuestring, length + 1U);
    return 0;
}

/**
 * @brief 解析一个完整零结尾对象，不截断配网凭据；完整合同见 d_ble_provision_json.h。
 */
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
