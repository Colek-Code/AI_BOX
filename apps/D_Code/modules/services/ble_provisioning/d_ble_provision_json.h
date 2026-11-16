/** @file d_ble_provision_json.h @brief 有界 BLE JSON 分帧与配网命令解码。 */
#ifndef D_BLE_PROVISION_JSON_H
#define D_BLE_PROVISION_JSON_H
#include <stdint.h>
#define D_BLE_JSON_CAPACITY 256U
typedef enum {
    D_BLE_JSON_WIFI,
    D_BLE_JSON_STATUS,
    D_BLE_JSON_MAC,
    D_BLE_JSON_UNKNOWN,
} d_ble_json_kind_e;
typedef struct {
    d_ble_json_kind_e kind;
    char ssid[33];
    char password[65];
} d_ble_json_command_t;
/**
 * @brief 检查有界接收片段是否包含一个完整 JSON 对象。
 * @param[in] json 非空字节片段，不要求当前片段零结尾。
 * @param[in] length 字节数，必须小于 D_BLE_JSON_CAPACITY。
 * @return 对象完整返回 1，尚不完整返回 0，格式或协议上限无效返回 -1。
 * @note 限制嵌套深度并拒绝内嵌零字符；完整语法仍由 decode 中的 cJSON 核实。
 */
int d_ble_json_frame(const char *json, uint16_t length);
/**
 * @brief 解析一个完整零结尾对象，不截断配网凭据。
 * @param[in] json 完整对象，json[length] 必须为零。
 * @param[in] length 不含末尾零的字节数。
 * @param[out] command 非空解析结果输出，仅成功时更新。
 * @return 成功返回 0，帧、字段重复、类型或凭据长度无效返回 -1。
 * @note 缺省 cmd 按 wifi_cfg；未知命令保留 UNKNOWN，由调用者响应。
 */
int d_ble_json_decode(const char *json, uint16_t length, d_ble_json_command_t *command);
#endif
