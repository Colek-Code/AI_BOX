/**
 * @file product_ui_connection_service.c
 * @brief 静态连接快照及回调槽的所有者；平台发布，UI 借用。
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "os/os_api.h"
#include "framework/d_error.h"
#include <string.h>

static OS_MUTEX s_connection_mutex;
static uint8_t s_connection_mutex_ready;
static product_ui_connection_snapshot_t s_snapshot;
static product_ui_connection_sync_cb_t s_sync_cb;
static product_ui_connection_request_cb_t s_request_cb;

/**
 * @brief 推进连接快照的非零内容代次。
 * @param[in,out] snapshot 待更新的快照；调用方持有互斥量。
 */
static void product_ui_connection_revision_advance(
    product_ui_connection_snapshot_t *snapshot)
{
    snapshot->revision++;
    if (snapshot->revision == 0U) {
        snapshot->revision = 1U;
    }
}

/**
 * @brief 在串行启动阶段重置连接快照及两个独立回调槽。
 */
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
    s_request_cb = NULL;
    os_mutex_post(&s_connection_mutex);
    return 0;
}

/**
 * @brief 替换同步回调槽，NULL 表示清除。
 */
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

/**
 * @brief 借用当前同步回调并在互斥量外调用。
 */
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

/**
 * @brief 替换独立请求回调槽，NULL 表示清除。
 */
void product_ui_connection_service_register_request(product_ui_connection_request_cb_t callback)
{
    if (!s_connection_mutex_ready) return;
    os_mutex_pend(&s_connection_mutex, 0);
    s_request_cb = callback;
    os_mutex_post(&s_connection_mutex);
}

/**
 * @brief 借用请求提供者并原样透传其受理结果及请求编号。
 */
int product_ui_connection_request(uint16_t command, const void *payload,
    uint16_t payload_size, uint32_t *out_ticket)
{
    product_ui_connection_request_cb_t callback;
    if (out_ticket == NULL) return D_ERR_INVALID_ARG;
    *out_ticket = 0U;
    if (!s_connection_mutex_ready) return D_ERR_NOT_READY;
    os_mutex_pend(&s_connection_mutex, 0);
    callback = s_request_cb;
    os_mutex_post(&s_connection_mutex);
    return callback != NULL ? callback(command, payload, payload_size, out_ticket) : D_ERR_NOT_READY;
}

/**
 * @brief 复制平台连接投影，实际内容变化时推进连接及注册表代次。
 */
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
        (s_snapshot.ready == snapshot->ready) &&
        s_snapshot.request_version == snapshot->request_version &&
        s_snapshot.request_id == snapshot->request_id &&
        s_snapshot.completed_id == snapshot->completed_id &&
        s_snapshot.request_kind == snapshot->request_kind &&
        s_snapshot.completed_kind == snapshot->completed_kind &&
        s_snapshot.request_phase == snapshot->request_phase &&
        s_snapshot.completed_result == snapshot->completed_result &&
        strncmp(s_snapshot.completed_ssid, snapshot->completed_ssid,
                sizeof(s_snapshot.completed_ssid)) == 0) {
        os_mutex_post(&s_connection_mutex);
        return 0;
    }
    s_snapshot.service_version = snapshot->service_version;
    s_snapshot.wifi_capable = snapshot->wifi_capable;
    s_snapshot.has_saved_credentials = snapshot->has_saved_credentials;
    s_snapshot.conn_state = snapshot->conn_state;
    s_snapshot.conn_error = snapshot->conn_error;
    s_snapshot.ready = snapshot->ready;
    s_snapshot.request_version = snapshot->request_version;
    s_snapshot.request_id = snapshot->request_id;
    s_snapshot.completed_id = snapshot->completed_id;
    s_snapshot.request_kind = snapshot->request_kind;
    s_snapshot.completed_kind = snapshot->completed_kind;
    s_snapshot.request_phase = snapshot->request_phase;
    s_snapshot.completed_result = snapshot->completed_result;
    memcpy(s_snapshot.completed_ssid, snapshot->completed_ssid, sizeof(s_snapshot.completed_ssid));
    s_snapshot.completed_ssid[sizeof(s_snapshot.completed_ssid) - 1U] = '\0';
    product_ui_connection_revision_advance(&s_snapshot);
    changed = 1U;
    os_mutex_post(&s_connection_mutex);
    if (changed != 0U) {
        /* 每次真实内容变化只推进一次注册表 CONNECTION 代次。 */
        (void)product_ui_service_data_changed(PRODUCT_UI_SERVICE_CONNECTION);
    }
    return 0;
}

/**
 * @brief 在互斥量内复制连接域快照。
 */
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
