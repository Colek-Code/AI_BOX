/** @file product_ui_alarm_gc9307.c @brief Alarm VM and independent sys_timer ports. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && D_BOARD_IS_GC9307
#include "modules/applications/product_ui/features/time/product_ui_alarm_port.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_model.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"
#include "framework/d_log.h"
#include "product_ui_vm_gc9307.h"

static uint32_t s_alarm_vm_status[PRODUCT_UI_VM_STATUS_SIZE / sizeof(uint32_t)];

int product_ui_alarm_storage_read(uint8_t slot, uint8_t *record, uint16_t size)
{
    uint16_t id;
    int result, mapped;
    if (slot > 1U || record == NULL || size != PRODUCT_UI_ALARM_RECORD_SIZE) return D_ERR_INVALID_ARG;
    id = slot ? PRODUCT_UI_ALARM_VM_B : PRODUCT_UI_ALARM_VM_A;
    result = product_ui_vm_read(id, record, size, s_alarm_vm_status);
    mapped = result == VM_DATA_LEN_ERR ? PRODUCT_UI_ALARM_ERR_VERSION : result;
    return mapped;
}

int product_ui_alarm_storage_write(uint8_t slot, const uint8_t *record, uint16_t size)
{
    if (slot > 1U || record == NULL || size != PRODUCT_UI_ALARM_RECORD_SIZE) return D_ERR_INVALID_ARG;
    return syscfg_write(slot ? PRODUCT_UI_ALARM_VM_B : PRODUCT_UI_ALARM_VM_A, (void *)record, size);
}

uint16_t product_ui_alarm_timer_start(void (*callback)(void *))
{ return sys_timer_add_to_task("sys_timer", NULL, callback, 100U); }
void product_ui_alarm_timer_stop(uint16_t timer) { if (timer) sys_timer_del(timer); }
#endif
