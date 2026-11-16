/** @file product_ui_connection_gc9307.c @brief 将 WiFi 服务状态与请求映射到 UI 连接域；缓存版本不作为无线就绪证明。 */

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

/* 已有有效快照且版本未变时，跳过完整状态读取。 */
static uint32_t s_seen_status_version = 0xFFFFFFFFU;
static uint32_t s_seen_request_version = 0xFFFFFFFFU;
static uint8_t s_have_valid_snapshot;
static uint8_t s_registry_ready;
static uint8_t s_command_registered;
static uint8_t s_last_wifi_on;

/**
 * @brief 检查给定可读容量内是否存在 NUL。
 * @param[in] text 借用的待检查字符缓冲。
 * @param[in] capacity 最多检查的可读字节数。
 * @return 非空且容量内找到终止符返回 1，否则返回 0。
 */
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

/**
 * @brief 显式映射 WiFi 服务状态，不依赖枚举数值一致。
 * @param[in] state WiFi 服务连接状态。
 * @return 对应 UI 状态；未知状态降级为 ERROR。
 */
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

/**
 * @brief 显式映射 WiFi 服务错误到 UI 域。
 * @param[in] error WiFi 服务连接错误。
 * @return 对应 UI 错误；未知错误降级为 NOT_READY。
 */
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

/**
 * @brief 验证连接域载荷并把有类型请求复制后交给 WiFi 请求执行器。
 * @param[in] command 扫描、重连、忘记或连接命令。
 * @param[in] payload 连接命令借用连接参数；其余命令必须为 NULL。
 * @param[in] payload_size 连接参数的完整大小；无载荷命令必须为零。
 * @param[out] out_ticket 必填请求编号，调用前清零，受理成功由服务写入。
 * @return 格式无效返回 INVALID_ARG；否则原样返回请求受理结果。
 * @note D_OK 只表示受理，调用方须按请求编号及种类读取完成确认；密码不进入值快照。
 */
static int product_ui_connection_gc9307_request(uint16_t command,
    const void *payload, uint16_t payload_size, uint32_t *out_ticket)
{
    const product_ui_connection_wifi_connect_t *req;
    wifi_service_request_t request = {0};
    if (out_ticket == NULL) return D_ERR_INVALID_ARG;
    *out_ticket = 0U;

    switch (command) {
    case PRODUCT_UI_CONNECTION_WIFI_SCAN_START:
        if ((payload != NULL) || (payload_size != 0U)) {
            return (int)D_ERR_INVALID_ARG;
        }
        request.kind = WIFI_SERVICE_REQUEST_SCAN;
        break;

    case PRODUCT_UI_CONNECTION_WIFI_RECONNECT:
        if ((payload != NULL) || (payload_size != 0U)) {
            return (int)D_ERR_INVALID_ARG;
        }
        request.kind = WIFI_SERVICE_REQUEST_RECONNECT;
        break;

    case PRODUCT_UI_CONNECTION_WIFI_FORGET:
        if ((payload != NULL) || (payload_size != 0U)) {
            return (int)D_ERR_INVALID_ARG;
        }
        request.kind = WIFI_SERVICE_REQUEST_FORGET;
        break;

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
        request.kind = WIFI_SERVICE_REQUEST_CONNECT;
        memcpy(request.ssid, req->ssid, sizeof(request.ssid));
        memcpy(request.pwd, req->pwd, sizeof(request.pwd));
        break;

    default:
        return (int)D_ERR_INVALID_ARG;
    }
    return (int)wifi_service_request_submit(&request, out_ticket);
}

/**
 * @brief 兼容通用注册表命令，使用相同非等待请求入口但不向调用方返回编号。
 * @param[in] command 连接域命令编号。
 * @param[in] payload 调用期间借用的载荷。
 * @param[in] payload_size 载荷字节数。
 * @param[in] user_data 注册上下文，当前未使用。
 * @return 原样返回受理结果；零不表示后台完成。
 */
static int product_ui_connection_gc9307_command(uint16_t command,
    const void *payload, uint16_t payload_size, void *user_data)
{
    uint32_t ticket;
    (void)user_data;
    return product_ui_connection_gc9307_request(command, payload, payload_size, &ticket);
}

/**
 * @brief 把 WiFi 请求种类显式映射到 UI 域。
 * @param[in] kind WiFi 服务请求种类。
 * @return 对应 UI 请求种类；未知值为 NONE。
 */
static product_ui_connection_request_kind_e product_ui_connection_map_request_kind(
    wifi_service_request_kind_e kind)
{
    switch (kind) {
    case WIFI_SERVICE_REQUEST_CONNECT: return PRODUCT_UI_CONN_REQUEST_CONNECT;
    case WIFI_SERVICE_REQUEST_SCAN: return PRODUCT_UI_CONN_REQUEST_SCAN;
    case WIFI_SERVICE_REQUEST_RECONNECT: return PRODUCT_UI_CONN_REQUEST_RECONNECT;
    case WIFI_SERVICE_REQUEST_FORGET: return PRODUCT_UI_CONN_REQUEST_FORGET;
    default: return PRODUCT_UI_CONN_REQUEST_NONE;
    }
}

/**
 * @brief 复制请求及完成编号、种类、结果与原始完成 SSID，并映射阶段。
 * @param[in,out] snapshot 接收请求投影的 UI 快照，其他状态字段保留。
 * @param[in] request 借用的 WiFi 请求快照。
 * @note 完成 SSID 保留原始身份字节，只补终止符，不做显示用 UTF-8 清洗。
 */
static void product_ui_connection_project_request(product_ui_connection_snapshot_t *snapshot,
    const wifi_service_request_snapshot_t *request)
{
    snapshot->request_version = request->revision;
    snapshot->request_id = request->request_id;
    snapshot->completed_id = request->completed_id;
    snapshot->request_kind = product_ui_connection_map_request_kind(request->kind);
    snapshot->completed_kind = product_ui_connection_map_request_kind(request->completed_kind);
    snapshot->completed_result = request->completed_result;
    memcpy(snapshot->completed_ssid, request->completed_ssid, sizeof(snapshot->completed_ssid));
    snapshot->completed_ssid[sizeof(snapshot->completed_ssid) - 1U] = '\0';
    switch (request->phase) {
    case WIFI_SERVICE_REQUEST_QUEUED: snapshot->request_phase = PRODUCT_UI_CONN_REQUEST_QUEUED; break;
    case WIFI_SERVICE_REQUEST_RUNNING: snapshot->request_phase = PRODUCT_UI_CONN_REQUEST_RUNNING; break;
    case WIFI_SERVICE_REQUEST_COMPLETE: snapshot->request_phase = PRODUCT_UI_CONN_REQUEST_COMPLETE; break;
    default: snapshot->request_phase = PRODUCT_UI_CONN_REQUEST_IDLE; break;
    }
}

/**
 * @brief 发布无线后端不可用的投影，保留请求完成信息并使状态缓存失效。
 */
static void product_ui_connection_gc9307_publish_unavailable(void)
{
    product_ui_connection_snapshot_t snap;
    wifi_service_request_snapshot_t request = {0};

    memset(&snap, 0, sizeof(snap));
    snap.ready = 0U;
    snap.wifi_capable = 0U;
    snap.service_version = wifi_service_get_status_version();
    wifi_service_get_request_snapshot(&request);
    product_ui_connection_project_request(&snap, &request);
    (void)product_ui_connection_service_publish(&snap);
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                     PRODUCT_UI_SERVICE_UNAVAILABLE, 0U, 0);
    s_registry_ready = 0U;
    s_have_valid_snapshot = 0U;
    /* WiFi 重新开启时强制完整重读，即使版本未变化。 */
    s_seen_status_version = 0xFFFFFFFFU;
}

/**
 * @brief 最多读取两次内存缓存快照，选取版本前后稳定的一次。
 * @param[out] out 接收稳定的连接状态。
 * @param[out] out_version 接收与状态匹配的版本。
 * @return 稳定快照写入并返回 0；空输出或两次都竞争返回 -1，不写输出。
 * @note 不执行同步 VM 或 RSSI 采样；两次竞争后留给下个周期重试。
 */
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

    /* 最多读取两次内存缓存快照；第二对版本仍竞争时放弃，
     * 不发布，也不更新已见版本。 */
    v0_before = wifi_service_get_status_version();
    wifi_service_get_cached_status(&first);
    v0_after = wifi_service_get_status_version();
    if (v0_before == v0_after) {
        *out = first;
        *out_version = v0_after;
        return 0;
    }

    v1_before = v0_after;
    wifi_service_get_cached_status(&second);
    v1_after = wifi_service_get_status_version();
    select = product_ui_connection_stable_select(v0_before, v0_after,
                                                 v1_before, v1_after);
    if (select == PRODUCT_UI_CONN_STABLE_USE_SECOND) {
        *out = second;
        *out_version = v1_after;
        return 0;
    }
    /* 两次竞争或异常选择结果均不发布。 */
    return -1;
}

/**
 * @brief 按无线就绪边沿及状态／请求版本决定采样，成功发布后才更新已见版本。
 * @note 版本零可为首次启动的合法快照；WiFi 重新开启时强制重新读取，不能以版本判断就绪。
 */
static void product_ui_connection_gc9307_sync(void)
{
    product_ui_connection_snapshot_t snap;
    wifi_service_status_t wifi;
    wifi_service_request_snapshot_t request = {0};
    uint32_t version;
    uint8_t wifi_on;
    product_ui_connection_sync_action_e action;

    /* 执行器先修改状态再发布请求完成；先读请求，再采样稳定状态。 */
    wifi_service_get_request_snapshot(&request);
    wifi_on = wifi_is_on() ? 1U : 0U;

    if (wifi_on == 0U) {
        /* 后端未启动（初始化失败或无线关闭）时清除有效缓存；
         * 后续开启边沿即使版本匹配也必须重读。 */
        product_ui_connection_gc9307_publish_unavailable();
        s_last_wifi_on = 0U;
        return;
    }

    /* 关闭转开启时使跳读缓存失效，强制完整状态读取。 */
    if (s_last_wifi_on == 0U) {
        s_have_valid_snapshot = 0U;
        s_seen_status_version = 0xFFFFFFFFU;
    }
    s_last_wifi_on = 1U;

    version = wifi_service_get_status_version();
    action = product_ui_connection_sync_action(wifi_on, s_have_valid_snapshot,
                                               version, s_seen_status_version);
    if (action == PRODUCT_UI_CONN_SYNC_SKIP_READ && request.revision == s_seen_request_version) {
        return;
    }
    if (action == PRODUCT_UI_CONN_SYNC_UNAVAILABLE) {
        product_ui_connection_gc9307_publish_unavailable();
        return;
    }

    memset(&wifi, 0, sizeof(wifi));
    if (product_ui_connection_gc9307_stable_status(&wifi, &version) != 0) {
        /* 两次采样都发生竞争，保留已见版本并留待下轮重试。 */
        return;
    }

    memset(&snap, 0, sizeof(snap));
    snap.ready = 1U;
    snap.wifi_capable = 1U;
    snap.service_version = version; /* 首次启动的合法 IDLE 版本可以为零。 */
    snap.has_saved_credentials = wifi.has_saved_credentials;
    snap.conn_state = product_ui_connection_map_state(wifi.state);
    snap.conn_error = product_ui_connection_map_error(wifi.error);
    product_ui_connection_project_request(&snap, &request);

    if (product_ui_connection_service_publish(&snap) != 0) {
        return;
    }
    /* 仅发布成功后才允许后续跳过相同版本的完整读取。 */
    s_seen_status_version = version;
    s_seen_request_version = request.revision;
    s_have_valid_snapshot = 1U;

    if (s_registry_ready == 0U) {
        if (product_ui_service_publish(
                PRODUCT_UI_SERVICE_CONNECTION, PRODUCT_UI_SERVICE_READY,
                PRODUCT_UI_STATUS_CAP_WIFI, 0) == 0) {
            s_registry_ready = 1U;
        }
    }
}

/**
 * @brief 清除本适配回调绑定、重置采样状态并发布连接域不可用。
 * @note 启动失败的串行回退；清除绑定不排空已借用的回调。
 */
static void product_ui_connection_gc9307_cleanup(void)
{
    product_ui_connection_service_register_sync(NULL);
    product_ui_connection_service_register_request(NULL);
    if (s_command_registered != 0U) {
        (void)product_ui_service_register_command(
            PRODUCT_UI_SERVICE_CONNECTION, NULL, NULL);
        s_command_registered = 0U;
    }
    s_registry_ready = 0U;
    s_have_valid_snapshot = 0U;
    s_last_wifi_on = 0U;
    s_seen_status_version = 0xFFFFFFFFU;
    s_seen_request_version = 0xFFFFFFFFU;
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                     PRODUCT_UI_SERVICE_UNAVAILABLE, 0U, -1);
    {
        product_ui_connection_snapshot_t snap;

        memset(&snap, 0, sizeof(snap));
        (void)product_ui_connection_service_publish(&snap);
    }
}

/**
 * @brief 装配连接快照、请求及兼容命令回调，先发布不可用再执行首次同步。
 */
int product_ui_connection_gc9307_init(void)
{
    s_seen_status_version = 0xFFFFFFFFU;
    s_seen_request_version = 0xFFFFFFFFU;
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
    product_ui_connection_service_register_request(product_ui_connection_gc9307_request);

    /* 初始不可用，只有无线开启后的同步才发布就绪。 */
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

#endif /* Product UI 的 GC9307 与 WiFi 条件。 */
