/**
 * @file camera_preview_state.h
 * @brief 独立于 SDK 调度的会话意图、状态与绝对期限策略。
 * @author D_Code
 * @date 2026-09-11
 */
#ifndef __CAMERA_PREVIEW_STATE_H__
#define __CAMERA_PREVIEW_STATE_H__
#include "camera_preview.h"

#define CAMERA_PREVIEW_START_TIMEOUT_MS  5000U
#define CAMERA_PREVIEW_FRAME_TIMEOUT_MS  3000U
#define CAMERA_PREVIEW_STOP_TIMEOUT_MS   3000U

/* 调用者序列化这些短元数据操作；执行 SDK 操作时不持此锁。 */
typedef struct {
    camera_preview_snapshot_t status;
    uint32_t started_ms;
    uint32_t last_frame_ms;
    uint32_t stopped_ms;
    uint8_t has_stop_deadline;
    uint8_t is_transaction_claimed;
} camera_preview_state_t;

/**
 * @brief 按初始化结果建立可用或不可用的初始状态。
 * @param[out] state 接收初始状态的非空对象。
 * @param[in] result 实际初始化结果。
 */
void camera_preview_state_init(camera_preview_state_t *state, d_err_t result);
/**
 * @brief 受理新预览意图或复用已存在的会话。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 * @param[in] now_ms 当前毫秒计时值。
 * @param[out] session 成功时写入非零会话编号；失败时不改。
 * @return 受理返回 D_OK；空输出、未就绪、FAULT 或禁止启动时返回对应错误。
 * @note 受理不表示 SDK 已打开；旧清理未完成时拒绝新会话。
 */
d_err_t camera_preview_state_start(camera_preview_state_t *state, uint32_t now_ms,
                                   uint32_t *session);
/**
 * @brief 撤销匹配会话意图，并只在首次停止时建立期限。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 * @param[in] session 需要停止的会话编号。
 * @param[in] reason 首次停止原因。
 * @param[in] now_ms 停止请求时刻。
 * @note 旧会话、已清理或 FAULT 状态不处理。
 */
void camera_preview_state_stop(camera_preview_state_t *state, uint32_t session,
                               camera_preview_stop_e reason, uint32_t now_ms);
/**
 * @brief 检查启动、帧间隔及停止期限并更新诊断状态。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 * @param[in] now_ms 当前毫秒计时值。
 * @note 超时不证明 SDK 返回或资源释放；启动与帧超时以实际到期点作为停止期限起点。
 */
void camera_preview_state_check_time(camera_preview_state_t *state, uint32_t now_ms);
/**
 * @brief 保存 SDK 诊断并撤销当前会话意图。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 * @param[in] error 业务错误分类。
 * @param[in] sdk_result SDK 原始结果。
 * @param[in] now_ms 停止期限起点。
 */
void camera_preview_state_fail(camera_preview_state_t *state, camera_preview_error_e error,
                               int sdk_result, uint32_t now_ms);
/**
 * @brief 由资源所有者发布正常收尾或不可恢复故障。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 * @param[in] is_fault 非零进入 FAULT 并保留清理未完成事实。
 * @note 正常收尾才置 is_cleanup_complete；调用前由所有者确认真实资源状态。
 */
void camera_preview_state_finish(camera_preview_state_t *state, uint8_t is_fault);
/**
 * @brief 为当前请求领取一次 SDK 事务执行权。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 * @return 首次领取返回 1；无请求或已经领取返回 0。
 */
uint8_t camera_preview_state_claim(camera_preview_state_t *state);
/**
 * @brief 检查指定会话是否仍允许发布图像。
 * @param[in] state 非空状态对象；访问由调用者序列化。
 * @param[in] session 待核对会话编号。
 * @return 会话匹配、仍请求且处于启动或运行阶段时返回非零。
 */
uint8_t camera_preview_state_can_publish(const camera_preview_state_t *state, uint32_t session);
/**
 * @brief 记录有效输入帧时刻并累计捕获数。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 * @param[in] now_ms 捕获有效帧的时刻。
 */
void camera_preview_state_frame(camera_preview_state_t *state, uint32_t now_ms);
/**
 * @brief 在意图仍有效时转入运行阶段并累计发布数。
 * @param[in,out] state 非空状态对象；调用者序列化元数据访问，SDK 操作在锁外执行。
 */
void camera_preview_state_publish(camera_preview_state_t *state);
/**
 * @brief 在局部副本上检查期限并输出当前状态。
 * @param[in] state 非空原状态；本函数不修改该对象。
 * @param[in] now_ms 快照对应时刻。
 * @param[out] snapshot 非空快照输出。
 * @note 调用者序列化对原状态的访问；快照中的超时不回写原状态。
 */
void camera_preview_state_snapshot(const camera_preview_state_t *state, uint32_t now_ms,
                                   camera_preview_snapshot_t *snapshot);

#endif
