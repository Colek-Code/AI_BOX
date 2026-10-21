/** @file product_ui_ai_chat_controller.h @brief Session projection and render retry contract. */
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

void product_ui_ai_chat_controls_read(product_ui_ai_chat_controls_t *controls);
void product_ui_ai_chat_describe(const qwen_service_snapshot_t *snapshot,
                                 product_ui_ai_chat_controls_t *controls);
d_err_t product_ui_ai_chat_activate(product_ai_action_e action);
void product_ui_ai_chat_render_begin(product_ui_ai_chat_render_state_t *state,
                                    uint32_t revision);
void product_ui_ai_chat_render_finish(product_ui_ai_chat_render_state_t *state,
                                     uint8_t success);
#endif
