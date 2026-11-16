/** @file product_ui_pomodoro_service.h @brief 独立计时及保存所有者；持久化完成确认不覆盖仍在更新的活动数据。 */
#ifndef PRODUCT_UI_POMODORO_SERVICE_H
#define PRODUCT_UI_POMODORO_SERVICE_H
#include "product_ui_pomodoro_model.h"

typedef struct {
    product_ui_pomodoro_model_t model;
    uint32_t today;
    uint32_t request_id;
    uint32_t completed_id;
    uint32_t persisted_revision;
    uint32_t save_started_ms;
    d_err_t timer_error;
    d_err_t storage_error;
    d_err_t audio_error;
    /* IO／NOT_READY：显式重读两槽前禁止覆盖；INVALID_ARG：记录可读但无效，
     * 使用默认值；NOT_FOUND：已经证实为空。 */
    d_err_t load_error;
    uint8_t timer_ready;
    uint8_t storage_ready;
    uint8_t saving;
    uint8_t quiescing;
    uint8_t has_record;
} product_ui_pomodoro_snapshot_t;

/**
 * @brief 一次性读取 A/B 记录，建立后台保存及独立计时。
 * @return 计时器成功为 D_OK，计时器失败为 IO；并发初始化在途为 BUSY，重复调用返回计时器结果。
 * @note 返回成功不代表保存 worker 或记录可写；storage_ready、load_error 及 storage_error 另从快照读取。空记录启动不自动写默认值。
 */
d_err_t product_ui_pomodoro_init(void);
/**
 * @brief 复制服务值快照并附带共享提示音当前错误。
 * @param[out] snapshot 接收值副本，NULL 时忽略。
 * @note 模型在临界区复制，提示音在锁外独立读取；不是跨服务原子快照。
 */
void product_ui_pomodoro_get_snapshot(product_ui_pomodoro_snapshot_t *snapshot);
/**
 * @brief 先结算单调时间，再按操作代次执行计时命令。
 * @param[in] command 开始、暂停、继续、跳过或结束。
 * @param[in] generation UI 操作绑定的当前模型代次。
 * @return 未初始化、无计时器或静默为 NOT_READY；其余返回模型命令结果。
 * @note 跳过或结束成功时在临界区外取消对应提示音；停止资源是否释放另由提示音所有者确认。
 */
d_err_t product_ui_pomodoro_execute(product_ui_pomodoro_command_e command, uint32_t generation);
/**
 * @brief 立即更新活动设置并生成后台保存请求。
 * @param[in] settings 本次复制的已校验设置。
 * @param[out] request 可为空，预置零；更新成功后即返回新请求编号，即使存储尚不可用。
 * @return 参数无效为 INVALID_ARG，未初始化或静默为 NOT_READY；更新后返回存储受理／加载保护结果。
 * @note 存储错误不回滚本次 RAM 设置；D_OK 也只表示待保存。关闭提示音会取消当前事件，完成确认仍读快照 completed_id 与持久化代次。
 */
d_err_t product_ui_pomodoro_save_settings(const product_ui_pomodoro_settings_t *settings, uint32_t *request);
/**
 * @brief 立即清空活动统计并生成保存请求，不改变当前阶段。
 * @param[out] request 可为空，预置零；修改发生后接收新请求编号。
 * @return 未初始化或静默为 NOT_READY；其余返回存储受理或加载保护结果。
 * @note 存储失败保留已清空的 RAM 数据，迟到首次加载也不得恢复旧统计。
 */
d_err_t product_ui_pomodoro_clear_stats(uint32_t *request);
/**
 * @brief 显式重试受保护的首次读取或失败保存，同步返回受理情况。
 * @param[out] request 可为空；预置零后返回当前或新请求编号。
 * @return 未就绪为 NOT_READY；已有保存或无需保存为 D_OK；存在 worker 时新重试受理为 D_OK，其他返回存储错误。
 * @note D_OK 不表示已恢复或保存；后台重新读取两槽，成功后按会话变更合并一次。
 */
d_err_t product_ui_pomodoro_retry_save(uint32_t *request);
/**
 * @brief 匹配当前事件时确认其提示，并取消不晚于指定事件的声音。
 * @param[in] event_id 用户已处理的完成事件编号；零不向声音服务取消。
 */
void product_ui_pomodoro_acknowledge(uint32_t event_id);
/**
 * @brief 先结算当前计时，再暂停运行阶段、关闭新操作并静默共享提示音。
 */
void product_ui_pomodoro_quiesce_begin(void);
/**
 * @brief 解除业务及提示音静默，已暂停阶段仍需用户显式继续。
 */
void product_ui_pomodoro_quiesce_cancel(void);
/**
 * @brief 限时等待脏保存数据及在途 IO 收敛。
 * @param[in] timeout_ms 最大等待毫秒数，零只检查当前状态。
 * @return 无脏数据且无在途保存为 D_OK；停止保存后的存储错误原样返回，等待超时为 TIMEOUT。
 * @note 不等待共享提示音停止；音频所有权需独立等待。超时不释放静态同步或在途缓冲。
 */
d_err_t product_ui_pomodoro_wait_idle(uint32_t timeout_ms);
/**
 * @brief 静默并停止计时器，要求保存 worker 在工作结束后退出。
 * @param[in] timeout_ms 最多等待 worker 停止的毫秒数。
 * @return worker 停止后返回保存空闲检查结果；期限到达返回 TIMEOUT。
 * @note 错误或超时继续保留静态状态及同步对象，不能视为所有数据已经保存。
 */
d_err_t product_ui_pomodoro_service_stop(uint32_t timeout_ms);
#endif
