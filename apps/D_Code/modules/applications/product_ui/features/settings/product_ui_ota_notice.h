#ifndef PRODUCT_UI_OTA_NOTICE_H
#define PRODUCT_UI_OTA_NOTICE_H
#include <stdint.h>
/** Create on the UI thread before accepting a destructive upgrade request. */
int product_ui_ota_notice_prepare(void);
/** UI processing phase only: discard a prepared overlay when acceptance fails. */
void product_ui_ota_notice_discard(void);
void product_ui_ota_notice_process(void);
uint8_t product_ui_ota_notice_blocking(void);
const char *product_ui_ota_state_text(unsigned state);
#endif
