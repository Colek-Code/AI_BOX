/** @file product_ui_ai_chat_view.h @brief Bounded, retryable sentence view. */
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

int product_ui_ai_chat_view_create(product_ui_ai_chat_view_t *view, lv_obj_t *parent);
int product_ui_ai_chat_view_render(product_ui_ai_chat_view_t *view,
                                   const qwen_subtitle_render_t *render);
#endif
