/** @file product_ui_appearance_gc9307.c @brief Product UI VM storage adapter. */
#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && D_BOARD_IS_GC9307
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance_storage.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"

int product_ui_appearance_storage_read(uint8_t *record, uint16_t size)
{
    if (record == NULL || size != PRODUCT_UI_APPEARANCE_RECORD_SIZE) return D_ERR_INVALID_ARG;
    return syscfg_read(PRODUCT_UI_APPEARANCE_VM_ID, record, size);
}

int product_ui_appearance_storage_write(const uint8_t *record, uint16_t size)
{
    if (record == NULL || size != PRODUCT_UI_APPEARANCE_RECORD_SIZE) return D_ERR_INVALID_ARG;
    return syscfg_write(PRODUCT_UI_APPEARANCE_VM_ID, (void *)record, size);
}

uint32_t product_ui_appearance_clock_ms(void)
{
    return (uint32_t)timer_get_ms();
}
#endif
