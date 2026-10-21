/** @file product_ui_ai_chat_controller.c @brief No LVGL calls or network work. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_ai_chat_controller.h"
#include <string.h>

void product_ui_ai_chat_describe(const qwen_service_snapshot_t *snapshot,
                                 product_ui_ai_chat_controls_t *controls)
{
    if (controls == NULL) return;
    memset(controls, 0, sizeof(*controls));
    controls->status = "AI服务未就绪";
    controls->button = "开始对话";
    if (snapshot == NULL) return;
    controls->service = *snapshot;
    controls->enabled = snapshot->session.can_start;
    controls->action = controls->enabled ? PRODUCT_AI_ACTION_START : PRODUCT_AI_ACTION_NONE;
    switch (snapshot->session.phase) {
    case QWEN_SESSION_OFFLINE:
        controls->status = "请先连接WiFi";
        break;
    case QWEN_SESSION_PREPARING:
        controls->status = "正在准备鉴权";
        break;
    case QWEN_SESSION_READY:
        controls->status = "已就绪，等待开始";
        break;
    case QWEN_SESSION_STARTING:
        controls->status = "正在启动对话";
        controls->button = "取消";
        controls->action = PRODUCT_AI_ACTION_STOP;
        controls->enabled = snapshot->session.can_stop;
        break;
    case QWEN_SESSION_ACTIVE:
        controls->status = snapshot->state == QWEN_STATE_LISTENING ? "正在聆听" :
            snapshot->state == QWEN_STATE_THINKING ? "正在思考" :
            snapshot->state == QWEN_STATE_PLAYING ? "正在播报" : "对话已连接";
        controls->button = "结束对话";
        controls->action = PRODUCT_AI_ACTION_STOP;
        controls->enabled = snapshot->session.can_stop;
        break;
    case QWEN_SESSION_STOPPING:
        controls->status = "正在释放对话资源";
        controls->button = "结束中";
        controls->enabled = 0U;
        controls->action = PRODUCT_AI_ACTION_NONE;
        break;
    case QWEN_SESSION_ERROR:
        controls->status = snapshot->session.fatal ? "服务异常，请重启" :
            snapshot->session.can_stop ? "停止尚未完成" : "连接准备失败";
        controls->button = snapshot->session.can_stop ? "重试结束" : "重试";
        controls->action = snapshot->session.can_stop ? PRODUCT_AI_ACTION_STOP :
            snapshot->session.can_start ? PRODUCT_AI_ACTION_START : PRODUCT_AI_ACTION_NONE;
        controls->enabled = controls->action != PRODUCT_AI_ACTION_NONE;
        break;
    default:
        controls->enabled = 0U;
        controls->action = PRODUCT_AI_ACTION_NONE;
        break;
    }
}

void product_ui_ai_chat_controls_read(product_ui_ai_chat_controls_t *controls)
{
    qwen_service_snapshot_t snapshot = {0};
#if D_FEATURE_QWEN
    (void)qwen_service_get_snapshot(&snapshot);
#endif
    product_ui_ai_chat_describe(&snapshot, controls);
}

d_err_t product_ui_ai_chat_activate(product_ai_action_e action)
{
#if D_FEATURE_QWEN
    if (action == PRODUCT_AI_ACTION_START) return qwen_service_session_start();
    if (action == PRODUCT_AI_ACTION_STOP) return qwen_service_session_stop();
#else
    (void)action;
#endif
    return D_ERR_NOT_READY;
}

void product_ui_ai_chat_render_begin(product_ui_ai_chat_render_state_t *state,
                                    uint32_t revision)
{
    if (state == NULL) return;
    state->revision = revision;
    state->pending = 1U;
}

void product_ui_ai_chat_render_finish(product_ui_ai_chat_render_state_t *state,
                                     uint8_t success)
{
    if (state != NULL && success) state->pending = 0U;
}
#endif
