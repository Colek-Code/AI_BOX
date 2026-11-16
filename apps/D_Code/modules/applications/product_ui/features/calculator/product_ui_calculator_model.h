/** @file product_ui_calculator_model.h @brief 有限输入、连续运算及重复等号的串行计算器模型。 */

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

/**
 * @brief 清空运算、重复等号和错误状态，以零开始新的输入。
 * @param[out] model 调用方持有的模型；NULL 时忽略。
 */
void product_ui_calculator_model_init(product_ui_calculator_model_t *model);
/**
 * @brief 处理一个逻辑按键并推进输入、连续运算、结果或错误状态。
 * @param[in,out] model 调用方持有并已初始化的模型；NULL 返回 FORMAT。
 * @param[in] key 逻辑键枚举。
 * @return 成功返回 OK；格式及算术失败返回对应状态；错误态仅清除、数字或小数点可重新开始。
 * @note 模型为串行 RAM 状态，无锁与持久化；显示及表达式更新均写入模型自己的固定缓冲区。
 */
product_ui_calculator_status_e product_ui_calculator_model_press(
    product_ui_calculator_model_t *model,
    product_ui_calculator_key_e key);
/**
 * @brief 借用模型当前显示文字。
 * @param[in] model 借用的模型状态。
 * @return 模型非空时返回其内部缓冲；NULL 时返回静态空串。
 * @note 指针随模型存活，文字会在后续按键操作时变化。
 */
const char *product_ui_calculator_model_display(
    const product_ui_calculator_model_t *model);
/**
 * @brief 借用模型当前表达式文字。
 * @param[in] model 借用的模型状态。
 * @return 模型非空时返回其内部缓冲；NULL 时返回静态空串。
 * @note 不转移所有权，后续模型修改可更新此缓冲区。
 */
const char *product_ui_calculator_model_expression(
    const product_ui_calculator_model_t *model);

#endif /* __PRODUCT_UI_CALCULATOR_MODEL_H__ */
