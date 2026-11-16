/**
 * @file    brtc_service.h
 * @brief   BRTC 百度智能体的事件桥接与异步启动、停止接口。
 * @note    封装 BRTC 模块(libbrtc.a)，提供 D_Code 事件总线集成接口
 *          生命周期：init → start（WiFi 就绪后）→ 对话 → stop 请求及后台收尾
 */

#ifndef BRTC_SERVICE_H
#define BRTC_SERVICE_H

#include "framework/d_error.h"
#include <stdbool.h>
#include <stdint.h>

//*********************************************************************************//
//                              事件数据结构（总线投递用）                          //
//*********************************************************************************//

/* ASR 字幕数据 */
typedef struct {
    char    text[120];      /* 字幕文本（截断到事件总线限制） */
    bool    is_final;       /* 是否为最终结果 */
} brtc_asr_data_t;

/* FunctionCall 数据 */
typedef struct {
    char    id[64];         /* 调用 ID */
    char    params[56];     /* JSON 参数（截断，完整数据在回调日志） */
} brtc_func_call_t;

/* 播放控制数据 */
typedef struct {
    char    path[120];      /* 音频 URL（截断保护） */
    bool    start;          /* true=开始, false=停止 */
} brtc_player_op_t;

//*********************************************************************************//
//                              公共接口                                           //
//*********************************************************************************//

/**
 * @brief 清空上下文并订阅 WiFi 事件，不直接连接智能体。
 * @return 订阅成功返回 D_OK，失败撤销已订阅部分并返回 D_ERR_IO。
 * @note 在服务启动阶段串行调用，不保证重复初始化活动服务安全。
 */
d_err_t brtc_service_init(void);

/**
 * @brief 在空闲状态创建后台 HTTP 换令牌及引擎启动流程。
 * @return 任务创建成功返回 D_OK；状态非空闲返回 D_ERR_BUSY；创建失败返回 D_ERR_IO。
 * @note 调用方应先确认 WiFi 就绪；D_OK 仅表示任务创建，实际媒体就绪由事件确认。
 */
d_err_t brtc_service_start(void);

/**
 * @brief 请求监控任务停止智能体与相关后台资源。
 * @return 当前固定返回 D_OK，包括原本空闲。
 * @note 异步停止意图，不确认引擎销毁、线程退出或服务端实例停止完成。
 */
d_err_t brtc_service_stop(void);

/**
 * @brief 在引擎运行阶段向智能体提交文本。
 * @param[in] text SDK 使用的非空文本，调用期间保持有效。
 * @note 其他阶段无操作；本封装不反馈发送结果。
 */
void brtc_service_send_text(const char *text);

/**
 * @brief 在引擎运行阶段提交直接 TTS 播报文本。
 * @param[in] text SDK 使用的非空文本，调用期间保持有效。
 * @note 其他阶段无操作，不等待播报完成。
 */
void brtc_service_send_tts(const char *text);

/**
 * @brief 引擎运行时提交打断请求。
 */
void brtc_service_interrupt(void);

/**
 * @brief 按当前阶段判断是否已进入呼叫或后续阶段。
 * @return state 大于等于 CALLING 返回 true，否则 false。
 * @note STOPPING 也符合该比较，不能作为停止完成确认。
 */
bool brtc_service_is_running(void);

//*********************************************************************************//
//                   扩展 API                                                //
//*********************************************************************************//

/**
 * @brief 读取监控任务发布的可响应退出标记。
 * @return 监控任务运行阶段为 true，否则 false。
 * @note 该标记不是引擎或音频资源已释放的证明。
 */
bool app_brtc_agent_can_exit(void);

/**
 * @brief 读取 SDK 回调维护的 AI TTS 播放标记。
 * @return SDK 报告正在说话返回 true。
 */
bool brtc_aiagent_tts_busy(void);

/**
 * @brief 设置 RTC 与外部音乐共用设备的协作标记。
 * @param[in] used true 表示需要在 TTS 开始和结束时交接。
 */
void brtc_audio_device_need_used_set(bool used);
/**
 * @brief 读取音频设备交接协作标记。
 * @return 当前设置值。
 */
bool brtc_audio_device_need_used_get(void);

/**
 * @brief 记录快速退出选择并标记网络离线、异常退出，同时请求音乐停止。
 * @param[in] quickly 非零省略部分退出提示等待。
 * @note 实际引擎清理由监控任务推进，本函数不等待。
 */
void app_sdk_should_close(int quickly);

/**
 * @brief 先禁用实时 ASR，再切换云端长文本录音状态。
 * @param[in] enable true 开始长文本录音，false 停止。
 * @note 启用时在两个 SDK 事件间延时 20 个节拍；退出长文本不会自动恢复实时模式。
 */
void app_brtc_agent_longtext_mode(bool enable);

/**
 * @brief 存在引擎时提交实时 ASR 启用或禁用事件。
 * @param[in] enable true 启用，false 禁用。
 */
void app_brtc_agent_asr_realtime(bool enable);

#endif /* BRTC_SERVICE_H */
