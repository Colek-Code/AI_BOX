/**
 * @file product_ui_calculator_engine.h
 * @brief Bounded fixed-point arithmetic for the Product UI calculator.
 */

#ifndef PRODUCT_UI_CALCULATOR_ENGINE_H
#define PRODUCT_UI_CALCULATOR_ENGINE_H

#include <stdint.h>

#define PRODUCT_UI_CALCULATOR_SCALE       1000LL
#define PRODUCT_UI_CALCULATOR_VALUE_LIMIT 999999999999LL

typedef int64_t product_ui_calculator_value_t;

typedef enum {
    PRODUCT_UI_CALCULATOR_OP_NONE,
    PRODUCT_UI_CALCULATOR_OP_ADD,
    PRODUCT_UI_CALCULATOR_OP_SUBTRACT,
    PRODUCT_UI_CALCULATOR_OP_MULTIPLY,
    PRODUCT_UI_CALCULATOR_OP_DIVIDE,
} product_ui_calculator_operator_e;

typedef enum {
    PRODUCT_UI_CALCULATOR_OK,
    PRODUCT_UI_CALCULATOR_ERROR_DIVIDE_BY_ZERO,
    PRODUCT_UI_CALCULATOR_ERROR_OVERFLOW,
    PRODUCT_UI_CALCULATOR_ERROR_FORMAT,
} product_ui_calculator_status_e;

product_ui_calculator_status_e product_ui_calculator_engine_apply(
    product_ui_calculator_value_t lhs,
    product_ui_calculator_operator_e operation,
    product_ui_calculator_value_t rhs,
    product_ui_calculator_value_t *result);
product_ui_calculator_status_e product_ui_calculator_engine_parse(
    const char *text, product_ui_calculator_value_t *value);
product_ui_calculator_status_e product_ui_calculator_engine_format(
    product_ui_calculator_value_t value, char *output, uint16_t output_size);

#endif /* PRODUCT_UI_CALCULATOR_ENGINE_H */
