/** @file ws_service.h @brief 不包含云协议的单实例 WS/WSS 传输服务合同。 */
#ifndef D_WS_SERVICE_H
#define D_WS_SERVICE_H
#include "framework/d_error.h"
#include "ws_protocol.h"
#include <stdint.h>
typedef enum { WS_CLIENT, WS_SERVER } ws_role_t;
#define WS_CAP_CLIENT_WS  1U
#define WS_CAP_SERVER_WS  2U
#define WS_CAP_CLIENT_WSS 4U
#define WS_CAP_SERVER_WSS 8U
typedef enum { WS_STOPPED, WS_WAIT_NETWORK, WS_CONNECTING, WS_LISTENING,
               WS_UPGRADING, WS_OPEN, WS_BACKOFF, WS_PAUSED, WS_CLOSING, WS_FAILED } ws_state_t;
typedef enum { WS_EVENT_STATE, WS_EVENT_DATA, WS_EVENT_CONTROL } ws_event_kind_t;
typedef enum { WS_HOLD_OTA = 1, WS_HOLD_POWER = 2, WS_HOLD_EXTERNAL = 4 } ws_hold_owner_t;
typedef struct {
    ws_event_kind_t kind;
    ws_state_t state;
    uint32_t generation;
    d_err_t error;
    uint8_t type, flags;
    const uint8_t *data; /* 仅在回调期间借用，内容可含零字节。 */
    uint32_t length;
} ws_event_t;
/** 运行于传输工作任务；仅执行有界工作和非阻塞控制请求，事件与数据均为调用期借用。 */
typedef void (*ws_event_callback_t)(const ws_event_t *event, void *user);
typedef struct {
    ws_role_t role;
    const char *url; /* 客户端为 ws(s)://host[:port]/path；服务端为精确匹配的 Upgrade 路径。 */
    uint16_t listen_port;
    uint8_t secure; /* 仅用于服务端；客户端模式由 URL 决定。 */
    const char *origin, *user_agent;
    const unsigned char *ca, *certificate, *private_key;
    uint32_t ca_length, certificate_length, private_key_length;
    /* 证书与密钥字节必须保留到停止且工作任务及其资源均已释放。 */
    uint32_t io_timeout_ms, maximum_message;
    uint16_t receive_slice, fragment_size;
    uint16_t ping_interval_s, pong_timeout_s;
    uint8_t fragment_send, resume_tls_session;
    ws_event_callback_t callback;
    void *user;
} ws_service_config_t;
typedef struct {
    ws_state_t state;
    d_err_t error;
    uint32_t generation, connections, received_bytes, sent_bytes;
    uint8_t requested, worker_active, maintenance, resources_owned;
} ws_service_snapshot_t;
/**
 * @brief 填充客户端传输的默认配置。
 * @param[out] config 默认配置输出；为空时不处理。
 */
void ws_service_defaults(ws_service_config_t *config);
/**
 * @brief 读取当前构建可用的 WS/WSS 角色能力。
 * @return WS_CAP_* 位掩码。
 */
uint32_t ws_service_capabilities(void);
/**
 * @brief 初始化端口同步对象并将服务标记为可配置。
 * @return 成功返回 D_OK；端口初始化失败返回其错误。
 */
d_err_t ws_service_init(void);
/**
 * @brief 在传输空闲时绑定单实例配置。
 * @param[in] config 非空配置；URL、Origin、User-Agent 复制到内部缓冲，回调上下文与证书材料持续借用。
 * @return 成功返回 D_OK；配置无效、能力或初始化未就绪、资源或维护占用返回对应错误。
 * @note 借用材料至少保留到 worker_active 和 resources_owned 均为 0；配置不启动连接。
 */
d_err_t ws_service_configure(const ws_service_config_t *config);
/**
 * @brief 创建一代工作任务并受理连接意图。
 * @return 受理返回 D_OK；未初始化或未配置返回 D_ERR_NOT_READY；旧资源或维护占用返回 D_ERR_BUSY；创建失败返回端口错误。
 * @note D_OK 不表示已握手成功，状态通过快照和工作任务回调确认。
 */
d_err_t ws_service_start(void);
/**
 * @brief 撤销连接意图并使当前代次失效。
 * @return 当前实现返回 D_OK。
 * @note 非阻塞受理取消；须等待 worker_active 和 resources_owned 均为 0 才确认完成。
 */
d_err_t ws_service_stop(void);
/**
 * @brief 请求停止，并仅在工作任务和资源均释放后解除配置。
 * @return 解除成功返回 D_OK；尚在收尾返回 D_ERR_BUSY，调用方稍后重试。
 */
d_err_t ws_service_deinit(void);
/**
 * @brief 由应用任务有界同步发送一条消息或控制帧。
 * @param[in] type WS_TEXT、WS_BINARY、WS_PING、WS_PONG 或 WS_CLOSE。
 * @param[in] data 调用期间借用的负载；长度为 0 时允许为空。
 * @param[in] length 负载字节数；控制帧最多 125，数据消息受 maximum_message 约束。
 * @return 写入完成返回 D_OK；参数、状态、回调重入、锁或进度期限错误返回对应值。
 * @note 不可在传输回调或 LVGL 中调用；文本需完整 UTF-8。部分帧写入失败会使连接不可继续使用，工作任务负责关闭。
 */
d_err_t ws_service_send(uint8_t type, const void *data, uint32_t length);
/**
 * @brief 同步发送指定内容的 PING 控制帧。
 * @param[in] data PING 负载；零长度允许为空。
 * @param[in] length 负载长度，不超过 125。
 * @return ws_service_send 的实际结果。
 */
d_err_t ws_service_ping(const void *data, uint32_t length);
/**
 * @brief 发送带关闭码及 UTF-8 原因的 CLOSE 帧。
 * @param[in] code 有效 RFC6455 关闭码。
 * @param[in] reason 可空、零结尾原因文本，最多 123 字节。
 * @return 发送结果；原因过长、关闭码或 UTF-8 无效返回 D_ERR_INVALID_ARG。
 * @note 成功表示 CLOSE 已写出，仍需工作任务执行关闭握手与资源收尾。
 */
d_err_t ws_service_close(uint16_t code, const char *reason);
/**
 * @brief 在元数据锁内复制当前传输状态。
 * @param[out] snapshot 状态输出；为空时不处理。
 */
void ws_service_get_snapshot(ws_service_snapshot_t *snapshot);
/**
 * @brief 为产品协调者增加维护占用并撤销当前连接代次。
 * @param[in] owner OTA、POWER 或 EXTERNAL 中的一个所有者位。
 * @return 有效 owner 返回 D_OK；无效 owner 返回 D_ERR_INVALID_ARG。
 * @note 非阻塞，仅请求工作任务清理，须 maintenance_poll 确认。
 */
d_err_t ws_service_maintenance_begin(ws_hold_owner_t owner);
/**
 * @brief 检查资源已释放且工作任务已暂停或退出。
 * @param[out] ready 非空完成确认输出，满足条件时为 1。
 * @return 正常查询返回 D_OK；空输出返回 D_ERR_INVALID_ARG。
 */
d_err_t ws_service_maintenance_poll(uint8_t *ready);
/**
 * @brief 清理完成后解除指定维护所有者位。
 * @param[in] owner 一个有效维护所有者位。
 * @return 解除成功返回 D_OK；仍持资源或尚未暂停返回 D_ERR_BUSY；所有者无效返回 D_ERR_INVALID_ARG。
 * @note 不会恢复已显式 stop 的连接意图。
 */
d_err_t ws_service_maintenance_release(ws_hold_owner_t owner);
/**
 * @brief 请求撤销协调者占用，资源清理后再真正解除。
 * @param[in] owner 一个有效维护所有者位。
 * @return 有效 owner 返回 D_OK；无效 owner 返回 D_ERR_INVALID_ARG。
 * @note 资源仍存在时记录延迟释放，由现有工作任务完成，不创建额外任务。
 */
d_err_t ws_service_maintenance_cancel(ws_hold_owner_t owner);
#endif
