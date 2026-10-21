/**
 * @file product_ui_service_registry.c
 * @brief Static Product UI service ownership and command routing.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "os/os_api.h"
#include <string.h>

typedef struct {
    product_ui_service_snapshot_t snapshot;
    product_ui_service_command_cb_t command;
    void *user_data;
} product_ui_service_entry_t;

static OS_MUTEX s_service_mutex;
static uint8_t s_service_mutex_ready;
static product_ui_service_entry_t s_services[PRODUCT_UI_SERVICE_COUNT];

static uint8_t product_ui_service_valid(product_ui_service_id_e service)
{
    return service >= PRODUCT_UI_SERVICE_SYSTEM &&
           service < PRODUCT_UI_SERVICE_COUNT;
}

static void product_ui_service_revision_advance(
    product_ui_service_snapshot_t *snapshot)
{
    snapshot->revision++;
    if (snapshot->revision == 0U) {
        snapshot->revision = 1U;
    }
}

int product_ui_service_registry_init(void)
{
    uint8_t index;

    if (!s_service_mutex_ready) {
        if (os_mutex_create(&s_service_mutex) != OS_NO_ERR) {
            return -1;
        }
        s_service_mutex_ready = 1;
    }
    os_mutex_pend(&s_service_mutex, 0);
    memset(s_services, 0, sizeof(s_services));
    for (index = 0; index < PRODUCT_UI_SERVICE_COUNT; index++) {
        s_services[index].snapshot.state = PRODUCT_UI_SERVICE_UNKNOWN;
    }
    os_mutex_post(&s_service_mutex);
    return 0;
}

int product_ui_service_publish(product_ui_service_id_e service,
                               product_ui_service_state_e state,
                               uint32_t valid_mask, int32_t error_code)
{
    product_ui_service_entry_t *entry;

    if (!s_service_mutex_ready || !product_ui_service_valid(service) ||
        (state < PRODUCT_UI_SERVICE_UNKNOWN) ||
        (state > PRODUCT_UI_SERVICE_ERROR)) {
        return -1;
    }
    os_mutex_pend(&s_service_mutex, 0);
    entry = &s_services[service];
    if ((entry->snapshot.state == state) &&
        (entry->snapshot.valid_mask == valid_mask) &&
        (entry->snapshot.error_code == error_code)) {
        os_mutex_post(&s_service_mutex);
        return 0;
    }
    entry->snapshot.state = state;
    entry->snapshot.valid_mask = valid_mask;
    entry->snapshot.error_code = error_code;
    product_ui_service_revision_advance(&entry->snapshot);
    os_mutex_post(&s_service_mutex);
    return 0;
}

int product_ui_service_data_changed(product_ui_service_id_e service)
{
    if (!s_service_mutex_ready || !product_ui_service_valid(service)) {
        return -1;
    }
    os_mutex_pend(&s_service_mutex, 0);
    product_ui_service_revision_advance(&s_services[service].snapshot);
    os_mutex_post(&s_service_mutex);
    return 0;
}

int product_ui_service_snapshot(product_ui_service_id_e service,
                                product_ui_service_snapshot_t *snapshot)
{
    if (!s_service_mutex_ready || !product_ui_service_valid(service) ||
        (snapshot == NULL)) {
        return -1;
    }
    os_mutex_pend(&s_service_mutex, 0);
    *snapshot = s_services[service].snapshot;
    os_mutex_post(&s_service_mutex);
    return 0;
}

uint8_t product_ui_service_is_ready(product_ui_service_id_e service)
{
    product_ui_service_snapshot_t snapshot;

    if (product_ui_service_snapshot(service, &snapshot) != 0) {
        return 0;
    }
    return snapshot.state == PRODUCT_UI_SERVICE_READY;
}

uint8_t product_ui_service_command_available(product_ui_service_id_e service)
{
    uint8_t is_available;

    if (!s_service_mutex_ready || !product_ui_service_valid(service)) {
        return 0U;
    }
    /*
     * Generic availability is READY + registered callback only.
     * Capability / state bits live in valid_mask and are interpreted by
     * each command domain (for example media CAP_TRANSPORT).
     */
    os_mutex_pend(&s_service_mutex, 0);
    is_available = (s_services[service].snapshot.state ==
                    PRODUCT_UI_SERVICE_READY) &&
                   (s_services[service].command != NULL);
    os_mutex_post(&s_service_mutex);
    return is_available;
}

int product_ui_service_register_command(product_ui_service_id_e service,
                                        product_ui_service_command_cb_t callback,
                                        void *user_data)
{
    if (!s_service_mutex_ready || !product_ui_service_valid(service)) {
        return -1;
    }
    os_mutex_pend(&s_service_mutex, 0);
    if ((s_services[service].command == callback) &&
        (s_services[service].user_data == user_data)) {
        os_mutex_post(&s_service_mutex);
        return 0;
    }
    s_services[service].command = callback;
    s_services[service].user_data = user_data;
    product_ui_service_revision_advance(&s_services[service].snapshot);
    os_mutex_post(&s_service_mutex);
    return 0;
}

int product_ui_service_command(product_ui_service_id_e service,
                               uint16_t command, const void *payload,
                               uint16_t payload_size)
{
    product_ui_service_command_cb_t callback;
    void *user_data;
    uint8_t is_ready;

    if (!s_service_mutex_ready || !product_ui_service_valid(service) ||
        ((payload == NULL) && (payload_size != 0U))) {
        return -1;
    }
    os_mutex_pend(&s_service_mutex, 0);
    callback = s_services[service].command;
    user_data = s_services[service].user_data;
    is_ready = s_services[service].snapshot.state == PRODUCT_UI_SERVICE_READY;
    os_mutex_post(&s_service_mutex);
    if (!is_ready || (callback == NULL)) {
        return -1;
    }
    return callback(command, payload, payload_size, user_data);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
