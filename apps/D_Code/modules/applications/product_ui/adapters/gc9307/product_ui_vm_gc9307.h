/** @file product_ui_vm_gc9307.h @brief Read-only, pinned SDK VM presence classification. */
#ifndef PRODUCT_UI_VM_GC9307_H
#define PRODUCT_UI_VM_GC9307_H
#include "framework/d_error.h"
#include "framework/d_log.h"
#include "syscfg/syscfg_id.h"
#include <stdint.h>

/* cfg_tool.a e276ce59adddc701a71558f50cc331309efeba360350dc19a1de8c9104d889bd.
 * check_alarm_sdk_vm.py verifies this opaque ABI. Each IO owner supplies its
 * own scratch buffer; alarm and Pomodoro workers must not share mutable data. */
#define PRODUCT_UI_VM_STATUS_SIZE 624U
#define PRODUCT_UI_VM_INDEX_OFFSET 24U
#define PRODUCT_UI_VM_FLAGS_OFFSET 623U
struct _VM_STRUCT;
extern int vm_status(struct _VM_STRUCT *status);

static inline int product_ui_vm_present(uint16_t id, uint32_t *scratch)
{
    const uint8_t *bytes = (const uint8_t *)scratch;
    uint16_t position = (uint16_t)(PRODUCT_UI_VM_INDEX_OFFSET + id * 2U);
    if (id >= 256U || vm_status((struct _VM_STRUCT *)(void *)scratch) != 0 ||
        (bytes[PRODUCT_UI_VM_FLAGS_OFFSET] & 1U) == 0U) return -1;
    return (bytes[position] | bytes[position + 1U]) != 0U;
}

/* -510 means both absent and damaged in this SDK. A stable absent index is
 * the only permission to create defaults; neither a zero read nor a sentinel
 * in the destination buffer proves absence. Preserve -511 for caller policy. */
static inline int product_ui_vm_read(uint16_t id, void *record, uint16_t size,
                                     uint32_t *scratch)
{
    int before = product_ui_vm_present(id, scratch);
    int result = syscfg_read(id, record, size);
    int after = product_ui_vm_present(id, scratch);
    int mapped;
    if (result > 0 || result == VM_DATA_LEN_ERR) mapped = result;
    else if (result == VM_READ_DATA_ERR) {
        if (before < 0 || after < 0) mapped = D_ERR_NOT_READY;
        else mapped = !before && !after ? D_ERR_NOT_FOUND : D_ERR_IO;
    } else mapped = result < 0 ? D_ERR_NOT_READY : D_ERR_IO;
    LOGI("PRODUCT_VM", "read id=%u raw=%d present=%d/%d mapped=%d", id, result, before, after, mapped);
    return mapped;
}
#endif
