/**
 * @file    ai_view.c
 * @brief   AI Chat View — 纯 LVGL 渲染层
 * @note    封装 d_face_engine + d_chat_ui，不含任何业务逻辑
 *          ❌ 禁止：d_event_publish / d_event_subscribe / HTTP 请求
 */

#include "config/app_config.h"

#if D_FEATURE_AI_CHAT

#include "ai_view.h"
#include "d_face_engine.h"
#include "d_chat_ui.h"

//*********************************************************************************//
//                              View 实现（委托给现有子组件）                       //
//*********************************************************************************//

void ai_view_create(lv_obj_t *parent)
{
    /* 设置背景色 */
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* 创建表情引擎（屏幕上部） */
    face_engine_create(parent);

    /* 创建聊天 UI（表情下方） */
    chat_ui_create(parent);

    /* 欢迎消息 */
    chat_ui_add_msg(0, "\xe4\xbd\xa0\xe5\xa5\xbd! \xe7\x82\xb9\xe5\x87\xbb\xe4\xb8\x8b\xe6\x96\xb9\xe6\x8c\x89\xe9\x92\xae\xe5\xbc\x80\xe5\xa7\x8b\xe5\xaf\xb9\xe8\xaf\x9d");
}

void ai_view_add_user_msg(const char *text)
{
    chat_ui_add_msg(1, text);
}

void ai_view_add_ai_msg(const char *text)
{
    chat_ui_add_msg(0, text);
}

void ai_view_show_typing(int show)
{
    chat_ui_show_typing(show);
}

void ai_view_set_emotion(int emotion)
{
    face_engine_set_emotion((face_emotion_t)emotion);
}

void ai_view_start_speaking(void)
{
    face_engine_start_speaking();
}

void ai_view_set_thinking(void)
{
    face_engine_set_emotion(FACE_EMOTION_NEUTRAL);
    face_engine_set_mouth_state(MOUTH_STATE_THINKING);
}

void ai_view_set_idle(void)
{
    face_engine_set_mouth_state(MOUTH_STATE_IDLE);
}

void ai_view_update(uint32_t elapsed_ms)
{
    face_engine_update(elapsed_ms);
}

void ai_view_set_mic_callback(lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *mic_btn = chat_ui_get_mic_btn();
    if (mic_btn) {
        lv_obj_add_event_cb(mic_btn, cb, LV_EVENT_CLICKED, user_data);
    }
}

lv_obj_t *ai_view_get_mic_btn(void)
{
    return chat_ui_get_mic_btn();
}

#endif /* D_FEATURE_AI_CHAT */
