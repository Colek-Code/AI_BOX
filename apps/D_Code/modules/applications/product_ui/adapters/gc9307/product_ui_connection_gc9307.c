/**
 * @file product_ui_connection_gc9307.c
 * @brief Map wifi_service into Product UI CONNECTION snapshot/commands.
 *
 * Does not change Wi-Fi scan/connect/reconnect/forget behavior — only owns
 * the Product UI boundary so control center never includes wifi_service.h.
 *
 * Readiness: wifi_is_on() from wifi/wifi_connect.h (backend radio started).
 * status_version is only used to skip duplicate full get_status reads after a
 * valid snapshot exists — never as readiness. version==0 is a legal initial
 * snapshot when Wi-Fi is on (first-boot, no credentials, IDLE).
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && \
    (D_FEATURE_WIFI)

#include "modules/applications/product_ui/adapters/gc9307/product_ui_gc9307_adapter.h"
#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_read_policy.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "modules/applications/product_ui/runtime/status/product_ui_status_model.h"
#include "modules/services/wifi/wifi_service.h"
#include "wifi/wifi_connect.h"
#include "framework/d_error.h"
#include <string.h>

/* After a valid snapshot: skip full get_status while version is unchanged. */
static uint32_t s_seen_status_version = 0xFFFFFFFFU;
static uint8_t s_have_valid_snapshot;
static uint8_t s_registry_ready;
static uint8_t s_command_registered;
static uint8_t s_last_wifi_on;

static int product_ui_connection_cstr_bounded(const char *text,
                                              uint16_t capacity)
{
    uint16_t i;

    if ((text == NULL) || (capacity == 0U)) {
        return 0;
    }
    for (i = 0U; i < capacity; i++) {
        if (text[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static uint8_t product_ui_connection_map_state(
    wifi_service_conn_state_t state)
{
    switch (state) {
    case WIFI_SERVICE_CONN_IDLE:
        return (uint8_t)PRODUCT_UI_CONN_STATE_IDLE;
    case WIFI_SERVICE_CONN_CONNECTING:
        return (uint8_t)PRODUCT_UI_CONN_STATE_CONNECTING;
    case WIFI_SERVICE_CONN_ASSOCIATED:
        return (uint8_t)PRODUCT_UI_CONN_STATE_ASSOCIATED;
    case WIFI_SERVICE_CONN_CONNECTED:
        return (uint8_t)PRODUCT_UI_CONN_STATE_CONNECTED;
    case WIFI_SERVICE_CONN_DISCONNECTED:
        return (uint8_t)PRODUCT_UI_CONN_STATE_DISCONNECTED;
    case WIFI_SERVICE_CONN_ERROR:
        return (uint8_t)PRODUCT_UI_CONN_STATE_ERROR;
    default:
        return (uint8_t)PRODUCT_UI_CONN_STATE_ERROR;
    }
}

static uint8_t product_ui_connection_map_error(
    wifi_service_conn_error_t error)
{
    switch (error) {
    case WIFI_SERVICE_ERR_NONE:
        return (uint8_t)PRODUCT_UI_CONN_ERR_NONE;
    case WIFI_SERVICE_ERR_NOT_READY:
        return (uint8_t)PRODUCT_UI_CONN_ERR_NOT_READY;
    case WIFI_SERVICE_ERR_BUSY:
        return (uint8_t)PRODUCT_UI_CONN_ERR_BUSY;
    case WIFI_SERVICE_ERR_NO_SAVED:
        return (uint8_t)PRODUCT_UI_CONN_ERR_NO_SAVED;
    case WIFI_SERVICE_ERR_NOT_FOUND:
        return (uint8_t)PRODUCT_UI_CONN_ERR_NOT_FOUND;
    case WIFI_SERVICE_ERR_ASSOC_FAIL:
        return (uint8_t)PRODUCT_UI_CONN_ERR_ASSOC_FAIL;
    case WIFI_SERVICE_ERR_ASSOC_TIMEOUT:
        return (uint8_t)PRODUCT_UI_CONN_ERR_ASSOC_TIMEOUT;
    case WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT:
        return (uint8_t)PRODUCT_UI_CONN_ERR_SWITCH_DISCONNECT_TIMEOUT;
    case WIFI_SERVICE_ERR_DHCP_TIMEOUT:
        return (uint8_t)PRODUCT_UI_CONN_ERR_DHCP_TIMEOUT;
    case WIFI_SERVICE_ERR_DISCONNECTED:
        return (uint8_t)PRODUCT_UI_CONN_ERR_DISCONNECTED;
    default:
        return (uint8_t)PRODUCT_UI_CONN_ERR_NOT_READY;
    }
}

/*
 * CONNECTION commands forward to wifi_service. Return values are d_err_t
 * (0 / negative) so UI toasts can distinguish BUSY / NOT_FOUND / IO.
 */
static int product_ui_connection_gc9307_command(
    uint16_t command, const void *payload, uint16_t payload_size,
    void *user_data)
{
    const product_ui_connection_wifi_connect_t *req;

    (void)user_data;

    switch (command) {
    case PRODUCT_UI_CONNECTION_WIFI_SCAN_START:
        if ((payload != NULL) || (payload_size != 0U)) {
            return (int)D_ERR_INVALID_ARG;
        }
        return (int)wifi_service_scan_start();

    case PRODUCT_UI_CONNECTION_WIFI_RECONNECT:
        if ((payload != NULL) || (payload_size != 0U)) {
            return (int)D_ERR_INVALID_ARG;
        }
        return (int)wifi_service_reconnect_saved();

    case PRODUCT_UI_CONNECTION_WIFI_FORGET:
        if ((payload != NULL) || (payload_size != 0U)) {
            return (int)D_ERR_INVALID_ARG;
        }
        return (int)wifi_service_forget_saved();

    case PRODUCT_UI_CONNECTION_WIFI_CONNECT:
        if ((payload == NULL) || (payload_size != sizeof(*req))) {
            return (int)D_ERR_INVALID_ARG;
        }
        req = (const product_ui_connection_wifi_connect_t *)payload;
        if ((product_ui_connection_cstr_bounded(
                 req->ssid, (uint16_t)sizeof(req->ssid)) == 0) ||
            (product_ui_connection_cstr_bounded(
                 req->pwd, (uint16_t)sizeof(req->pwd)) == 0) ||
            (req->ssid[0] == '\0')) {
            return (int)D_ERR_INVALID_ARG;
        }
        return (int)wifi_service_connect_ui(req->ssid, req->pwd);

    default:
        return (int)D_ERR_INVALID_ARG;
    }
}

static void product_ui_connection_gc9307_publish_unavailable(void)
{
    product_ui_connection_snapshot_t snap;

    memset(&snap, 0, sizeof(snap));
    snap.ready = 0U;
    snap.wifi_capable = 0U;
    snap.service_version = wifi_service_get_status_version();
    (void)product_ui_connection_service_publish(&snap);
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                     PRODUCT_UI_SERVICE_UNAVAILABLE, 0U, 0);
    s_registry_ready = 0U;
    s_have_valid_snapshot = 0U;
    /* Force full re-read when Wi-Fi comes back on (even if version unchanged). */
    s_seen_status_version = 0xFFFFFFFFU;
}

static int product_ui_connection_gc9307_stable_status(
    wifi_service_status_t *out, uint32_t *out_version)
{
    wifi_service_status_t first;
    wifi_service_status_t second;
    uint32_t v0_before;
    uint32_t v0_after;
    uint32_t v1_before;
    uint32_t v1_after;
    product_ui_connection_stable_select_e select;

    if ((out == NULL) || (out_version == NULL)) {
        return -1;
    }
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    /*
     * Bounded stable-read: at most two get_status calls. If the second pair
     * still races, abort without publishing or updating seen_version.
     */
    v0_before = wifi_service_get_status_version();
    wifi_service_get_status(&first);
    v0_after = wifi_service_get_status_version();
    if (v0_before == v0_after) {
        *out = first;
        *out_version = v0_after;
        return 0;
    }

    v1_before = v0_after;
    wifi_service_get_status(&second);
    v1_after = wifi_service_get_status_version();
    select = product_ui_connection_stable_select(v0_before, v0_after,
                                                 v1_before, v1_after);
    if (select == PRODUCT_UI_CONN_STABLE_USE_SECOND) {
        *out = second;
        *out_version = v1_after;
        return 0;
    }
    /* PRODUCT_UI_CONN_STABLE_ABORT (or unexpected): do not publish. */
    return -1;
}

static void product_ui_connection_gc9307_sync(void)
{
    product_ui_connection_snapshot_t snap;
    wifi_service_status_t wifi;
    uint32_t version;
    uint8_t wifi_on;
    product_ui_connection_sync_action_e action;

    wifi_on = wifi_is_on() ? 1U : 0U;

    if (wifi_on == 0U) {
        /*
         * Backend not started (init failed or radio off). Clear any cached
         * valid snapshot so a later on edge re-reads even if version matches.
         */
        product_ui_connection_gc9307_publish_unavailable();
        s_last_wifi_on = 0U;
        return;
    }

    /* off → on: invalidate skip cache and force a full status read. */
    if (s_last_wifi_on == 0U) {
        s_have_valid_snapshot = 0U;
        s_seen_status_version = 0xFFFFFFFFU;
    }
    s_last_wifi_on = 1U;

    version = wifi_service_get_status_version();
    action = product_ui_connection_sync_action(wifi_on, s_have_valid_snapshot,
                                               version, s_seen_status_version);
    if (action == PRODUCT_UI_CONN_SYNC_SKIP_READ) {
        return;
    }
    if (action == PRODUCT_UI_CONN_SYNC_UNAVAILABLE) {
        product_ui_connection_gc9307_publish_unavailable();
        return;
    }

    memset(&wifi, 0, sizeof(wifi));
    if (product_ui_connection_gc9307_stable_status(&wifi, &version) != 0) {
        /* Race on both samples: leave seen_version untouched; retry later. */
        return;
    }

    memset(&snap, 0, sizeof(snap));
    snap.ready = 1U;
    snap.wifi_capable = 1U;
    snap.service_version = version; /* may be 0 for legal first-boot IDLE */
    snap.has_saved_credentials = wifi.has_saved_credentials;
    snap.conn_state = product_ui_connection_map_state(wifi.state);
    snap.conn_error = product_ui_connection_map_error(wifi.error);

    if (product_ui_connection_service_publish(&snap) != 0) {
        return;
    }
    /* Only after successful publish may we skip future full status reads. */
    s_seen_status_version = version;
    s_have_valid_snapshot = 1U;

    if (s_registry_ready == 0U) {
        if (product_ui_service_publish(
                PRODUCT_UI_SERVICE_CONNECTION, PRODUCT_UI_SERVICE_READY,
                PRODUCT_UI_STATUS_CAP_WIFI, 0) == 0) {
            s_registry_ready = 1U;
        }
    }
}

static void product_ui_connection_gc9307_cleanup(void)
{
    product_ui_connection_service_register_sync(NULL);
    if (s_command_registered != 0U) {
        (void)product_ui_service_register_command(
            PRODUCT_UI_SERVICE_CONNECTION, NULL, NULL);
        s_command_registered = 0U;
    }
    s_registry_ready = 0U;
    s_have_valid_snapshot = 0U;
    s_last_wifi_on = 0U;
    s_seen_status_version = 0xFFFFFFFFU;
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                     PRODUCT_UI_SERVICE_UNAVAILABLE, 0U, -1);
    {
        product_ui_connection_snapshot_t snap;

        memset(&snap, 0, sizeof(snap));
        (void)product_ui_connection_service_publish(&snap);
    }
}

int product_ui_connection_gc9307_init(void)
{
    s_seen_status_version = 0xFFFFFFFFU;
    s_have_valid_snapshot = 0U;
    s_registry_ready = 0U;
    s_command_registered = 0U;
    s_last_wifi_on = 0U;

    if (product_ui_connection_service_init() != 0) {
        (void)product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                         PRODUCT_UI_SERVICE_UNAVAILABLE, 0U,
                                         -1);
        return -1;
    }
    if (product_ui_service_register_command(
            PRODUCT_UI_SERVICE_CONNECTION,
            product_ui_connection_gc9307_command, NULL) != 0) {
        product_ui_connection_gc9307_cleanup();
        return -1;
    }
    s_command_registered = 1U;

    /* Start UNAVAILABLE until wifi_is_on(); promote in first sync when on. */
    if (product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                   PRODUCT_UI_SERVICE_UNAVAILABLE, 0U,
                                   0) != 0) {
        product_ui_connection_gc9307_cleanup();
        return -1;
    }
    product_ui_connection_service_register_sync(
        product_ui_connection_gc9307_sync);
    product_ui_connection_gc9307_sync();
    return 0;
}

#endif /* Product UI GC9307 + Wi-Fi */
