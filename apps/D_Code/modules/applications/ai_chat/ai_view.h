/**
 * @file    ai_view.h
 * @brief   AI Chat View 接口（纯 LVGL 操作）
 * @note    ❌ 禁止业务逻辑；❌ 禁止网络调用；❌ 禁止事件发布
 */

#ifndef AI_VIEW_H
#define AI_VIEW_H

#include "lvgl.h"

/**
 * @brief 在有效父容器创建表情与聊天区域并追加欢迎消息。
 * @param[in,out] parent 非空且由页面管理器持有的父对象。
 * @note 仅在 LVGL 线程调用，当前 void 接口不报告局部创建失败。
 */
void ai_view_create(lv_obj_t *parent);

/**
 * @brief 向聊天区追加用户侧气泡。
 * @param[in] text 调用期间借用的消息文字，由标签复制；空或空串被聊天组件忽略。
 */
void ai_view_add_user_msg(const char *text);

/**
 * @brief 向聊天区追加 AI 侧气泡。
 * @param[in] text 调用期间借用的消息文字，由标签复制；空或空串忽略。
 */
void ai_view_add_ai_msg(const char *text);

/**
 * @brief 显示或移除本地正在输入指示气泡。
 * @param[in] show 非零显示，零隐藏；须先完成聊天区域创建。
 */
void ai_view_show_typing(int show);

/**
 * @brief 把业务情绪编号交给表情引擎投影。
 * @param[in] emotion 与 face_emotion_t 兼容的合法情绪编号。
 */
void ai_view_set_emotion(int emotion);

/**
 * @brief 触发表情引擎的有限说话动画，不驱动真实音频播放。
 */
void ai_view_start_speaking(void);

/**
 * @brief 设置中性情绪及思考嘴巴动画。
 */
void ai_view_set_thinking(void);

/**
 * @brief 仅把嘴巴动画恢复空闲，不改网络或服务状态。
 */
void ai_view_set_idle(void);

/**
 * @brief 在 LVGL 页面周期推进表情动画。
 * @param[in] elapsed_ms 本轮经过的毫秒数。
 */
void ai_view_update(uint32_t elapsed_ms);

/**
 * @brief 在已有麦克风按钮追加点击回调。
 * @param[in] cb 调用方持有的有效事件回调。
 * @param[in] user_data 回调借用上下文，须覆盖按钮寿命。
 * @note 按钮不存在时不操作；此函数追加监听，不替换或排空先前回调。
 */
void ai_view_set_mic_callback(lv_event_cb_t cb, void *user_data);

/**
 * @brief 借用聊天组件当前麦克风按钮。
 * @return 按钮对象指针；尚未创建时为 NULL，不由调用方释放。
 */
lv_obj_t *ai_view_get_mic_btn(void);

#endif /* AI_VIEW_H */
