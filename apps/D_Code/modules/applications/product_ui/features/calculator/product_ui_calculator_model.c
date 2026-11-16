/** @file product_ui_calculator_model.c @brief 有限输入、连续运算及重复等号的串行计算器模型。 */

#include "modules/applications/product_ui/features/calculator/product_ui_calculator_model.h"
#include <string.h>

/**
 * @brief 有界拷贝文字并补 NUL，空输入得到空串。
 * @param[out] output 目标缓冲区，NULL 时忽略。
 * @param[in] capacity 含终止符的总容量；零时不写入。
 * @param[in] text 借用的 NUL 结尾输入，可为 NULL。
 */
static void product_ui_calculator_copy(char *output, uint16_t capacity,
                                       const char *text)
{
    uint16_t index = 0;

    if ((output == NULL) || (capacity == 0U)) {
        return;
    }
    while ((text != NULL) && (text[index] != '\0') &&
           (index + 1U < capacity)) {
        output[index] = text[index];
        index++;
    }
    output[index] = '\0';
}

/**
 * @brief 在既有终止字符串后追加有限文字。
 * @param[in,out] output 已有 NUL 且已用长度小于 capacity 的输出缓冲。
 * @param[in] capacity 整个输出缓冲容量。
 * @param[in] text 借用的待追加文字，可为 NULL。
 */
static void product_ui_calculator_append(char *output, uint16_t capacity,
                                         const char *text)
{
    uint16_t used = (uint16_t)strlen(output);

    product_ui_calculator_copy(output + used, capacity - used, text);
}

/**
 * @brief 根据当前结果或等待态准备新输入，必要时开始全新计算。
 * @param[in,out] model 有效模型状态。
 */
static void product_ui_calculator_model_start_entry(
    product_ui_calculator_model_t *model)
{
    if ((model->state == PRODUCT_UI_CALCULATOR_STATE_RESULT) &&
        (model->pending_operation == PRODUCT_UI_CALCULATOR_OP_NONE)) {
        product_ui_calculator_model_init(model);
    }
    if (model->state != PRODUCT_UI_CALCULATOR_STATE_ENTRY) {
        product_ui_calculator_copy(model->entry, sizeof(model->entry), "0");
        model->state = PRODUCT_UI_CALCULATOR_STATE_ENTRY;
    }
}

/**
 * @brief 替换前导零或在容量内追加数字，并同步显示。
 * @param[in,out] model 有效模型状态。
 * @param[in] digit 调用方保证为 ASCII 数字。
 */
static void product_ui_calculator_model_digit(
    product_ui_calculator_model_t *model, char digit)
{
    uint16_t length;

    product_ui_calculator_model_start_entry(model);
    length = (uint16_t)strlen(model->entry);
    if ((model->entry[0] == '0') && (model->entry[1] == '\0')) {
        model->entry[0] = digit;
    } else if ((model->entry[0] == '-') && (model->entry[1] == '0') &&
               (model->entry[2] == '\0')) {
        model->entry[1] = digit;
    } else if (length + 1U < sizeof(model->entry)) {
        model->entry[length] = digit;
        model->entry[length + 1U] = '\0';
    }
    product_ui_calculator_copy(model->display, sizeof(model->display),
                               model->entry);
}

/**
 * @brief 进入输入态，在尚无小数点且有容量时追加小数点。
 * @param[in,out] model 有效模型状态。
 */
static void product_ui_calculator_model_decimal(
    product_ui_calculator_model_t *model)
{
    uint16_t length;

    product_ui_calculator_model_start_entry(model);
    length = (uint16_t)strlen(model->entry);
    if ((strchr(model->entry, '.') == NULL) &&
        (length + 1U < sizeof(model->entry))) {
        model->entry[length] = '.';
        model->entry[length + 1U] = '\0';
        product_ui_calculator_copy(model->display, sizeof(model->display),
                                   model->entry);
    }
}

/**
 * @brief 切换当前输入或独立结果的符号，并清除不再适用的重复运算。
 * @param[in,out] model 有效模型状态。
 */
static void product_ui_calculator_model_sign(
    product_ui_calculator_model_t *model)
{
    uint16_t length;

    if ((model->state == PRODUCT_UI_CALCULATOR_STATE_RESULT) &&
        (model->pending_operation == PRODUCT_UI_CALCULATOR_OP_NONE)) {
        model->accumulator = -model->accumulator;
        model->repeat_operation = PRODUCT_UI_CALCULATOR_OP_NONE;
        model->expression[0] = '\0';
        product_ui_calculator_engine_format(
            model->accumulator, model->display, sizeof(model->display));
        return;
    }
    product_ui_calculator_model_start_entry(model);
    length = (uint16_t)strlen(model->entry);
    if (model->entry[0] == '-') {
        memmove(model->entry, model->entry + 1, length);
    } else if (length + 1U < sizeof(model->entry)) {
        memmove(model->entry + 1, model->entry, length + 1U);
        model->entry[0] = '-';
    }
    product_ui_calculator_copy(model->display, sizeof(model->display),
                               model->entry);
}

/**
 * @brief 仅在输入态删除末字节，孤立负号恢复为零。
 * @param[in,out] model 有效模型状态。
 */
static void product_ui_calculator_model_backspace(
    product_ui_calculator_model_t *model)
{
    uint16_t length;

    if (model->state != PRODUCT_UI_CALCULATOR_STATE_ENTRY) {
        return;
    }
    length = (uint16_t)strlen(model->entry);
    if (length > 1U) {
        model->entry[length - 1U] = '\0';
    }
    if ((model->entry[0] == '-') && (model->entry[1] == '\0')) {
        product_ui_calculator_copy(model->entry, sizeof(model->entry), "0");
    }
    product_ui_calculator_copy(model->display, sizeof(model->display),
                               model->entry);
}

/**
 * @brief 把运算符映射为表达式使用的静态符号。
 * @param[in] operation 待显示运算符。
 * @return 有效运算符返回静态符号；NONE 或非法值返回空串。
 */
static const char *product_ui_calculator_operator_text(
    product_ui_calculator_operator_e operation)
{
    static const char *const texts[] = {"", "+", "-", "*", "/"};

    if ((operation < PRODUCT_UI_CALCULATOR_OP_NONE) ||
        (operation > PRODUCT_UI_CALCULATOR_OP_DIVIDE)) {
        return "";
    }
    return texts[operation];
}

/**
 * @brief 在模型缓冲内组织待运算或已完成的表达式。
 * @param[in,out] model 接收表达式的有效模型。
 * @param[in] lhs 左操作数。
 * @param[in] operation 运算符。
 * @param[in] rhs 可为空；非空时追加右操作数及等号。
 */
static void product_ui_calculator_expression_set(
    product_ui_calculator_model_t *model,
    product_ui_calculator_value_t lhs,
    product_ui_calculator_operator_e operation,
    const product_ui_calculator_value_t *rhs)
{
    char value_text[PRODUCT_UI_CALCULATOR_DISPLAY_CAP];

    model->expression[0] = '\0';
    product_ui_calculator_engine_format(lhs, value_text, sizeof(value_text));
    product_ui_calculator_append(model->expression, sizeof(model->expression),
                                 value_text);
    product_ui_calculator_append(model->expression, sizeof(model->expression),
                                 " ");
    product_ui_calculator_append(model->expression, sizeof(model->expression),
                                 product_ui_calculator_operator_text(operation));
    if (rhs != NULL) {
        product_ui_calculator_engine_format(*rhs, value_text,
                                            sizeof(value_text));
        product_ui_calculator_append(model->expression,
                                     sizeof(model->expression), " ");
        product_ui_calculator_append(model->expression,
                                     sizeof(model->expression), value_text);
        product_ui_calculator_append(model->expression,
                                     sizeof(model->expression), " =");
    }
}

/**
 * @brief 保存错误状态并以对应中文说明替代结果显示。
 * @param[in,out] model 有效模型状态。
 * @param[in] status 需要记录的算术或格式错误。
 * @return 原样返回传入的错误状态。
 */
static product_ui_calculator_status_e product_ui_calculator_model_error(
    product_ui_calculator_model_t *model,
    product_ui_calculator_status_e status)
{
    const char *message = "输入错误";

    if (status == PRODUCT_UI_CALCULATOR_ERROR_DIVIDE_BY_ZERO) {
        message = "除数不能为零";
    } else if (status == PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW) {
        message = "结果超出范围";
    }
    model->status = status;
    model->state = PRODUCT_UI_CALCULATOR_STATE_ERROR;
    model->expression[0] = '\0';
    product_ui_calculator_copy(model->display, sizeof(model->display), message);
    return status;
}

/**
 * @brief 清空运算、重复等号和错误状态，以零开始新的输入。
 */
void product_ui_calculator_model_init(product_ui_calculator_model_t *model)
{
    if (model == NULL) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->state = PRODUCT_UI_CALCULATOR_STATE_ENTRY;
    product_ui_calculator_copy(model->entry, sizeof(model->entry), "0");
    product_ui_calculator_copy(model->display, sizeof(model->display), "0");
}

/**
 * @brief 把当前显示值除以一百，作为新的输入或独立结果。
 * @param[in,out] model 有效模型状态。
 * @return 成功返回 OK；解析或除法失败时进入错误态并返回对应状态。
 */
static product_ui_calculator_status_e product_ui_calculator_model_percent(
    product_ui_calculator_model_t *model)
{
    product_ui_calculator_value_t value;
    product_ui_calculator_value_t result;
    product_ui_calculator_status_e status;

    status = product_ui_calculator_engine_parse(model->display, &value);
    if (status == PRODUCT_UI_CALCULATOR_OK) {
        status = product_ui_calculator_engine_apply(
            value, PRODUCT_UI_CALCULATOR_OP_DIVIDE,
            100 * PRODUCT_UI_CALCULATOR_SCALE, &result);
    }
    if (status != PRODUCT_UI_CALCULATOR_OK) {
        return product_ui_calculator_model_error(model, status);
    }
    product_ui_calculator_engine_format(result, model->entry,
                                        sizeof(model->entry));
    product_ui_calculator_copy(model->display, sizeof(model->display),
                               model->entry);
    if (model->pending_operation == PRODUCT_UI_CALCULATOR_OP_NONE) {
        model->accumulator = result;
        model->repeat_operation = PRODUCT_UI_CALCULATOR_OP_NONE;
        model->expression[0] = '\0';
    }
    model->state = PRODUCT_UI_CALCULATOR_STATE_ENTRY;
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 把四则运算逻辑键映射为引擎运算符。
 * @param[in] key 待映射的逻辑键。
 * @return 对应四则运算符；其他键返回 NONE。
 */
static product_ui_calculator_operator_e product_ui_calculator_key_operation(
    product_ui_calculator_key_e key)
{
    switch (key) {
    case PRODUCT_UI_CALCULATOR_KEY_ADD: return PRODUCT_UI_CALCULATOR_OP_ADD;
    case PRODUCT_UI_CALCULATOR_KEY_SUBTRACT:
        return PRODUCT_UI_CALCULATOR_OP_SUBTRACT;
    case PRODUCT_UI_CALCULATOR_KEY_MULTIPLY:
        return PRODUCT_UI_CALCULATOR_OP_MULTIPLY;
    case PRODUCT_UI_CALCULATOR_KEY_DIVIDE: return PRODUCT_UI_CALCULATOR_OP_DIVIDE;
    default: return PRODUCT_UI_CALCULATOR_OP_NONE;
    }
}

/**
 * @brief 需要时先完成前一运算，再预留新的运算符。
 * @param[in,out] model 有效模型状态。
 * @param[in] operation 已经由按键映射确认的运算符。
 * @return 成功返回 OK；解析或算术失败进入错误态并返回对应状态。
 */
static product_ui_calculator_status_e product_ui_calculator_model_operator(
    product_ui_calculator_model_t *model,
    product_ui_calculator_operator_e operation)
{
    product_ui_calculator_value_t rhs;
    product_ui_calculator_value_t result;
    product_ui_calculator_status_e status;

    if ((model->pending_operation != PRODUCT_UI_CALCULATOR_OP_NONE) &&
        (model->state == PRODUCT_UI_CALCULATOR_STATE_ENTRY)) {
        status = product_ui_calculator_engine_parse(model->entry, &rhs);
        if (status == PRODUCT_UI_CALCULATOR_OK) {
            status = product_ui_calculator_engine_apply(
                model->accumulator, model->pending_operation, rhs, &result);
        }
        if (status != PRODUCT_UI_CALCULATOR_OK) {
            return product_ui_calculator_model_error(model, status);
        }
        model->accumulator = result;
    } else if (model->pending_operation == PRODUCT_UI_CALCULATOR_OP_NONE) {
        status = product_ui_calculator_engine_parse(model->display,
                                                    &model->accumulator);
        if (status != PRODUCT_UI_CALCULATOR_OK) {
            return product_ui_calculator_model_error(model, status);
        }
    }
    model->pending_operation = operation;
    model->repeat_operation = PRODUCT_UI_CALCULATOR_OP_NONE;
    model->state = PRODUCT_UI_CALCULATOR_STATE_PENDING;
    product_ui_calculator_engine_format(model->accumulator, model->display,
                                        sizeof(model->display));
    product_ui_calculator_expression_set(model, model->accumulator,
                                         operation, NULL);
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 执行待运算或重复上次等号，并保存可重复操作数。
 * @param[in,out] model 有效模型状态。
 * @return 成功或无可执行运算返回 OK；失败进入错误态并返回对应状态。
 */
static product_ui_calculator_status_e product_ui_calculator_model_equals(
    product_ui_calculator_model_t *model)
{
    product_ui_calculator_value_t lhs = model->accumulator;
    product_ui_calculator_value_t rhs = model->repeat_operand;
    product_ui_calculator_value_t result;
    product_ui_calculator_operator_e operation = model->repeat_operation;
    product_ui_calculator_status_e status;

    if (model->pending_operation != PRODUCT_UI_CALCULATOR_OP_NONE) {
        operation = model->pending_operation;
        rhs = model->accumulator;
        if (model->state == PRODUCT_UI_CALCULATOR_STATE_ENTRY) {
            status = product_ui_calculator_engine_parse(model->entry, &rhs);
            if (status != PRODUCT_UI_CALCULATOR_OK) {
                return product_ui_calculator_model_error(model, status);
            }
        }
    } else if (operation == PRODUCT_UI_CALCULATOR_OP_NONE) {
        return PRODUCT_UI_CALCULATOR_OK;
    }
    status = product_ui_calculator_engine_apply(lhs, operation, rhs, &result);
    if (status != PRODUCT_UI_CALCULATOR_OK) {
        return product_ui_calculator_model_error(model, status);
    }
    product_ui_calculator_expression_set(model, lhs, operation, &rhs);
    model->accumulator = result;
    model->repeat_operand = rhs;
    model->repeat_operation = operation;
    model->pending_operation = PRODUCT_UI_CALCULATOR_OP_NONE;
    model->state = PRODUCT_UI_CALCULATOR_STATE_RESULT;
    product_ui_calculator_engine_format(result, model->display,
                                        sizeof(model->display));
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 处理一个逻辑按键并推进输入、连续运算、结果或错误状态。
 */
product_ui_calculator_status_e product_ui_calculator_model_press(
    product_ui_calculator_model_t *model, product_ui_calculator_key_e key)
{
    product_ui_calculator_operator_e operation;

    if (model == NULL) {
        return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
    }
    if (key == PRODUCT_UI_CALCULATOR_KEY_CLEAR) {
        product_ui_calculator_model_init(model);
        return PRODUCT_UI_CALCULATOR_OK;
    }
    if (model->state == PRODUCT_UI_CALCULATOR_STATE_ERROR) {
        if ((key >= PRODUCT_UI_CALCULATOR_KEY_0) &&
            (key <= PRODUCT_UI_CALCULATOR_KEY_9)) {
            product_ui_calculator_model_init(model);
        } else if (key == PRODUCT_UI_CALCULATOR_KEY_DECIMAL) {
            product_ui_calculator_model_init(model);
        } else {
            return model->status;
        }
    }
    if ((key >= PRODUCT_UI_CALCULATOR_KEY_0) &&
        (key <= PRODUCT_UI_CALCULATOR_KEY_9)) {
        product_ui_calculator_model_digit(model, (char)('0' + key));
    } else if (key == PRODUCT_UI_CALCULATOR_KEY_DECIMAL) {
        product_ui_calculator_model_decimal(model);
    } else if (key == PRODUCT_UI_CALCULATOR_KEY_SIGN) {
        product_ui_calculator_model_sign(model);
    } else if (key == PRODUCT_UI_CALCULATOR_KEY_BACKSPACE) {
        product_ui_calculator_model_backspace(model);
    } else if (key == PRODUCT_UI_CALCULATOR_KEY_PERCENT) {
        return product_ui_calculator_model_percent(model);
    } else if (key == PRODUCT_UI_CALCULATOR_KEY_EQUALS) {
        return product_ui_calculator_model_equals(model);
    } else {
        operation = product_ui_calculator_key_operation(key);
        if (operation == PRODUCT_UI_CALCULATOR_OP_NONE) {
            return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
        }
        return product_ui_calculator_model_operator(model, operation);
    }
    model->status = PRODUCT_UI_CALCULATOR_OK;
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 借用模型当前显示文字。
 */
const char *product_ui_calculator_model_display(
    const product_ui_calculator_model_t *model)
{
    return (model != NULL) ? model->display : "";
}

/**
 * @brief 借用模型当前表达式文字。
 */
const char *product_ui_calculator_model_expression(
    const product_ui_calculator_model_t *model)
{
    return (model != NULL) ? model->expression : "";
}
