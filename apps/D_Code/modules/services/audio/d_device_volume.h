/**
 * @file d_device_volume.h
 * @brief 统一设备播放音量、异步请求及闹铃占用的公开合同。
 */
#ifndef __D_DEVICE_VOLUME_H__
#define __D_DEVICE_VOLUME_H__

#include "framework/d_error.h"

#include <stdint.h>

#define D_DEVICE_VOLUME_MIN      0U
#define D_DEVICE_VOLUME_MAX      100U
#define D_DEVICE_VOLUME_TIMEOUT_MS 2000U

typedef struct {
    uint32_t revision;
    uint32_t request_id;
    uint32_t completed_id;
    uint32_t superseded_id;
    uint32_t backend_generation;
    d_err_t service_error;
    d_err_t result;
    uint8_t volume_percent;
    uint8_t target_percent;
    uint8_t restore_percent;
    uint8_t is_service_ready;
    uint8_t is_backend_ready;
    uint8_t is_pending;
    uint8_t is_in_progress;
    uint8_t is_timed_out;
    uint8_t is_confirmed;
    uint8_t is_quiescing;
    uint8_t can_request;
} d_device_volume_snapshot_t;

typedef d_err_t (*d_device_volume_backend_set_fn_t)(uint8_t volume_percent,
                                                     void *context);

typedef struct {
    d_device_volume_backend_set_fn_t set_volume;
    void *context;
} d_device_volume_backend_t;

typedef struct {
    uint32_t token;
    uint32_t revision;
    uint32_t completed_revision;
    d_err_t error;
    uint8_t ready;
    uint8_t requested;
    uint8_t held;
    uint8_t busy;
    uint8_t percent;
} d_device_volume_alert_snapshot_t;

/**
 * @brief 注册静态生命周期的闹铃音量后端。
 * @param[in] backend 复制的后端描述；回调及上下文须在后续服务运行中持续有效。
 * @return 参数无效返回 D_ERR_INVALID_ARG；回调忙或已注册不同后端返回 D_ERR_BUSY；成功返回 D_OK。
 * @note 后端回调仅由音量工作任务执行；本接口没有注销入口。
 */
d_err_t d_device_volume_register_alert_backend(const d_device_volume_backend_t *backend);
/**
 * @brief 非阻塞申请或更新闹铃音量占用。
 * @param[in] token 非零闹铃所有者代次。
 * @param[in] percent 1～100 的闹铃音量。
 * @return 受理或同一请求已受理返回 D_OK；参数错误、服务未就绪或占用冲突返回相应错误。
 * @note 受理不等于音量已生效；通过闹铃快照的 completed_revision 和 error 确认。媒体请求在闹铃释放前等待。
 */
d_err_t d_device_volume_alert_begin(uint32_t token, uint8_t percent);
/**
 * @brief 为匹配的闹铃代次申请释放音量占用。
 * @param[in] token 申请占用时使用的闹铃代次。
 * @return 代次不匹配返回 D_ERR_BUSY；释放已受理或已完成返回 D_OK。
 * @note 受理后由工作任务恢复媒体音量；held 清零才表示占用已释放。
 */
d_err_t d_device_volume_alert_end(uint32_t token);
/**
 * @brief 在锁内复制闹铃音量快照并计算超时视图。
 * @param[out] snapshot 接收快照；为空时不处理。
 */
void d_device_volume_get_alert_snapshot(d_device_volume_alert_snapshot_t *snapshot);

/**
 * @brief 初始化音量缓存并同步当前音量。
 * @return 忙时返回 D_ERR_BUSY；否则返回后端结果，没有后端时缓存同步返回 D_OK。
 */
d_err_t d_device_volume_init(void);

/**
 * @brief 只尝试一次创建持久音量工作任务。
 * @return 就绪返回 D_OK；正在创建返回 D_ERR_BUSY；其余为保存的启动错误或功能关闭时的 D_ERR_NOT_READY。
 * @note 不重置缓存音量；静态唤醒信号量保留到重启，创建失败后不自动重试。
 */
d_err_t d_device_volume_service_init(void);
/**
 * @brief 受理绝对音量请求，可替换尚未执行的请求。
 * @param[in] percent 0～100 的目标音量。
 * @param[out] request_id 可选输出；失败置 0，受理后写入非零请求编号。
 * @return 受理返回 D_OK；越界、未就绪或忙返回对应错误。
 * @note 只有 completed_id 和结果快照证明执行结束；superseded_id 表示被替换的请求。
 */
d_err_t d_device_volume_request_set(uint8_t percent, uint32_t *request_id);
/**
 * @brief 基于最新期望音量受理相对调整请求。
 * @param[in] step 有符号音量步长，最终音量截断到 0～100。
 * @param[out] request_id 可选输出；失败置 0，受理后写入非零请求编号。
 * @return 受理返回 D_OK；服务或后端未就绪、忙时返回对应错误。
 * @note 受理与执行完成分离；同槽待执行请求可能被替换。
 */
d_err_t d_device_volume_request_adjust(int8_t step, uint32_t *request_id);
/**
 * @brief 受理静音或恢复音量请求。
 * @param[in] is_mute 1 为静音，0 为恢复最近非零音量。
 * @param[out] request_id 可选输出；失败置 0，受理后写入请求编号。
 * @return 受理返回 D_OK；参数无效、未就绪或忙返回对应错误。
 */
d_err_t d_device_volume_request_mute(uint8_t is_mute, uint32_t *request_id);
/**
 * @brief 在锁内复制音量状态并更新超时与准入视图。
 * @param[out] snapshot 接收状态；为空时不处理。
 * @note 后端回调在锁外运行；超时标记不代表该回调已返回。
 */
void d_device_volume_get_snapshot(d_device_volume_snapshot_t *snapshot);
/**
 * @brief 禁止新请求，已受理的工作仍继续执行。
 */
void d_device_volume_quiesce_begin(void);
/**
 * @brief 撤销静默准入限制。
 */
void d_device_volume_quiesce_cancel(void);
/**
 * @brief 轮询等待媒体请求和闹铃占用全部空闲。
 * @param[in] timeout_ms 最多等待毫秒数；0 表示只检查当前状态。
 * @return 全部空闲返回 D_OK；超过期限返回 D_ERR_TIMEOUT。
 * @note 用于可阻塞的后台或关机场景；请求失败但回调已经返回时仍可达到空闲。
 */
d_err_t d_device_volume_wait_idle(uint32_t timeout_ms);

/**
 * @brief 注册唯一媒体音量后端并同步缓存音量。
 * @param[in] backend 服务复制描述；回调和上下文在注销成功前须有效。
 * @return 成功返回 D_OK；参数无效返回 D_ERR_INVALID_ARG；占用冲突返回 D_ERR_BUSY；其他错误来自同步回调。
 * @note 相同回调与上下文可重复注册；不同后端不替换。同步回调在锁外执行，新注册后端同步失败时撤销该后端。
 */
d_err_t d_device_volume_register_backend(const d_device_volume_backend_t *backend);

/**
 * @brief 注销匹配的媒体音量后端。
 * @param[in] backend 预期移除的回调与上下文对。
 * @return 已解除或本来为空返回 D_OK；不同后端返回 D_ERR_NOT_FOUND；在途回调或闹铃占用返回 D_ERR_BUSY；无效参数返回 D_ERR_INVALID_ARG。
 * @note D_ERR_BUSY 时调用方须保留回调资源并重试；进入注销后会拒绝新媒体请求并丢弃待执行请求。
 */
d_err_t d_device_volume_unregister_backend(const d_device_volume_backend_t *backend);
/**
 * @brief 读取最近确认的媒体音量。
 * @return 0～100 的缓存音量。
 */
uint8_t d_device_volume_get(void);
/**
 * @brief 同步设置媒体音量，数值截断到有效范围。
 * @param[in] volume_percent 请求音量；超过 100 时按 100 处理。
 * @return 占用时返回 D_ERR_BUSY；否则返回后端结果，无后端时仅更新缓存并返回 D_OK。
 * @note 后端回调在调用者上下文中执行。
 */
d_err_t d_device_volume_set(uint8_t volume_percent);
/**
 * @brief 同步调整当前期望媒体音量。
 * @param[in] step_percent 有符号步长，结果截断到 0～100。
 * @return 占用时返回 D_ERR_BUSY；否则返回后端结果，无后端时仅更新缓存。
 */
d_err_t d_device_volume_adjust(int8_t step_percent);
/**
 * @brief 同步静音或恢复最近非零音量。
 * @param[in] is_mute 非零静音，0 恢复。
 * @return 占用时返回 D_ERR_BUSY；否则返回后端结果，无后端时更新缓存。
 */
d_err_t d_device_volume_set_mute(uint8_t is_mute);

#endif /* __D_DEVICE_VOLUME_H__ */
