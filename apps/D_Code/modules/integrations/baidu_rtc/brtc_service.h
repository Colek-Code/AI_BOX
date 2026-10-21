/**
 * @file    brtc_service.h
 * @brief   BRTC 百度 RTC 智能体服务 — D_Code 模块封装
 * @note    封装 BRTC 模块(libbrtc.a)，提供 D_Code 事件总线集成接口
 *          生命周期：init → start(WiFi就绪后) → [对话中] → stop → deinit
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
//                              API                                                //
//*********************************************************************************//

/**
 * @brief 初始化 BRTC 服务（注册事件订阅，不连接）
 * @note  在 app_main Phase 4 中调用
 * @return D_OK 成功
 */
d_err_t brtc_service_init(void);

/**
 * @brief 启动 BRTC 智能体（HTTP 换 token → 创建引擎 → 登录 → 呼叫）
 * @note  需要 WiFi 已连接；会创建后台任务异步执行
 * @return D_OK 成功, D_ERR_BUSY 已在运行
 */
d_err_t brtc_service_start(void);

/**
 * @brief 停止 BRTC 智能体（销毁引擎，释放音频资源）
 * @return D_OK 成功
 */
d_err_t brtc_service_stop(void);

/**
 * @brief 发送文本到 AI 智能体
 * @param text 文本消息
 */
void brtc_service_send_text(const char *text);

/**
 * @brief 发送文本到 TTS 直接播报
 * @param text 文本内容
 */
void brtc_service_send_tts(const char *text);

/**
 * @brief 打断当前对话
 */
void brtc_service_interrupt(void);

/**
 * @brief 查询服务是否正在运行
 * @return true=运行中
 */
bool brtc_service_is_running(void);

//*********************************************************************************//
//                   扩展 API                                                //
//*********************************************************************************//

/**
 * @brief 查询是否可以安全退出（监控任务运行中为 true）
 */
bool app_brtc_agent_can_exit(void);

/**
 * @brief 查询 TTS 是否正在播放
 */
bool brtc_aiagent_tts_busy(void);

/**
 * @brief 设置/获取音频设备独占标识
 */
void brtc_audio_device_need_used_set(bool used);
bool brtc_audio_device_need_used_get(void);

/**
 * @brief 快速关闭
 * @param quickly 是否快速退出（跳过等待提示音）
 */
void app_sdk_should_close(int quickly);

/**
 * @brief 切换云端 ASR 长文本录音模式
 * @param enable true=进入长文本模式, false=退出长文本模式
 */
void app_brtc_agent_longtext_mode(bool enable);

/**
 * @brief 控制 ASR 实时模式
 * @param enable true=启用, false=禁用
 */
void app_brtc_agent_asr_realtime(bool enable);

#endif /* BRTC_SERVICE_H */
