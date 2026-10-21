/** @file product_ui_pomodoro_port.h @brief Device/host ports; no LVGL ownership. */
#ifndef PRODUCT_UI_POMODORO_PORT_H
#define PRODUCT_UI_POMODORO_PORT_H
#include <stdint.h>
#define PRODUCT_UI_POMODORO_VM_A 27U
#define PRODUCT_UI_POMODORO_VM_B 28U
uint32_t product_ui_pomodoro_clock_ms(void);
uint32_t product_ui_pomodoro_today(void);
/** Return length or a normalized error; NOT_FOUND requires proven absence. */
int product_ui_pomodoro_storage_read(uint8_t slot, uint8_t *record, uint16_t size);
int product_ui_pomodoro_storage_write(uint8_t slot, const uint8_t *record, uint16_t size);
uint16_t product_ui_pomodoro_timer_start(void (*callback)(void *));
void product_ui_pomodoro_timer_stop(uint16_t timer);
#endif
