/**
 * @file    ai_view.h
 * @brief   AI Chat View 接口（纯 LVGL 操作）
 * @note    ❌ 禁止业务逻辑；❌ 禁止网络调用；❌ 禁止事件发布
 */

#ifndef AI_VIEW_H
#define AI_VIEW_H

#include "lvgl.h"

/**
 * @brief 创建 AI Chat 界面（表情 + 聊天 UI）
 */
void ai_view_create(lv_obj_t *parent);

/**
 * @brief 添加用户消息气泡
 */
void ai_view_add_user_msg(const char *text);

/**
 * @brief 添加 AI 消息气泡
 */
void ai_view_add_ai_msg(const char *text);

/**
 * @brief 显示/隐藏 "正在输入" 动画
 */
void ai_view_show_typing(int show);

/**
 * @brief 设置表情
 */
void ai_view_set_emotion(int emotion);

/**
 * @brief 开始说话动画
 */
void ai_view_start_speaking(void);

/**
 * @brief 设置嘴巴状态（思考中）
 */
void ai_view_set_thinking(void);

/**
 * @brief 设置嘴巴空闲
 */
void ai_view_set_idle(void);

/**
 * @brief 更新表情动画（每帧调用）
 */
void ai_view_update(uint32_t elapsed_ms);

/**
 * @brief 绑定麦克风按钮回调
 */
void ai_view_set_mic_callback(lv_event_cb_t cb, void *user_data);

/**
 * @brief 获取麦克风按钮对象
 */
lv_obj_t *ai_view_get_mic_btn(void);

#endif /* AI_VIEW_H */
