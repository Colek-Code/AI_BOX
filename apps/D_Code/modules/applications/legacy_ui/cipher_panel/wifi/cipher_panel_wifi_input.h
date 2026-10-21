/**
 * @file cipher_panel_wifi_input.h
 * @brief Password input panel for the cipher panel WiFi page.
 */
#ifndef __CIPHER_PANEL_WIFI_INPUT_H__
#define __CIPHER_PANEL_WIFI_INPUT_H__

#include "lvgl.h"
#include "modules/services/wifi/wifi_service.h"
#include <stdint.h>

typedef uint8_t (*cipher_wifi_input_connect_cb_t)(const char *ssid,
                                                  const char *pwd,
                                                  void *user_data);

void cipher_wifi_input_reset(void);
void cipher_wifi_input_close(void);
void cipher_wifi_input_open(lv_obj_t *parent,
                            const wifi_service_scan_result_t *item,
                            cipher_wifi_input_connect_cb_t connect_cb,
                            void *user_data);
void cipher_wifi_input_set_message(const char *text, uint32_t color);
uint8_t cipher_wifi_input_is_open(void);

#endif /* __CIPHER_PANEL_WIFI_INPUT_H__ */
