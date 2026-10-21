/**
 * @file d_ble_net_cfg.h
 * @brief BLE provisioning entry points used outside d_ble_net_cfg.c.
 */

#ifndef D_BLE_NET_CFG_H
#define D_BLE_NET_CFG_H

#include <stdint.h>

struct sys_event;

void config_network_connect(void);
int ble_demo_bt_event_handler(struct sys_event *event);
void bt_ble_module_init(void);
void bt_ble_init(void);
void bt_ble_exit(void);
void d_ble_net_cfg_set_adv(uint8_t enable);
void ble_app_disconnect(void);
void ble_cfg_net_result_notify(int event);
int ble_connected_net_notify(void);
/** out_len supplies output capacity on entry and receives written bytes on success. */
int d_ble_decrypt(const uint8_t *in, uint16_t in_len,
                  uint8_t *out, uint16_t *out_len);
int d_ble_encrypt(const uint8_t *in, uint16_t in_len,
                  uint8_t *out, uint16_t *out_len);

#endif /* D_BLE_NET_CFG_H */
