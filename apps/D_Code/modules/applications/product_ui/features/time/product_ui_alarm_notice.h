/** @file product_ui_alarm_notice.h @brief UI-thread alarm overlay and navigation barrier. */
#ifndef PRODUCT_UI_ALARM_NOTICE_H
#define PRODUCT_UI_ALARM_NOTICE_H
#include <stdint.h>
void product_ui_alarm_notice_process(void);
uint8_t product_ui_alarm_notice_blocking(void);
uint8_t product_ui_alarm_notice_pending(void);
#endif
