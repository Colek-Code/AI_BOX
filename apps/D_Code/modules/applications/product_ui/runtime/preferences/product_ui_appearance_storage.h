/** @file product_ui_appearance_storage.h @brief Device/host storage boundary. */
#ifndef PRODUCT_UI_APPEARANCE_STORAGE_H
#define PRODUCT_UI_APPEARANCE_STORAGE_H
#include <stdint.h>

/* Return the actual byte count, or a negative error. Never perform LVGL work. */
int product_ui_appearance_storage_read(uint8_t *record, uint16_t size);
int product_ui_appearance_storage_write(const uint8_t *record, uint16_t size);
/* One monotonic clock for acceptance, worker completion and timeout observers. */
uint32_t product_ui_appearance_clock_ms(void);
#endif
