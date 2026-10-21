/**
 * @file product_ui_calculator_model.h
 * @brief Static calculator input and operation state.
 */

#ifndef __PRODUCT_UI_CALCULATOR_MODEL_H__
#define __PRODUCT_UI_CALCULATOR_MODEL_H__

#include "modules/applications/product_ui/features/calculator/product_ui_calculator_engine.h"
#include <stdint.h>

#define PRODUCT_UI_CALCULATOR_DISPLAY_CAP    24
#define PRODUCT_UI_CALCULATOR_EXPRESSION_CAP 48

typedef enum {
    PRODUCT_UI_CALCULATOR_KEY_0,
    PRODUCT_UI_CALCULATOR_KEY_1,
    PRODUCT_UI_CALCULATOR_KEY_2,
    PRODUCT_UI_CALCULATOR_KEY_3,
    PRODUCT_UI_CALCULATOR_KEY_4,
    PRODUCT_UI_CALCULATOR_KEY_5,
    PRODUCT_UI_CALCULATOR_KEY_6,
    PRODUCT_UI_CALCULATOR_KEY_7,
    PRODUCT_UI_CALCULATOR_KEY_8,
    PRODUCT_UI_CALCULATOR_KEY_9,
    PRODUCT_UI_CALCULATOR_KEY_DECIMAL,
    PRODUCT_UI_CALCULATOR_KEY_ADD,
    PRODUCT_UI_CALCULATOR_KEY_SUBTRACT,
    PRODUCT_UI_CALCULATOR_KEY_MULTIPLY,
    PRODUCT_UI_CALCULATOR_KEY_DIVIDE,
    PRODUCT_UI_CALCULATOR_KEY_EQUALS,
    PRODUCT_UI_CALCULATOR_KEY_CLEAR,
    PRODUCT_UI_CALCULATOR_KEY_SIGN,
    PRODUCT_UI_CALCULATOR_KEY_PERCENT,
    PRODUCT_UI_CALCULATOR_KEY_BACKSPACE,
} product_ui_calculator_key_e;

typedef enum {
    PRODUCT_UI_CALCULATOR_STATE_ENTRY,
    PRODUCT_UI_CALCULATOR_STATE_PENDING,
    PRODUCT_UI_CALCULATOR_STATE_RESULT,
    PRODUCT_UI_CALCULATOR_STATE_ERROR,
} product_ui_calculator_state_e;

typedef struct {
    product_ui_calculator_value_t accumulator;
    product_ui_calculator_value_t repeat_operand;
    product_ui_calculator_operator_e pending_operation;
    product_ui_calculator_operator_e repeat_operation;
    product_ui_calculator_status_e status;
    product_ui_calculator_state_e state;
    char entry[PRODUCT_UI_CALCULATOR_DISPLAY_CAP];
    char display[PRODUCT_UI_CALCULATOR_DISPLAY_CAP];
    char expression[PRODUCT_UI_CALCULATOR_EXPRESSION_CAP];
} product_ui_calculator_model_t;

void product_ui_calculator_model_init(product_ui_calculator_model_t *model);
product_ui_calculator_status_e product_ui_calculator_model_press(
    product_ui_calculator_model_t *model,
    product_ui_calculator_key_e key);
const char *product_ui_calculator_model_display(
    const product_ui_calculator_model_t *model);
const char *product_ui_calculator_model_expression(
    const product_ui_calculator_model_t *model);

#endif /* __PRODUCT_UI_CALCULATOR_MODEL_H__ */
