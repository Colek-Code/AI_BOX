/** @file product_ui_clock_port.h @brief SDK boundary for product software time. */
#ifndef PRODUCT_UI_CLOCK_PORT_H
#define PRODUCT_UI_CLOCK_PORT_H
#include "product_ui_time_domain.h"
uint32_t product_ui_clock_port_ms(void);
product_ui_time_unavailable_reason_e product_ui_clock_port_network(int64_t *utc_seconds);
uint16_t product_ui_clock_port_start(void (*callback)(void *));
void product_ui_clock_port_stop(uint16_t timer);
#endif
