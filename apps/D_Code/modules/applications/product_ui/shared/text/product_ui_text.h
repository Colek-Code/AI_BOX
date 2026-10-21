/**
 * @file product_ui_text.h
 * @brief Bounded UTF-8 helpers shared by Product UI models and views.
 */

#ifndef __PRODUCT_UI_TEXT_H__
#define __PRODUCT_UI_TEXT_H__

#include <stdint.h>

uint16_t product_ui_utf8_copy(char *output, uint16_t capacity,
                              const char *text);
uint16_t product_ui_utf8_copy_n(char *output, uint16_t capacity,
                                const char *text, uint16_t text_capacity);

#endif /* __PRODUCT_UI_TEXT_H__ */
