/**
 * @file product_ui_text.c
 * @brief Bounded UTF-8 copy that never emits a partial code point.
 */

#include <stddef.h>

#include "modules/applications/product_ui/shared/text/product_ui_text.h"

static uint8_t product_ui_utf8_sequence_length(uint8_t lead)
{
    if (lead < 0x80U) {
        return 1U;
    }
    if ((lead >= 0xC2U) && (lead <= 0xDFU)) {
        return 2U;
    }
    if ((lead >= 0xE0U) && (lead <= 0xEFU)) {
        return 3U;
    }
    if ((lead >= 0xF0U) && (lead <= 0xF4U)) {
        return 4U;
    }
    return 0U;
}

static uint8_t product_ui_utf8_sequence_valid(const uint8_t *text,
                                               uint8_t length)
{
    uint8_t index;

    if ((text == NULL) || (length == 0U)) {
        return 0U;
    }
    for (index = 1U; index < length; index++) {
        if ((text[index] == 0U) || ((text[index] & 0xC0U) != 0x80U)) {
            return 0U;
        }
    }
    if ((length == 3U) &&
        (((text[0] == 0xE0U) && (text[1] < 0xA0U)) ||
         ((text[0] == 0xEDU) && (text[1] >= 0xA0U)))) {
        return 0U;
    }
    if ((length == 4U) &&
        (((text[0] == 0xF0U) && (text[1] < 0x90U)) ||
         ((text[0] == 0xF4U) && (text[1] >= 0x90U)))) {
        return 0U;
    }
    return 1U;
}

uint16_t product_ui_utf8_copy_n(char *output, uint16_t capacity,
                                const char *text, uint16_t text_capacity)
{
    const uint8_t *input = (const uint8_t *)text;
    uint16_t input_index = 0U;
    uint16_t output_index = 0U;
    uint8_t sequence_length;
    uint8_t index;

    if ((output == NULL) || (capacity == 0U)) {
        return 0U;
    }
    output[0] = '\0';
    if ((text == NULL) || (text_capacity == 0U)) {
        return 0U;
    }
    while ((input_index < text_capacity) && (input[input_index] != 0U)) {
        sequence_length = product_ui_utf8_sequence_length(input[input_index]);
        if ((uint32_t)input_index + sequence_length > text_capacity) {
            break;
        }
        if (!product_ui_utf8_sequence_valid(&input[input_index],
                                            sequence_length)) {
            if (output_index + 1U >= capacity) {
                break;
            }
            output[output_index++] = '?';
            input_index++;
            continue;
        }
        if ((uint32_t)output_index + sequence_length >= capacity) {
            break;
        }
        for (index = 0U; index < sequence_length; index++) {
            output[output_index++] = (char)input[input_index++];
        }
    }
    output[output_index] = '\0';
    return output_index;
}

uint16_t product_ui_utf8_copy(char *output, uint16_t capacity,
                              const char *text)
{
    return product_ui_utf8_copy_n(output, capacity, text, UINT16_MAX);
}
