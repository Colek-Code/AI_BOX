/**
 * @file    qwen_cmd.h
 * @brief   SDK 业务命令到 D_Code 值事件与音量请求的解析接口。
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
 * @brief 分别注册音量、应用、翻译及通用 change 回调。
 * @return 全部成功返回 D_OK；其余返回首次注册错误映射，后续注册仍继续尝试。
 */
d_err_t qwen_cmd_register_handlers(void);

/**
 * @brief 解析 SDK 音量参数，提交统一音量请求并发布命令值事件。
 * @param[in] event SDK 音量命令事件。
 * @param[in] params 可空 c_mm_volume_param_t 借用参数。
 * @return 当前固定返回 UTIL_SUCCESS，即使命令被忽略或请求失败。
 * @note 音量受理与完成分离；事件中的 action 仅记录受理结论。
 */
int32_t qwen_cmd_handle_volume(uint32_t event, void *params);

/**
 * @brief 解析 SDK 应用或设置名称并发布待上层处理的命令。
 * @param[in] event SDK 应用命令事件。
 * @param[in] params 按事件对应的可空应用或设置参数。
 * @return 固定返回 UTIL_SUCCESS。
 * @note 本函数不启动或关闭实际应用。
 */
int32_t qwen_cmd_handle_application(uint32_t event, void *params);

/**
 * @brief 把 SDK 翻译开始或结束转换为业务命令值事件。
 * @param[in] event SDK 翻译命令事件。
 * @param[in] params 兼容参数，本实现不使用。
 * @return 固定返回 UTIL_SUCCESS。
 */
int32_t qwen_cmd_handle_voice_translate(uint32_t event, void *params);

#endif /* __QWEN_CMD_H__ */
