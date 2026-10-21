/**
 * @file product_ui_calculator_model.c
 * @brief Bounded calculator input and continuous-operation model.
 */

#include "modules/applications/product_ui/features/calculator/product_ui_calculator_model.h"
#include <string.h>

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

static void product_ui_calculator_append(char *output, uint16_t capacity,
                                         const char *text)
{
    uint16_t used = (uint16_t)strlen(output);

    product_ui_calculator_copy(output + used, capacity - used, text);
}

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
    model->state = PRODUCT_UI_CALCULATOR_STATE_ENTRY;
    return PRODUCT_UI_CALCULATOR_OK;
}

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

const char *product_ui_calculator_model_display(
    const product_ui_calculator_model_t *model)
{
    return (model != NULL) ? model->display : "";
}

const char *product_ui_calculator_model_expression(
    const product_ui_calculator_model_t *model)
{
    return (model != NULL) ? model->expression : "";
}
