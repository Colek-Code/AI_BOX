/**
 * @file product_ui_connection_service.h
 * @brief CONNECTION domain snapshot + command contract (no wifi_service.h).
 */

#ifndef __PRODUCT_UI_CONNECTION_SERVICE_H__
#define __PRODUCT_UI_CONNECTION_SERVICE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include <stdint.h>

/* Match wifi_service SSID/PWD bounds without pulling wifi headers here.
 * WIFI_SSID_MAXLEN=33, WIFI_PWD_MAXLEN=65 (includes trailing NUL). */
#define PRODUCT_UI_CONNECTION_SSID_MAX  33U
#define PRODUCT_UI_CONNECTION_PWD_MAX   65U

/*
 * Product UI owned connection state/error codes. Platform adapters must map
 * vendor enums via explicit switch — do not rely on numeric ABI alignment.
 */
typedef enum {
    PRODUCT_UI_CONN_STATE_IDLE = 0,
    PRODUCT_UI_CONN_STATE_CONNECTING,
    PRODUCT_UI_CONN_STATE_ASSOCIATED,
    PRODUCT_UI_CONN_STATE_CONNECTED,
    PRODUCT_UI_CONN_STATE_DISCONNECTED,
    PRODUCT_UI_CONN_STATE_ERROR,
} product_ui_connection_conn_state_e;

typedef enum {
    PRODUCT_UI_CONN_ERR_NONE = 0,
    PRODUCT_UI_CONN_ERR_NOT_READY,
    PRODUCT_UI_CONN_ERR_BUSY,
    PRODUCT_UI_CONN_ERR_NO_SAVED,
    PRODUCT_UI_CONN_ERR_NOT_FOUND,
    PRODUCT_UI_CONN_ERR_ASSOC_FAIL,
    PRODUCT_UI_CONN_ERR_ASSOC_TIMEOUT,
    PRODUCT_UI_CONN_ERR_SWITCH_DISCONNECT_TIMEOUT,
    PRODUCT_UI_CONN_ERR_DHCP_TIMEOUT,
    PRODUCT_UI_CONN_ERR_DISCONNECTED,
} product_ui_connection_conn_error_e;

typedef enum {
    PRODUCT_UI_CONNECTION_WIFI_SCAN_START = 0,
    PRODUCT_UI_CONNECTION_WIFI_RECONNECT,
    PRODUCT_UI_CONNECTION_WIFI_FORGET,
    PRODUCT_UI_CONNECTION_WIFI_CONNECT,
} product_ui_connection_command_e;

typedef struct {
    char ssid[PRODUCT_UI_CONNECTION_SSID_MAX];
    char pwd[PRODUCT_UI_CONNECTION_PWD_MAX];
} product_ui_connection_wifi_connect_t;

/*
 * Product UI owned WLAN projection for control center / future consumers.
 * Platform adapters publish; UI modules must not call wifi_service_*.
 */
typedef struct {
    uint32_t revision;
    uint32_t service_version;
    uint8_t wifi_capable;
    uint8_t has_saved_credentials;
    uint8_t conn_state;
    uint8_t conn_error;
    uint8_t ready;
} product_ui_connection_snapshot_t;

typedef void (*product_ui_connection_sync_cb_t)(void);

int product_ui_connection_service_init(void);
void product_ui_connection_service_register_sync(
    product_ui_connection_sync_cb_t callback);
void product_ui_connection_service_sync(void);
int product_ui_connection_service_publish(
    const product_ui_connection_snapshot_t *snapshot);
int product_ui_connection_service_snapshot(
    product_ui_connection_snapshot_t *snapshot);

static inline int product_ui_connection_command(uint16_t command,
                                                const void *payload,
                                                uint16_t payload_size)
{
    return product_ui_service_command(PRODUCT_UI_SERVICE_CONNECTION, command,
                                      payload, payload_size);
}

#endif
#endif /* __PRODUCT_UI_CONNECTION_SERVICE_H__ */
