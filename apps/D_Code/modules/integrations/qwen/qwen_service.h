/**
 * @file    qwen_service.h
 * @brief   Qwen 多模态对话服务、快照及产品音频与维护交接合同。
 * @author  D_Code
 * @date    2026-05-19
 *
 * @note    基于阿里 Qwen RTOS SDK v1.1.0，通过 WebSocket 与云端交互。
 *          音频格式和采样率由 config/qwen_config.h 选择。
 *          功能在 config/app_config.h 选择，并由 config/internal/d_config_validate.h 校验互斥。
 */

#ifndef QWEN_SERVICE_H
#define QWEN_SERVICE_H

#include "framework/d_event_payloads.h"
#include "framework/d_error.h"
#include "qwen_session_policy.h"
#include "qwen_local_tool.h"

//*********************************************************************************//
//                              状态枚举                                            //
//*********************************************************************************//

typedef enum {
    QWEN_STATE_IDLE = 0,        /* 无活动会话；服务是否可用由快照 initialized 判定 */
    QWEN_STATE_REGISTERING,     /* License 注册中 */
    QWEN_STATE_TOKEN_REQ,       /* Token 获取中 */
    QWEN_STATE_CONNECTING,      /* WSS 连接中 */
    QWEN_STATE_READY,           /* 就绪，等待对话 */
    QWEN_STATE_LISTENING,       /* 拾音中（语音上行） */
    QWEN_STATE_THINKING,        /* 等待 LLM 响应 */
    QWEN_STATE_PLAYING,         /* TTS 播放中 */
    QWEN_STATE_ERROR,           /* 错误状态 */
} qwen_state_e;

typedef struct {
    qwen_session_snapshot_t session;
    qwen_state_e state;
    qwen_tool_snapshot_t tool; /* 仅运输结果，不是业务或保存完成确认。 */
} qwen_service_snapshot_t;

typedef enum {
    QWEN_VOLUME_SET,
    QWEN_VOLUME_ADJUST,
    QWEN_VOLUME_MUTE,
} qwen_volume_operation_e;

/**
 * @brief 在当前会话准入下非阻塞提交统一设备音量请求。
 * @param[in] operation 绝对设置、相对调整或静音。
 * @param[in] value SET 为 0～100，ADJUST 为 -100～100，MUTE 为 0 或 1。
 * @param[out] request_id 可选输出，失败置 0，成功写入请求编号。
 * @return 受理返回 D_OK；值无效返回 D_ERR_INVALID_ARG；会话、后端或队列未就绪及占用返回实际错误。
 * @note 不等待音频后端；完成通过设备音量快照确认。
 */
d_err_t qwen_service_request_volume(qwen_volume_operation_e operation,
                                    int16_t value, uint32_t *request_id);

/**
 * @brief 非阻塞受理会话启动，重复有效请求保持幂等。
 * @return 受理返回 D_OK；未就绪、维护或清理忙、致命故障及产品准入失败返回对应错误。
 * @note 可在普通任务或 LVGL 回调调用；产品音频 begin 必须非阻塞，认证与建链由后台执行。D_OK 不等于已连接。
 */
d_err_t qwen_service_session_start(void);

/**
 * @brief 撤销会话启动意图并请求后台清理。
 * @return 已受理或原本无请求返回 D_OK；服务未就绪返回 D_ERR_NOT_READY；致命故障返回 D_ERR_IO。
 * @note 快照区分清理中、失败与完成；返回成功不证明资源已释放。
 */
d_err_t qwen_service_session_stop(void);

/**
 * @brief 复制服务缓存、纯策略与工具通知，不调用网络或 SDK。
 * @param[out] snapshot 非空值快照输出。
 * @return 成功返回 D_OK；空输出返回 D_ERR_INVALID_ARG；未就绪返回 D_ERR_NOT_READY 且输出全零。
 * @note 工具快照为运输通知，不是业务存盘完成确认。
 */
d_err_t qwen_service_get_snapshot(qwen_service_snapshot_t *snapshot);
/**
 * @brief 空闲会话下提交 UI 激活码，或以 NULL 重试已有激活工作。
 * @param[in] code 有效激活码；为空表示重试。
 * @return 受理返回 D_OK；功能或服务未就绪返回 D_ERR_NOT_READY；工作、维护、闹铃或会话活动返回 D_ERR_BUSY；其余为激活输入错误。
 * @note 调用者不执行 I/O；受理后后台先停止待机与旧 SDK，再处理本地来源或网络激活。
 */
d_err_t qwen_service_activation_request(const char *code);

//*********************************************************************************//
//                              事件回调（发布到事件总线）                           //
//*********************************************************************************//

typedef enum {
    QWEN_EVT_STATE_CHANGED = 0, /* 状态变化，data = qwen_state_e* */
    QWEN_EVT_ASR_RESULT,        /* ASR 识别结果，data = char* (UTF-8) */
    QWEN_EVT_LLM_RESULT,        /* LLM 回复文本，data = char* (UTF-8) */
    QWEN_EVT_TTS_START,         /* TTS 开始播放 */
    QWEN_EVT_TTS_END,           /* TTS 播放结束 */
    QWEN_EVT_ERROR,             /* 错误，data = int32_t* (错误码) */
} qwen_event_e;

//*********************************************************************************//
//                              生命周期 API                                        //
//*********************************************************************************//

/**
 * @brief 装配接入与音频、创建持久工作任务并订阅事件，最后开放输入准入。
 * @return 完成返回 D_OK；重复初始化返回 D_ERR_BUSY；其他接入、同步、资源或订阅错误返回实际错误。
 * @note 初始化不直接等同认证或连接完成；在启动阶段串行执行。
 */
d_err_t qwen_service_init(void);

/**
 * @brief 关闭输入并等待借用、音量与唯一工作任务受检收尾。
 * @note 仅生命周期后台所有者调用；忙时记录需重试，清理失败保留可能被引用对象至重启。void 兼容入口不提供彻底销毁回执。
 */
void qwen_service_deinit(void);

//*********************************************************************************//
//                              对话控制 API                                        //
//*********************************************************************************//

/**
 * @brief 取得 SDK 借用后提交按键对讲开始上行。
 * @return 受理返回 D_OK；模式或状态未就绪返回 D_ERR_NOT_READY；已有上行或准入冲突返回 D_ERR_BUSY；参数错误沿 SDK 映射。
 * @note Duplex 构建拒绝此手动开始入口；异步 SpeechStart 事件才推进拾音阶段。
 */
d_err_t qwen_service_speech_start(void);

/**
 * @brief 取得 SDK 借用后提交按键对讲结束上行。
 * @return 受理或已无上行返回 D_OK；准入及 SDK 错误返回映射值。
 * @note Duplex 内部实现无操作，但外层仍要求会话准入；不表示 ASR 或本轮响应已完成。
 */
d_err_t qwen_service_speech_end(void);

/**
 * @brief 处理已由产品映射的输入，并在同步调用期间借用服务资源。
 * @param[in] key_type 兼容按键类型，当前不参与路由。
 * @param[in] key_action SDK 按键动作。
 * @param[in] key_value 产品已经匹配的按键值。
 * @return 返回 D_KEY_* 处理结果；服务不可用时 D_KEY_REJECTED，不作为继续传播。
 * @note 消费仅表示输入处理，不证明会话、WiFi 或 SDK 完成。清理期间真实 UP 可清除按下意图；所有者等借用返回后才销毁，不虚构物理 CANCEL。
 */
int qwen_service_handle_key_event(uint8_t key_type, uint8_t key_action, uint8_t key_value);

/**
 * @brief 从兼容唤醒来源受理会话启动并记录来源。
 * @param[in] source 可空诊断来源名称。
 * @return session_start 的实际结果。
 */
d_err_t qwen_service_wakeup_from_idle(const char *source);

/**
 * @brief 取得 SDK 借用后提交暂停，清本地上行意图并请求音频停止。
 * @return SDK 暂停结果或准入错误；成功为 D_OK。
 * @note 即使 SDK 失败，本地状态也按实现转回空闲；音频停止完成仍需受检清理确认。
 */
d_err_t qwen_service_speech_pause(void);

/**
 * @brief 在就绪、可文本请求且无活动轮次时提交 UTF-8 文本问题。
 * @param[in] text 非空零结尾 UTF-8 文本，调用期间保持有效。
 * @return 受理返回 D_OK；空文本返回 D_ERR_INVALID_ARG；工具或响应槽忙返回 D_ERR_BUSY；其他状态返回 D_ERR_NOT_READY 或 SDK 映射。
 * @note 只确认 SDK 受理，LLM 及 TTS 由后续事件推进。
 */
d_err_t qwen_service_question(const char *text);

/**
 * @brief 在文本准入允许时提交直接 TTS 请求。
 * @param[in] text 非空零结尾 UTF-8 文本。
 * @return 受理返回 D_OK；空文本、工具或响应槽冲突、状态及 SDK 错误返回相应值。
 * @note 不经过 LLM，不等待播报或本地排空。
 */
d_err_t qwen_service_tts(const char *text);

//*********************************************************************************//
//                              状态查询                                            //
//*********************************************************************************//

/**
 * @brief 读取当前缓存的公开服务状态。
 * @return 当前 qwen_state_e 值。
 * @note 需要可用性、清理与准入的完整快照时使用 get_snapshot。
 */
qwen_state_e qwen_service_get_state(void);

/**
 * @brief 检查内部轮次活动或公开状态是否仍属于工作阶段。
 * @return 活动轮次或非 IDLE、READY 状态返回 1，否则为 0。
 * @note ERROR 也可能返回工作中，不代表成功或资源已释放。
 */
int qwen_service_is_working(void);

/**
 * @brief 非阻塞取得维护准入，关闭自动会话并请求完整后台清理。
 * @return 首次或重复取得返回 D_OK；服务未就绪或致命状态返回 D_ERR_NOT_READY。
 * @note 占用持续到 maintenance_poll 确认后 release；显式新维护可触发清理重试。
 */
d_err_t qwen_service_maintenance_begin(void);
/**
 * @brief 检查维护清理、SDK 借用、按键事务及后台准备均已完成。
 * @param[out] ready 非空完成确认输出，先置 0。
 * @return 正常查询返回 D_OK；无占用、致命或清理失败返回相应错误；小乐仍运行可返回 D_ERR_BUSY。
 */
d_err_t qwen_service_maintenance_poll(uint8_t *ready);
/**
 * @brief 确认维护全部就绪后解除占用并安排恢复本地待机。
 * @return 解除返回 D_OK；仍未清理返回 D_ERR_BUSY；查询错误原样返回。
 * @note 不自动恢复被显式中断的会话，需用户再次开始后恢复原自动策略资格。
 */
d_err_t qwen_service_maintenance_release(void);

typedef struct {
    /** D_OK 表示受理该代次，也包括已有待处理的重复申请；
     * 资源完成确认与错误由 poll 返回。 */
    d_err_t (*begin)(uint32_t generation);
    d_err_t (*poll)(uint32_t generation, uint8_t *ready);
    void (*release)(uint32_t generation);
} qwen_audio_peer_t;
/**
 * @brief 初始化前绑定静态产品音频同行操作集。
 * @param[in] peer begin、poll、release 均有效且在服务生命周期持续存活的描述。
 * @return 有效首次或相同绑定返回 D_OK；参数不完整返回 D_ERR_INVALID_ARG；已初始化或不同同行返回 D_ERR_BUSY。
 */
d_err_t qwen_service_bind_audio_peer(const qwen_audio_peer_t *peer);
/**
 * @brief 在外部音量借用计数保护下同步设置 Qwen 播放音量。
 * @param[in] percent 目标音量，音频层截断到 0～100。
 * @return 成功返回 D_OK；正在解除或音频未初始化返回 D_ERR_NOT_READY；硬件失败返回 D_ERR_IO。
 * @note 用于产品统一音量后端；借用归还前音频对象不能被销毁。
 */
d_err_t qwen_service_apply_device_volume(uint8_t percent);
/**
 * @brief 请求后台完整清理并限时等候按键事务与清理结束。
 * @return 清理完成返回 D_OK；致命故障返回 D_ERR_IO；超过关机内部期限返回 D_ERR_TIMEOUT；无电源控制构建返回 D_OK。
 * @note 只由后台关机编排调用，不在 UI 或服务工作任务等待自身。
 */
d_err_t qwen_service_shutdown_wait(void);
/**
 * @brief 为非零闹铃票据取得音频交接占用，停止自动会话并请求后台清理。
 * @param[in] token 按环形代次递增的非零闹铃占用票据。
 * @return 受理或相同票据重复返回 D_OK；参数、就绪、致命或旧票据冲突返回对应错误。
 * @note 仅受理，不表示麦克风或播放器已静音；继续 alarm_poll。
 */
d_err_t qwen_service_alarm_begin(uint32_t token);
/**
 * @brief 核对闹铃票据并检查音频会话清理及 SDK、按键借用已归还。
 * @param[in] token 申请时的非零闹铃票据。
 * @param[out] ready 非空完成确认输出，先置 0。
 * @return 正常查询返回 D_OK；参数、旧票据、未就绪或清理故障返回对应错误。
 * @note 完成条件满足但小乐仍报告运行时返回 D_ERR_IO，不宣称交接完成。
 */
d_err_t qwen_service_alarm_poll(uint32_t token, uint8_t *ready);
/**
 * @brief 只解除匹配闹铃票据并唤醒后台恢复待机资格。
 * @param[in] token 要解除的非零占用票据，过期值忽略。
 * @note 调用者先确认闹铃自身音频与音量已收尾；本入口不自动重开对话。
 */
void qwen_service_alarm_release(uint32_t token);

#endif /* QWEN_SERVICE_H */
