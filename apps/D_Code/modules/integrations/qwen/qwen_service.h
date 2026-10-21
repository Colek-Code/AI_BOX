/**
 * @file    qwen_service.h
 * @brief   Qwen 多模态语音对话服务（License 模式）
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
} qwen_service_snapshot_t;

typedef enum {
    QWEN_VOLUME_SET,
    QWEN_VOLUME_ADJUST,
    QWEN_VOLUME_MUTE,
} qwen_volume_operation_e;

/** Admit a cloud volume command for the current session; never waits for audio. */
d_err_t qwen_service_request_volume(qwen_volume_operation_e operation,
                                    int16_t value, uint32_t *request_id);

/**
 * @brief 非阻塞提交启动请求，重复开始保持幂等。
 * @return D_OK 表示已接受请求；连接是否完成须读取服务快照。
 * @note 可在 LVGL 回调或普通任务调用；鉴权、麦克风交接和建链由后台执行。
 */
d_err_t qwen_service_session_start(void);

/**
 * @brief 非阻塞撤销启动请求并请求停止，重复停止保持幂等。
 * @return D_OK 表示请求已接受或已经停止，不证明资源已释放。
 * @note 通过快照区分清理中、清理失败和完成；清理未完成时不能再次开始。
 */
d_err_t qwen_service_session_stop(void);

/**
 * @brief 复制服务缓存状态，不调用网络或 SDK 接口。
 * @param snapshot 调用方提供的输出对象。
 * @return D_OK / D_ERR_NOT_READY / D_ERR_INVALID_ARG。
 * @note 可在 LVGL 与事件订阅回调读取；服务未就绪时输出为全零。
 */
d_err_t qwen_service_get_snapshot(qwen_service_snapshot_t *snapshot);

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
 * @brief 初始化 Qwen 服务（创建任务、订阅 WiFi 事件）
 * @return D_OK / D_ERR_BUSY
 */
d_err_t qwen_service_init(void);

/**
 * @brief 反初始化 Qwen 服务（断开 WSS、释放资源）
 * @note 仅由服务生命周期管理方在后台调用。清理失败时保留可能被引用的
 *       对象并打印错误；此 void 兼容接口不提供“已彻底销毁”的返回证明。
 */
void qwen_service_deinit(void);

//*********************************************************************************//
//                              对话控制 API                                        //
//*********************************************************************************//

/**
 * @brief 开始语音上行（按下说话键）
 * @return D_OK / D_ERR_BUSY / D_ERR_NOT_READY
 */
d_err_t qwen_service_speech_start(void);

/**
 * @brief 结束语音上行（松开说话键，Push2Talk 模式）
 * @return D_OK / D_ERR_NOT_READY
 */
d_err_t qwen_service_speech_end(void);

int qwen_service_handle_key_event(uint8_t key_type, uint8_t key_action, uint8_t key_value);

d_err_t qwen_service_wakeup_from_idle(const char *source);

/**
 * @brief 暂停/打断当前对话（进入等待状态）
 * @return D_OK / D_ERR_NOT_READY
 */
d_err_t qwen_service_speech_pause(void);

/**
 * @brief 发送文本问题（跳过 ASR，直接问 LLM）
 * @param text UTF-8 文本
 * @return D_OK / D_ERR_NOT_READY / D_ERR_INVALID_ARG
 */
d_err_t qwen_service_question(const char *text);

/**
 * @brief 文本转语音（不经过 LLM，直接 TTS）
 * @param text UTF-8 文本
 * @return D_OK / D_ERR_NOT_READY / D_ERR_INVALID_ARG
 */
d_err_t qwen_service_tts(const char *text);

//*********************************************************************************//
//                              状态查询                                            //
//*********************************************************************************//

/**
 * @brief 获取当前服务状态
 */
qwen_state_e qwen_service_get_state(void);

/**
 * @brief 查询 SDK 是否正在工作
 * @return 1=工作中, 0=空闲
 */
int qwen_service_is_working(void);

/** OTA background owner: hold persists until full cleanup is confirmed. */
d_err_t qwen_service_maintenance_begin(void);
d_err_t qwen_service_maintenance_poll(uint8_t *ready);
d_err_t qwen_service_maintenance_release(void);

#endif /* QWEN_SERVICE_H */
