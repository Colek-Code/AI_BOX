/**
 * @file product_ui_service_registry.c
 * @brief 静态服务快照与回调槽的所有权及互斥路由。
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

/**
 * @brief 检查服务编号是否落在静态注册表范围内。
 * @param[in] service 待检查的服务编号。
 * @return 有效返回非零，否则为零。
 */
static uint8_t product_ui_service_valid(product_ui_service_id_e service)
{
    return service >= PRODUCT_UI_SERVICE_SYSTEM &&
           service < PRODUCT_UI_SERVICE_COUNT;
}

/**
 * @brief 推进服务快照的非零变化代次。
 * @param[in,out] snapshot 调用方在互斥量保护下更新的快照。
 */
static void product_ui_service_revision_advance(
    product_ui_service_snapshot_t *snapshot)
{
    snapshot->revision++;
    if (snapshot->revision == 0U) {
        snapshot->revision = 1U;
    }
}

/**
 * @brief 在串行启动阶段重置全部服务快照和命令绑定。
 */
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

/**
 * @brief 发布服务可用性、能力位和错误，内容变化时推进代次。
 */
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

/**
 * @brief 为服务内部数据变化单独推进快照代次。
 */
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

/**
 * @brief 在互斥量内读取服务快照的值副本。
 */
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

/**
 * @brief 查询服务是否明确发布 READY。
 */
uint8_t product_ui_service_is_ready(product_ui_service_id_e service)
{
    product_ui_service_snapshot_t snapshot;

    if (product_ui_service_snapshot(service, &snapshot) != 0) {
        return 0;
    }
    return snapshot.state == PRODUCT_UI_SERVICE_READY;
}

/**
 * @brief 查询服务是否同时处于 READY 且绑定命令回调。
 */
uint8_t product_ui_service_command_available(product_ui_service_id_e service)
{
    uint8_t is_available;

    if (!s_service_mutex_ready || !product_ui_service_valid(service)) {
        return 0U;
    }
    /* 通用可用性只检查 READY 和回调绑定；valid_mask 中的能力及状态位
     * 由具体命令域解释，例如媒体 CAP_TRANSPORT。 */
    os_mutex_pend(&s_service_mutex, 0);
    is_available = (s_services[service].snapshot.state ==
                    PRODUCT_UI_SERVICE_READY) &&
                   (s_services[service].command != NULL);
    os_mutex_post(&s_service_mutex);
    return is_available;
}

/**
 * @brief 原子替换命令回调及其借用上下文，完全相同的绑定不推进代次。
 */
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

/**
 * @brief 锁内借用 READY 状态及回调绑定，然后锁外同步调用。
 */
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
