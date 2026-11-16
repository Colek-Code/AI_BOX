/**
 * @file product_ui_connection_service.h
 * @brief 连接域的值快照及命令合同；不向 UI 暴露 WiFi SDK 头文件或密码。
 */

#ifndef __PRODUCT_UI_CONNECTION_SERVICE_H__
#define __PRODUCT_UI_CONNECTION_SERVICE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include <stdint.h>

/* 与 wifi_service 的 SSID/密码边界一致，但此处不引入 WiFi 头文件。
 * WIFI_SSID_MAXLEN=33、WIFI_PWD_MAXLEN=65，均包含末尾 NUL。 */
#define PRODUCT_UI_CONNECTION_SSID_MAX  33U
#define PRODUCT_UI_CONNECTION_PWD_MAX   65U

/* Product UI 自有连接状态及错误码。平台适配必须显式映射原厂枚举，
 * 不依赖枚举数值恰好一致。 */
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

typedef enum {
    PRODUCT_UI_CONN_REQUEST_NONE = 0,
    PRODUCT_UI_CONN_REQUEST_CONNECT,
    PRODUCT_UI_CONN_REQUEST_SCAN,
    PRODUCT_UI_CONN_REQUEST_RECONNECT,
    PRODUCT_UI_CONN_REQUEST_FORGET,
} product_ui_connection_request_kind_e;

typedef enum {
    PRODUCT_UI_CONN_REQUEST_IDLE = 0,
    PRODUCT_UI_CONN_REQUEST_QUEUED,
    PRODUCT_UI_CONN_REQUEST_RUNNING,
    PRODUCT_UI_CONN_REQUEST_COMPLETE,
} product_ui_connection_request_phase_e;

typedef struct {
    char ssid[PRODUCT_UI_CONNECTION_SSID_MAX];
    char pwd[PRODUCT_UI_CONNECTION_PWD_MAX];
} product_ui_connection_wifi_connect_t;

/** 控制中心及其他 UI 消费者使用的 WLAN 值投影；由平台适配发布，
 * UI 模块不直接调用 wifi_service_*。 */
typedef struct {
    uint32_t revision;
    uint32_t service_version;
    uint8_t wifi_capable;
    uint8_t has_saved_credentials;
    uint8_t conn_state;
    uint8_t conn_error;
    uint8_t ready;
    uint32_t request_version;
    uint32_t request_id;
    uint32_t completed_id;
    product_ui_connection_request_kind_e request_kind;
    product_ui_connection_request_kind_e completed_kind;
    product_ui_connection_request_phase_e request_phase;
    int completed_result;
    char completed_ssid[PRODUCT_UI_CONNECTION_SSID_MAX];
} product_ui_connection_snapshot_t;

typedef void (*product_ui_connection_sync_cb_t)(void);
typedef int (*product_ui_connection_request_cb_t)(uint16_t command,
    const void *payload, uint16_t payload_size, uint32_t *out_ticket);

/**
 * @brief 在串行启动阶段重置连接快照及两个独立回调槽。
 * @return 成功返回 0；互斥量创建失败返回 -1。
 * @note 重复调用也会清空数据；这不是并发拆卸或在途回调排空操作。
 */
int product_ui_connection_service_init(void);
/**
 * @brief 替换同步回调槽，NULL 表示清除。
 * @param[in] callback 调用方保有生命周期的回调；初始化前调用被忽略。
 * @note 锁内替换、锁外调用；清除或替换不等待已借用的调用结束，调用方须继续保留回调代码及其状态。
 */
void product_ui_connection_service_register_sync(
    product_ui_connection_sync_cb_t callback);
/**
 * @brief 借用当前同步回调并在互斥量外调用。
 * @note 未初始化或未绑定时不操作；快照 ready 不作为调用门槛，回调可重新进入本服务。
 */
void product_ui_connection_service_sync(void);
/**
 * @brief 替换独立请求回调槽，NULL 表示清除。
 * @param[in] callback 借用的请求提供者；初始化前调用被忽略。
 * @note 生命周期及替换规则与同步槽相同；不影响同步槽，不排空已经借用的请求调用。
 */
void product_ui_connection_service_register_request(product_ui_connection_request_cb_t callback);
/**
 * @brief 借用请求提供者并原样透传其受理结果及请求编号。
 * @param[in] command 连接域命令，由提供者校验。
 * @param[in] payload 本次调用借用的命令载荷；提供者返回前完成验证与拷贝。
 * @param[in] payload_size 载荷字节数。
 * @param[out] out_ticket 必填，调用前清零；提供者在 D_OK 时应写入有效请求编号。
 * @return out_ticket 为 NULL 返回 D_ERR_INVALID_ARG；未初始化或未绑定返回 D_ERR_NOT_READY；其他返回值由提供者决定。
 * @note 锁只保护回调借用，调用发生在锁外；返回后不复核输出。D_OK 只表示受理，完成须匹配快照编号和种类。
 */
int product_ui_connection_request(uint16_t command, const void *payload,
    uint16_t payload_size, uint32_t *out_ticket);
/**
 * @brief 复制平台连接投影，实际内容变化时推进连接及注册表代次。
 * @param[in] snapshot 调用期间借用的值快照；输入 revision 不直接发布。
 * @return 有效发布或内容未变返回 0；未初始化或空指针返回 -1。
 * @note 互斥量内更新快照，锁外通知注册表；完成 SSID 被限定长度并补终止符。
 */
int product_ui_connection_service_publish(
    const product_ui_connection_snapshot_t *snapshot);
/**
 * @brief 在互斥量内复制连接域快照。
 * @param[out] snapshot 接收值副本，不含密码且不借用服务内部指针。
 * @return 成功返回 0；未初始化或空指针返回 -1。
 */
int product_ui_connection_service_snapshot(
    product_ui_connection_snapshot_t *snapshot);

/**
 * @brief 经通用服务注册表同步分发连接域命令。
 * @param[in] command 连接域命令编号。
 * @param[in] payload 本次调用借用的命令载荷。
 * @param[in] payload_size 载荷字节数；非零时 payload 必须非空。
 * @return 原样返回通用注册表或处理器结果；失败及完成含义由具体命令定义。
 */
static inline int product_ui_connection_command(uint16_t command,
                                                const void *payload,
                                                uint16_t payload_size)
{
    return product_ui_service_command(PRODUCT_UI_SERVICE_CONNECTION, command,
                                      payload, payload_size);
}

#endif
#endif /* __PRODUCT_UI_CONNECTION_SERVICE_H__ */
