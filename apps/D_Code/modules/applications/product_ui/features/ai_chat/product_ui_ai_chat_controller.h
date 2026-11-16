/** @file product_ui_ai_chat_controller.h @brief AI 会话的纯状态投影及可重试字幕呈现合同，不执行 LVGL 或网络工作。 */
#ifndef PRODUCT_UI_AI_CHAT_CONTROLLER_H
#define PRODUCT_UI_AI_CHAT_CONTROLLER_H
#include "modules/integrations/qwen/qwen_service.h"

typedef enum {
    PRODUCT_AI_ACTION_NONE = 0,
    PRODUCT_AI_ACTION_START,
    PRODUCT_AI_ACTION_STOP,
} product_ai_action_e;

typedef struct {
    qwen_service_snapshot_t service;
    const char *status;
    const char *button;
    product_ai_action_e action;
    uint8_t enabled;
} product_ui_ai_chat_controls_t;

typedef struct {
    uint32_t revision;
    uint8_t pending;
} product_ui_ai_chat_render_state_t;

/**
 * @brief 读取可用的 Qwen 快照并生成当前操作投影。
 * @param[out] controls 输出控制状态，NULL 时不写入。
 * @note 功能关闭或快照不可用时由零初始化快照得到不可启动投影。
 */
void product_ui_ai_chat_controls_read(product_ui_ai_chat_controls_t *controls);
/**
 * @brief 把服务快照映射为静态状态文字、按钮、允许操作和使能状态。
 * @param[in] snapshot 本次借用的服务快照；NULL 表示默认未就绪。
 * @param[out] controls 投影输出，NULL 时忽略。
 * @note 复制服务值快照；文字指向静态字符串，不转移资源所有权。
 */
void product_ui_ai_chat_describe(const qwen_service_snapshot_t *snapshot,
                                 product_ui_ai_chat_controls_t *controls);
/**
 * @brief 把显式开始或停止操作交给 Qwen 会话服务。
 * @param[in] action 当前控制投影允许的操作。
 * @return START/STOP 原样返回相应服务结果；无有效操作或功能关闭返回 D_ERR_NOT_READY。
 * @note 成功含义遵循会话服务受理合同，不代表后台启动或释放已经完成。
 */
d_err_t product_ui_ai_chat_activate(product_ai_action_e action);
/**
 * @brief 记录待呈现字幕代次并置位待重试状态。
 * @param[in,out] state 页面持有的呈现状态，NULL 时忽略。
 * @param[in] revision 本次字幕内容代次。
 */
void product_ui_ai_chat_render_begin(product_ui_ai_chat_render_state_t *state,
                                    uint32_t revision);
/**
 * @brief 仅在完整呈现成功后清除待处理状态。
 * @param[in,out] state 页面持有的呈现状态，可为 NULL。
 * @param[in] success 非零表示成功，零保留 pending 供下轮重试。
 */
void product_ui_ai_chat_render_finish(product_ui_ai_chat_render_state_t *state,
                                     uint8_t success);
#endif
