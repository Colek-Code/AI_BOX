/** @file product_ui_calculator_engine.h @brief 有范围检查的千分制定点运算、文本解析和显示格式化。 */

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

/**
 * @brief 对两个受限的千分制定点数执行四则运算。
 * @param[in] lhs 左操作数，实际值乘以 PRODUCT_UI_CALCULATOR_SCALE。
 * @param[in] operation 加、减、乘、除之一。
 * @param[in] rhs 右操作数，使用相同缩放。
 * @param[out] result 非空结果指针；只有成功路径写入。
 * @return 成功返回 OK；零除、输入或结果越界、非法操作或空输出分别返回对应状态。
 * @note 乘除保留三位定点精度，整数除法向零截断；没有浮点或后台操作。
 */
product_ui_calculator_status_e product_ui_calculator_engine_apply(
    product_ui_calculator_value_t lhs,
    product_ui_calculator_operator_e operation,
    product_ui_calculator_value_t rhs,
    product_ui_calculator_value_t *result);
/**
 * @brief 解析可选负号及最多三位小数的十进制文本。
 * @param[in] text NUL 结尾输入；不接受空白、正号和指数。
 * @param[out] value 千分制定点结果；失败时不得使用，最终范围检查前可能已写入。
 * @return 成功返回 OK；格式无效或空参数返回 FORMAT，数值超界返回 OVERFLOW。
 */
product_ui_calculator_status_e product_ui_calculator_engine_parse(
    const char *text, product_ui_calculator_value_t *value);
/**
 * @brief 把受限千分制定点数格式化为去掉小数尾零的十进制文字。
 * @param[in] value 范围内的千分制定点值。
 * @param[out] output 调用方提供的文字缓冲区。
 * @param[in] output_size 含末尾 NUL 的总容量，至少为 3。
 * @return 成功返回 OK；数值越界、空输出或容量不足返回 FORMAT。
 * @note 失败可能已写入部分文字，且不保证 NUL 结尾；调用方只在成功时消费输出。
 */
product_ui_calculator_status_e product_ui_calculator_engine_format(
    product_ui_calculator_value_t value, char *output, uint16_t output_size);

#endif /* PRODUCT_UI_CALCULATOR_ENGINE_H */
