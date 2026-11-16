/** @file d_local_prompt.h @brief 番茄钟、试听和闹铃共享音频所有权与可确认交接接口。 */
#ifndef D_LOCAL_PROMPT_H
#define D_LOCAL_PROMPT_H
#include "framework/d_error.h"
#include <stdint.h>
typedef enum {
    D_LOCAL_PROMPT_POMODORO = 0,
    D_LOCAL_PROMPT_PREVIEW,
    D_LOCAL_PROMPT_ALARM,
    D_LOCAL_PROMPT_SOURCE_COUNT,
} d_local_prompt_source_e;

typedef struct {
    d_local_prompt_source_e source;
    uint32_t event_id;
    uint32_t generation;
    uint32_t created_ms;
    uint8_t volume;
} d_local_prompt_request_t;

typedef struct {
    uint32_t event_id;
    uint32_t generation;
    d_err_t error;
    uint8_t ready;
    uint8_t pending;
    uint8_t playing; /* START 已确认；仅在 STOP 检查成功后清除。 */
    uint8_t owned;   /* 同时涵盖 OPEN/START 在途与失败后保留的资源。 */
} d_local_prompt_source_snapshot_t;

typedef struct {
    d_err_t (*begin)(uint32_t token);
    d_err_t (*poll)(uint32_t token, uint8_t *ready);
    void (*release)(uint32_t token);
} d_local_prompt_handoff_t;

/**
 * @brief 注册闹铃抢占语音与媒体的交接回调。
 * @param[in] handoff 非阻塞交接回调描述，指针及回调须具有静态生命周期。
 * @return 参数不完整返回 D_ERR_INVALID_ARG；已经注册不同描述指针返回 D_ERR_BUSY；成功返回 D_OK。
 * @note 回调在提示音锁外执行；同一描述指针重复注册允许。
 */
d_err_t d_local_prompt_register_alarm_handoff(const d_local_prompt_handoff_t *handoff);
/**
 * @brief 按来源和代次受理提示音、试听或闹铃意图。
 * @param[in] request 复制的请求；番茄钟要求 generation 等于 event_id，其他来源代次递增，音量为 1～100。
 * @return 受理及相同事件的同代次请求返回 D_OK；参数、就绪或准入冲突返回对应错误。
 * @note D_OK 仅表示受理，不表示已播放；闹铃优先于试听。来源间事件编号独立。
 */
d_err_t d_local_prompt_request_source(const d_local_prompt_request_t *request);
/**
 * @brief 取消某来源中不新于指定代次的请求。
 * @param[in] source 目标提示音来源，无效来源不处理。
 * @param[in] through_generation 取消上界，0 表示当前代次；番茄钟按事件编号解释。
 * @note 只提交停止意图，不等待解码器或音量释放。
 */
void d_local_prompt_cancel_source(d_local_prompt_source_e source, uint32_t through_generation);
/**
 * @brief 读取来源的请求、播放与资源占用状态。
 * @param[in] source 目标来源；无效来源输出全零快照。
 * @param[out] snapshot 接收快照；为空时不处理。
 * @note playing 需 START 已确认；owned 同时涵盖 OPEN、START 在途及失败后保留的资源。
 */
void d_local_prompt_get_source_snapshot(d_local_prompt_source_e source, d_local_prompt_source_snapshot_t *snapshot);
/**
 * @brief 检查是否存在闹铃请求或尚未进入释放阶段的闹铃所有者。
 * @return 存在时返回非零，否则为 0。
 */
uint8_t d_local_prompt_alarm_requested(void);
typedef struct {
    d_err_t error;
    uint32_t event_id;
    uint32_t generation;
    uint8_t ready;
    uint8_t playing;
    uint8_t pending;
    uint8_t foreground;
    uint8_t quiescing;
} d_local_prompt_snapshot_t;
/**
 * @brief 一次性创建持久提示音任务并注册所需音量后端。
 * @return 初始化成功返回 D_OK；失败返回实际错误；再次调用未就绪状态返回 D_ERR_NOT_READY。
 * @note 初始化尝试不自动重试；线程及后端就绪仍分别受实际注册结果限制。
 */
d_err_t d_local_prompt_init(void);
/**
 * @brief 注册用于检查上层音频忙碌状态的探针。
 * @param[in] probe 静态生命周期、非阻塞的快照探针。
 * @return 空回调返回 D_ERR_INVALID_ARG；已注册不同探针返回 D_ERR_BUSY；成功返回 D_OK。
 * @note 服务在锁外调用探针，避免向上层形成直接实现依赖。
 */
d_err_t d_local_prompt_register_foreground_probe(uint8_t (*probe)(void));
/**
 * @brief 复制番茄钟提示音的当前状态。
 * @param[out] snapshot 接收快照；为空时不处理。
 */
void d_local_prompt_get_snapshot(d_local_prompt_snapshot_t *snapshot);
/**
 * @brief 按最新事件优先策略提交番茄钟提示音。
 * @param[in] event_id 非零、按环形序列递增的事件编号。
 * @param[in] created_ms 设备单调时钟上的事件创建时刻。
 * @note 兼容入口忽略受理错误；等待超过 30 秒的事件不会播放。
 */
void d_local_prompt_request(uint32_t event_id, uint32_t created_ms);
/**
 * @brief 取消不新于指定事件的番茄钟提示音。
 * @param[in] event_id 取消上界，0 取当前事件。
 * @note 非阻塞，只提交停止意图。
 */
void d_local_prompt_cancel(uint32_t event_id);
/**
 * @brief 使用兼容所有者位 1 请求前台占用。
 * @note 停止等待由后台音频所有者调用 wait_stopped 执行。
 */
void d_local_prompt_foreground_begin(void);
/**
 * @brief 释放兼容所有者位 1 的前台占用。
 */
void d_local_prompt_foreground_end(void);
/**
 * @brief 独立设置或清除指定所有者的前台占用位。
 * @param[in] owner_bit 调用方独占的位掩码，不可清除其他所有者位。
 * @param[in] held 非零占用，0 释放。
 * @note 占用会取消试听并停止非闹铃播放；闹铃不因该位直接被停止。
 */
void d_local_prompt_foreground_set(uint32_t owner_bit, uint8_t held);
/**
 * @brief 在后台限时等待解码与闹铃所有权释放。
 * @param[in] timeout_ms 最大等待毫秒数，0 只检查当前状态。
 * @return 已空闲返回 D_OK；仍有有效闹铃请求返回 D_ERR_BUSY；清理错误返回 D_ERR_IO；超时返回 D_ERR_TIMEOUT。
 * @note 不自行提交停止；超时或错误后不能视为资源已释放。
 */
d_err_t d_local_prompt_wait_stopped(uint32_t timeout_ms);
/**
 * @brief 向提示音任务提交媒体音量并限时等待完成确认。
 * @param[in] percent 0～100 的音量。
 * @param[in] timeout_ms 等待完成确认的毫秒期限。
 * @return 成功返回 D_OK；越界、未就绪、上项未完成、解码错误或超时返回对应错误。
 * @note 仅由设备音量工作任务调用，不可在 UI 或提示音工作任务中调用；超时会保留在途请求并触发停止。
 */
d_err_t d_local_prompt_apply_volume(uint8_t percent, uint32_t timeout_ms);
/**
 * @brief 禁止新播放，取消全部待执行来源并请求停止。
 */
void d_local_prompt_quiesce_begin(void);
/**
 * @brief 解除静默准入限制，不恢复已经取消的请求。
 */
void d_local_prompt_quiesce_cancel(void);
#endif
