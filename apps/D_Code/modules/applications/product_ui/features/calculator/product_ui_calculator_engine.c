/**
 * @file product_ui_calculator_engine.c
 * @brief Overflow-checked fixed-point calculator operations.
 */

#include "modules/applications/product_ui/features/calculator/product_ui_calculator_engine.h"
#include <stddef.h>

static product_ui_calculator_value_t product_ui_calculator_abs(
    product_ui_calculator_value_t value)
{
    return (value < 0) ? -value : value;
}

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
