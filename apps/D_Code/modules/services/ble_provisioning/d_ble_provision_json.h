/** @file d_ble_provision_json.h @brief Bounded BLE JSON framing and command decoding. */
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
/** 1: complete object; 0: incomplete; -1: malformed or over protocol limits. */
int d_ble_json_frame(const char *json, uint16_t length);
/** Decode a complete terminated frame; never truncate credentials. */
int d_ble_json_decode(const char *json, uint16_t length, d_ble_json_command_t *command);
#endif
