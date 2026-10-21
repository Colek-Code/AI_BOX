/**
 * @file product_ui_connection_service.c
 * @brief Static CONNECTION snapshot ownership (platform publishes).
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "os/os_api.h"
#include <string.h>

static OS_MUTEX s_connection_mutex;
static uint8_t s_connection_mutex_ready;
static product_ui_connection_snapshot_t s_snapshot;
static product_ui_connection_sync_cb_t s_sync_cb;

static void product_ui_connection_revision_advance(
    product_ui_connection_snapshot_t *snapshot)
{
    snapshot->revision++;
    if (snapshot->revision == 0U) {
        snapshot->revision = 1U;
    }
}

int product_ui_connection_service_init(void)
{
    if (!s_connection_mutex_ready) {
        if (os_mutex_create(&s_connection_mutex) != OS_NO_ERR) {
            return -1;
        }
        s_connection_mutex_ready = 1U;
    }
    os_mutex_pend(&s_connection_mutex, 0);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_sync_cb = NULL;
    os_mutex_post(&s_connection_mutex);
    return 0;
}

void product_ui_connection_service_register_sync(
    product_ui_connection_sync_cb_t callback)
{
    if (!s_connection_mutex_ready) {
        return;
    }
    os_mutex_pend(&s_connection_mutex, 0);
    s_sync_cb = callback;
    os_mutex_post(&s_connection_mutex);
}

void product_ui_connection_service_sync(void)
{
    product_ui_connection_sync_cb_t callback;

    if (!s_connection_mutex_ready) {
        return;
    }
    os_mutex_pend(&s_connection_mutex, 0);
    callback = s_sync_cb;
    os_mutex_post(&s_connection_mutex);
    if (callback != NULL) {
        callback();
    }
}

int product_ui_connection_service_publish(
    const product_ui_connection_snapshot_t *snapshot)
{
    uint8_t changed = 0U;

    if (!s_connection_mutex_ready || (snapshot == NULL)) {
        return -1;
    }
    os_mutex_pend(&s_connection_mutex, 0);
    if ((s_snapshot.service_version == snapshot->service_version) &&
        (s_snapshot.wifi_capable == snapshot->wifi_capable) &&
        (s_snapshot.has_saved_credentials ==
         snapshot->has_saved_credentials) &&
        (s_snapshot.conn_state == snapshot->conn_state) &&
        (s_snapshot.conn_error == snapshot->conn_error) &&
        (s_snapshot.ready == snapshot->ready)) {
        os_mutex_post(&s_connection_mutex);
        return 0;
    }
    s_snapshot.service_version = snapshot->service_version;
    s_snapshot.wifi_capable = snapshot->wifi_capable;
    s_snapshot.has_saved_credentials = snapshot->has_saved_credentials;
    s_snapshot.conn_state = snapshot->conn_state;
    s_snapshot.conn_error = snapshot->conn_error;
    s_snapshot.ready = snapshot->ready;
    product_ui_connection_revision_advance(&s_snapshot);
    changed = 1U;
    os_mutex_post(&s_connection_mutex);
    if (changed != 0U) {
        /* Advance Registry CONNECTION revision once per real content change. */
        (void)product_ui_service_data_changed(PRODUCT_UI_SERVICE_CONNECTION);
    }
    return 0;
}

int product_ui_connection_service_snapshot(
    product_ui_connection_snapshot_t *snapshot)
{
    if (!s_connection_mutex_ready || (snapshot == NULL)) {
        return -1;
    }
    os_mutex_pend(&s_connection_mutex, 0);
    *snapshot = s_snapshot;
    os_mutex_post(&s_connection_mutex);
    return 0;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
