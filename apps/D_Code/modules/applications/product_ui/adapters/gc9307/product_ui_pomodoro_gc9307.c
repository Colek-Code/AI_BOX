/** @file product_ui_pomodoro_gc9307.c @brief 番茄钟双槽 VM 及独立系统定时器端口。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && D_BOARD_IS_GC9307
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_port.h"
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_model.h"
#include "modules/applications/product_ui/features/time/product_ui_clock.h"
#include "system/includes.h"
#include "syscfg/syscfg_id.h"
#include "product_ui_vm_gc9307.h"

/* 初始化及单一保存 worker 串行读取；此暂存区不与闹钟 IO 共用。 */
static uint32_t s_pomodoro_vm_status[PRODUCT_UI_VM_STATUS_SIZE / sizeof(uint32_t)];

/**
 * @brief 读取番茄钟统一的 uint32_t 回绕毫秒时钟。
 */
uint32_t product_ui_pomodoro_clock_ms(void) { return (uint32_t)timer_get_ms(); }

/**
 * @brief 读取有效北京时间并编码年月日，无可信时钟时返回零。
 */
uint32_t product_ui_pomodoro_today(void)
{
    product_ui_datetime_t value;
    if (product_ui_clock_read(&value) != PRODUCT_UI_TIME_UNAVAILABLE_NONE) return 0U;
    return (uint32_t)value.date.year * 10000U + (uint32_t)value.date.month * 100U + value.date.day;
}

/**
 * @brief 按完整记录尺寸读取番茄钟双槽，长度不符映射为未就绪以保护记录。
 */
int product_ui_pomodoro_storage_read(uint8_t slot, uint8_t *record, uint16_t size)
{
    int result;
    if (slot > 1U || record == NULL || size != PRODUCT_UI_POMODORO_RECORD_SIZE) return D_ERR_INVALID_ARG;
    result = product_ui_vm_read(slot ? PRODUCT_UI_POMODORO_VM_B : PRODUCT_UI_POMODORO_VM_A,
                               record, size, s_pomodoro_vm_status);
    return result == VM_DATA_LEN_ERR ? D_ERR_NOT_READY : result;
}

/**
 * @brief 校验槽和完整记录尺寸后写入对应番茄钟 VM 槽。
 */
int product_ui_pomodoro_storage_write(uint8_t slot, const uint8_t *record, uint16_t size)
{
    if (slot > 1U || record == NULL || size != PRODUCT_UI_POMODORO_RECORD_SIZE) return D_ERR_INVALID_ARG;
    return syscfg_write(slot ? PRODUCT_UI_POMODORO_VM_B : PRODUCT_UI_POMODORO_VM_A, (void *)record, size);
}

/**
 * @brief 在 sys_timer 任务创建 100 毫秒番茄钟周期回调。
 */
uint16_t product_ui_pomodoro_timer_start(void (*callback)(void *))
{
    return sys_timer_add_to_task("sys_timer", NULL, callback, 100U);
}

/**
 * @brief 删除非零番茄钟定时器编号。
 */
void product_ui_pomodoro_timer_stop(uint16_t timer) { if (timer != 0U) sys_timer_del(timer); }
#endif
