/** @file product_ui_ai_chat_view.h @brief 有限句子字幕视图，文本由页面持有，呈现失败可重试。 */
#ifndef PRODUCT_UI_AI_CHAT_VIEW_H
#define PRODUCT_UI_AI_CHAT_VIEW_H
#include "modules/applications/qwen_subtitle/qwen_subtitle_text.h"

typedef struct {
    lv_obj_t *label;
    char text[QWEN_SUBTITLE_SENTENCE_TEXT_MAX];
    uint8_t current;
} product_ui_ai_chat_row_t;

typedef struct {
    lv_obj_t *panel;
    const lv_font_t *font;
    product_ui_ai_chat_row_t rows[QWEN_SUBTITLE_SENTENCE_MAX];
    uint8_t auto_follow;
    uint8_t programmatic;
    uint8_t user_scrolling;
} product_ui_ai_chat_view_t;

/**
 * @brief 创建可竖向滚动的字幕面板并借用页面状态作为事件上下文。
 * @param[out] view 调用方持有的视图状态，须存活至面板及其事件回调结束。
 * @param[in,out] parent 拥有新面板的父对象。
 * @return 成功返回 0；空参数或面板创建失败返回 -1。
 * @note 行标签按需创建；本函数不删除父对象。
 */
int product_ui_ai_chat_view_create(product_ui_ai_chat_view_t *view, lv_obj_t *parent);
/**
 * @brief 按需创建行标签、复制句子并更新高亮及自动跟随。
 * @param[in,out] view 已创建且由页面持有的视图。
 * @param[in] render 本次借用的句子快照，内容复制到行缓冲区。
 * @return 全部处理成功返回 0；参数无效、句子数越界或标签创建失败返回 -1。
 * @note 失败保留已创建行及已写入内容，调用方保留 pending 再试；用户滚动时不强制跟随，末行作为当前句高亮。
 */
int product_ui_ai_chat_view_render(product_ui_ai_chat_view_t *view,
                                   const qwen_subtitle_render_t *render);
#endif
