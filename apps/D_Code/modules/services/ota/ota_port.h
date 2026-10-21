/** @file ota_port.h @brief Private, replaceable device boundaries; no LVGL. */
#ifndef D_OTA_PORT_H
#define D_OTA_PORT_H
#include "ota_service.h"
void ota_port_lock(void);
void ota_port_unlock(void);
uint32_t ota_port_now(void);
uint32_t ota_port_board(void);
const char *ota_port_default_url(void);
d_err_t ota_port_ready(void);
d_err_t ota_port_start(void (*worker)(void *), void *context);
void ota_port_notify(ota_state_e state);
void ota_port_reboot(void);
void ota_port_wait(uint32_t milliseconds);

d_err_t ota_transport_open(const char *url, uint8_t head, const char *etag,
                           ota_http_response_t *response);
int ota_transport_read(uint8_t *bytes, uint16_t capacity);
void ota_transport_close(void);
void ota_transport_hash_begin(void);
void ota_transport_hash_update(const uint8_t *bytes, uint32_t length);
d_err_t ota_transport_hash_finish(uint8_t digest[32]);

d_err_t ota_sdk_begin(void);
uint16_t ota_sdk_first_size(void);
d_err_t ota_sdk_write(const uint8_t *bytes, uint16_t length);
d_err_t ota_sdk_verify(void);
d_err_t ota_sdk_commit(void);
/** Never frees while a callback or SDK call may still reference the context. */
d_err_t ota_sdk_close(void);
uint8_t ota_sdk_pending(void);

#endif
