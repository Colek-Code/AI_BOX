/** @file product_ui_calculator_engine.c @brief 有范围检查的千分制定点运算、文本解析和显示格式化。 */

#include "modules/applications/product_ui/features/calculator/product_ui_calculator_engine.h"
#include <stddef.h>

/**
 * @brief 取得已受限千分制定点值的绝对值。
 * @param[in] value 调用链保证不为 INT64_MIN 的定点值。
 * @return 输入的非负绝对值。
 */
static product_ui_calculator_value_t product_ui_calculator_abs(
    product_ui_calculator_value_t value)
{
    return (value < 0) ? -value : value;
}

/**
 * @brief 先检查上界及下界再写入定点加法结果。
 * @param[in] lhs 已受限左操作数。
 * @param[in] rhs 已受限右操作数。
 * @param[out] result 调用方保证非空的成功结果。
 * @return 未超界返回 OK；越界返回 OVERFLOW 并保留原输出。
 */
static product_ui_calculator_status_e product_ui_calculator_add(
    product_ui_calculator_value_t lhs,
    product_ui_calculator_value_t rhs,
    product_ui_calculator_value_t *result)
{
    if (((rhs > 0) &&
         (lhs > (PRODUCT_UI_CALCULATOR_VALUE_LIMIT - rhs))) ||
        ((rhs < 0) &&
         (lhs < (-PRODUCT_UI_CALCULATOR_VALUE_LIMIT - rhs)))) {
        return PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW;
    }
    *result = lhs + rhs;
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 按整数与余数分解计算缩放乘积，避免直接相乘溢出。
 * @param[in] lhs 已受限左操作数。
 * @param[in] rhs 已受限右操作数。
 * @param[out] result 非空结果缓冲；分数乘积向零截断。
 * @return 结果可表示返回 OK；超界返回 OVERFLOW 且不写结果。
 */
static product_ui_calculator_status_e product_ui_calculator_multiply(
    product_ui_calculator_value_t lhs,
    product_ui_calculator_value_t rhs,
    product_ui_calculator_value_t *result)
{
    product_ui_calculator_value_t lhs_abs;
    product_ui_calculator_value_t rhs_abs;
    product_ui_calculator_value_t whole_product;
    product_ui_calculator_value_t fraction_product;
    product_ui_calculator_value_t quotient;
    product_ui_calculator_value_t remainder;

    lhs_abs = product_ui_calculator_abs(lhs);
    rhs_abs = product_ui_calculator_abs(rhs);
    quotient = lhs_abs / PRODUCT_UI_CALCULATOR_SCALE;
    remainder = lhs_abs % PRODUCT_UI_CALCULATOR_SCALE;

    if ((rhs_abs != 0) &&
        (quotient > (PRODUCT_UI_CALCULATOR_VALUE_LIMIT / rhs_abs))) {
        return PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW;
    }
    whole_product = quotient * rhs_abs;
    fraction_product =
        (remainder * rhs_abs) / PRODUCT_UI_CALCULATOR_SCALE;
    if (whole_product >
        (PRODUCT_UI_CALCULATOR_VALUE_LIMIT - fraction_product)) {
        return PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW;
    }

    *result = whole_product + fraction_product;
    if ((lhs < 0) != (rhs < 0)) {
        *result = -*result;
    }
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 在拒绝零除后计算按千分制缩放的商。
 * @param[in] lhs 已受限被除数，缩放后仍可由 int64_t 表示。
 * @param[in] rhs 已受限除数。
 * @param[out] result 非空结果指针，仅成功时写入。
 * @return 成功返回 OK；除数为零返回 DIVIDE_BY_ZERO；商超界返回 OVERFLOW。
 */
static product_ui_calculator_status_e product_ui_calculator_divide(
    product_ui_calculator_value_t lhs,
    product_ui_calculator_value_t rhs,
    product_ui_calculator_value_t *result)
{
    product_ui_calculator_value_t quotient;

    if (rhs == 0) {
        return PRODUCT_UI_CALCULATOR_ERROR_DIVIDE_BY_ZERO;
    }
    quotient = (lhs * PRODUCT_UI_CALCULATOR_SCALE) / rhs;
    if (product_ui_calculator_abs(quotient) >
        PRODUCT_UI_CALCULATOR_VALUE_LIMIT) {
        return PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW;
    }
    *result = quotient;
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 对两个受限的千分制定点数执行四则运算。
 */
product_ui_calculator_status_e product_ui_calculator_engine_apply(
    product_ui_calculator_value_t lhs,
    product_ui_calculator_operator_e operation,
    product_ui_calculator_value_t rhs,
    product_ui_calculator_value_t *result)
{
    if (result == NULL) {
        return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
    }
    if ((lhs < -PRODUCT_UI_CALCULATOR_VALUE_LIMIT) ||
        (lhs > PRODUCT_UI_CALCULATOR_VALUE_LIMIT) ||
        (rhs < -PRODUCT_UI_CALCULATOR_VALUE_LIMIT) ||
        (rhs > PRODUCT_UI_CALCULATOR_VALUE_LIMIT)) {
        return PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW;
    }

    switch (operation) {
    case PRODUCT_UI_CALCULATOR_OP_ADD:
        return product_ui_calculator_add(lhs, rhs, result);
    case PRODUCT_UI_CALCULATOR_OP_SUBTRACT:
        return product_ui_calculator_add(lhs, -rhs, result);
    case PRODUCT_UI_CALCULATOR_OP_MULTIPLY:
        return product_ui_calculator_multiply(lhs, rhs, result);
    case PRODUCT_UI_CALCULATOR_OP_DIVIDE:
        return product_ui_calculator_divide(lhs, rhs, result);
    default:
        return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
    }
}

typedef struct {
    product_ui_calculator_value_t whole;
    product_ui_calculator_value_t fraction;
    uint8_t fraction_digits;
    uint8_t has_digit;
    uint8_t has_decimal;
} product_ui_calculator_parse_t;

/**
 * @brief 向解析状态追加一位十进制数字并检查整数与小数位数边界。
 * @param[in,out] parse 已初始化的解析累积状态。
 * @param[in] digit 调用方保证在 ASCII 0 至 9 范围内。
 * @return 可继续解析返回 OK；小数超过三位返回 FORMAT，整数增长超界返回 OVERFLOW。
 */
static product_ui_calculator_status_e product_ui_calculator_parse_digit(
    product_ui_calculator_parse_t *parse, char digit)
{
    parse->has_digit = 1;
    if (parse->has_decimal) {
        if (parse->fraction_digits >= 3U) {
            return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
        }
        parse->fraction = (parse->fraction * 10) + (digit - '0');
        parse->fraction_digits++;
    } else {
        if (parse->whole >
            (PRODUCT_UI_CALCULATOR_VALUE_LIMIT /
             PRODUCT_UI_CALCULATOR_SCALE / 10)) {
            return PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW;
        }
        parse->whole = (parse->whole * 10) + (digit - '0');
    }
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 解析可选负号及最多三位小数的十进制文本。
 */
product_ui_calculator_status_e product_ui_calculator_engine_parse(
    const char *text, product_ui_calculator_value_t *value)
{
    product_ui_calculator_parse_t parse = {0};
    product_ui_calculator_status_e status;
    uint8_t is_negative = 0;

    if ((text == NULL) || (value == NULL)) {
        return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
    }
    if (*text == '-') {
        is_negative = 1;
        text++;
    }
    while (*text != '\0') {
        if (*text == '.') {
            if (parse.has_decimal) {
                return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
            }
            parse.has_decimal = 1;
        } else if ((*text >= '0') && (*text <= '9')) {
            status = product_ui_calculator_parse_digit(&parse, *text);
            if (status != PRODUCT_UI_CALCULATOR_OK) {
                return status;
            }
        } else {
            return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
        }
        text++;
    }
    if (!parse.has_digit) {
        return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
    }
    while (parse.fraction_digits < 3U) {
        parse.fraction *= 10;
        parse.fraction_digits++;
    }
    *value = (parse.whole * PRODUCT_UI_CALCULATOR_SCALE) + parse.fraction;
    if (*value > PRODUCT_UI_CALCULATOR_VALUE_LIMIT) {
        return PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW;
    }
    if (is_negative) {
        *value = -*value;
    }
    return PRODUCT_UI_CALCULATOR_OK;
}

/**
 * @brief 把受限千分制定点数格式化为去掉小数尾零的十进制文字。
 */
product_ui_calculator_status_e product_ui_calculator_engine_format(
    product_ui_calculator_value_t value, char *output, uint16_t output_size)
{
    product_ui_calculator_value_t absolute;
    product_ui_calculator_value_t whole;
    uint16_t fraction;
    char reverse[16];
    uint8_t reverse_count = 0;
    uint8_t output_count = 0;

    if ((output == NULL) || (output_size < 3U) ||
        (value < -PRODUCT_UI_CALCULATOR_VALUE_LIMIT) ||
        (value > PRODUCT_UI_CALCULATOR_VALUE_LIMIT)) {
        return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
    }
    absolute = product_ui_calculator_abs(value);
    whole = absolute / PRODUCT_UI_CALCULATOR_SCALE;
    fraction = (uint16_t)(absolute % PRODUCT_UI_CALCULATOR_SCALE);
    if (value < 0) {
        output[output_count++] = '-';
    }
    do {
        reverse[reverse_count++] = (char)('0' + (whole % 10));
        whole /= 10;
    } while ((whole != 0) && (reverse_count < sizeof(reverse)));
    if ((uint16_t)(output_count + reverse_count + 1U) > output_size) {
        return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
    }
    while (reverse_count > 0U) {
        output[output_count++] = reverse[--reverse_count];
    }
    if (fraction != 0U) {
        if ((uint16_t)(output_count + 5U) > output_size) {
            return PRODUCT_UI_CALCULATOR_ERROR_FORMAT;
        }
        output[output_count++] = '.';
        output[output_count++] = (char)('0' + (fraction / 100U));
        output[output_count++] = (char)('0' + ((fraction / 10U) % 10U));
        output[output_count++] = (char)('0' + (fraction % 10U));
        while (output[output_count - 1U] == '0') {
            output_count--;
        }
    }
    output[output_count] = '\0';
    return PRODUCT_UI_CALCULATOR_OK;
}
