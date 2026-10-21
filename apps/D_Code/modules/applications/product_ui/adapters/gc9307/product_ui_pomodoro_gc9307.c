/** @file product_ui_pomodoro_gc9307.c @brief Bounded VM and independent sys_timer adapter. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && D_BOARD_IS_GC9307
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_port.h"
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_model.h"
#include "modules/applications/product_ui/features/time/product_ui_clock.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"
#include "product_ui_vm_gc9307.h"

/* Init and the single save worker serialize reads; not shared with alarm IO. */
static uint32_t s_pomodoro_vm_status[PRODUCT_UI_VM_STATUS_SIZE / sizeof(uint32_t)];

uint32_t product_ui_pomodoro_clock_ms(void) { return (uint32_t)timer_get_ms(); }

uint32_t product_ui_pomodoro_today(void)
{
    product_ui_datetime_t value;
    if (product_ui_clock_read(&value) != PRODUCT_UI_TIME_UNAVAILABLE_NONE) return 0U;
    return (uint32_t)value.date.year * 10000U + (uint32_t)value.date.month * 100U + value.date.day;
}

int product_ui_pomodoro_storage_read(uint8_t slot, uint8_t *record, uint16_t size)
{
    int result;
    if (slot > 1U || record == NULL || size != PRODUCT_UI_POMODORO_RECORD_SIZE) return D_ERR_INVALID_ARG;
    result = product_ui_vm_read(slot ? PRODUCT_UI_POMODORO_VM_B : PRODUCT_UI_POMODORO_VM_A,
                               record, size, s_pomodoro_vm_status);
    return result == VM_DATA_LEN_ERR ? D_ERR_NOT_READY : result;
}

int product_ui_pomodoro_storage_write(uint8_t slot, const uint8_t *record, uint16_t size)
{
    if (slot > 1U || record == NULL || size != PRODUCT_UI_POMODORO_RECORD_SIZE) return D_ERR_INVALID_ARG;
    return syscfg_write(slot ? PRODUCT_UI_POMODORO_VM_B : PRODUCT_UI_POMODORO_VM_A, (void *)record, size);
}

uint16_t product_ui_pomodoro_timer_start(void (*callback)(void *))
{
    return sys_timer_add_to_task("sys_timer", NULL, callback, 100U);
}

void product_ui_pomodoro_timer_stop(uint16_t timer) { if (timer != 0U) sys_timer_del(timer); }
#endif
