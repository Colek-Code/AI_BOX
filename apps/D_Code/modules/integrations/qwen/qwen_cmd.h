/**
 * @file    qwen_cmd.h
 * @brief   Qwen 业务命令解析接口
 * @author  D_Code
 * @date    2026-05-23
 */

#ifndef __QWEN_CMD_H__
#define __QWEN_CMD_H__

#include "framework/d_error.h"

#include <stdint.h>

#define QWEN_CMD_TEXT_LEN 64U

typedef enum {
    QWEN_CMD_DOMAIN_UNKNOWN = 0,
    QWEN_CMD_DOMAIN_VOLUME,
    QWEN_CMD_DOMAIN_APPLICATION,
    QWEN_CMD_DOMAIN_VOICE_TRANSLATE,
} qwen_cmd_domain_e;

typedef struct {
    qwen_cmd_domain_e domain;
    uint32_t command_event;
    uint32_t value;
    uint32_t category;
    char text[QWEN_CMD_TEXT_LEN];
} qwen_cmd_msg_t;

/**
 * @brief 注册 Qwen SDK 业务命令回调
 * @return D_OK 表示成功，其他错误码表示失败
 */
d_err_t qwen_cmd_register_handlers(void);

/**
 * @brief 处理 SDK 音量命令回调
 * @param event 命令事件
 * @param params 命令参数
 * @return SDK 回调结果
 */
int32_t qwen_cmd_handle_volume(uint32_t event, void *params);

/**
 * @brief 处理 SDK 应用命令回调
 * @param event 命令事件
 * @param params 命令参数
 * @return SDK 回调结果
 */
int32_t qwen_cmd_handle_application(uint32_t event, void *params);

/**
 * @brief 处理 SDK 语音翻译命令回调
 * @param event 命令事件
 * @param params 命令参数
 * @return SDK 回调结果
 */
int32_t qwen_cmd_handle_voice_translate(uint32_t event, void *params);

#endif /* __QWEN_CMD_H__ */
