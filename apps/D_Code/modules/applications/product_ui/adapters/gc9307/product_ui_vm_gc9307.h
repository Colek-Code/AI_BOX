/** @file product_ui_vm_gc9307.h @brief 固定 SDK 布局下的只读 VM 存在性判别，避免错误创建默认记录。 */
#ifndef PRODUCT_UI_VM_GC9307_H
#define PRODUCT_UI_VM_GC9307_H
#include "framework/d_error.h"
#include "framework/d_log.h"
#include "syscfg/syscfg_id.h"
#include <stdint.h>
#include "platform/storage/d_vm_status.h"

/* 保留产品适配 API 及 IO 策略；SDK 布局由平台层拥有，
 * 闹钟与番茄钟各自持有独立暂存缓冲。 */
#define PRODUCT_UI_VM_STATUS_SIZE D_VM_STATUS_SIZE
#define PRODUCT_UI_VM_INDEX_OFFSET D_VM_INDEX_OFFSET
#define PRODUCT_UI_VM_FLAGS_OFFSET D_VM_FLAGS_OFFSET
#define product_ui_vm_present d_vm_present

/**
 * @brief 以读取前后两次存在性观察区分确实缺失、损坏及未就绪。
 * @param[in] id 项目已分配的 VM 索引。
 * @param[out] record 容量至少为 size 的读取缓冲区；失败时内容不可使用。
 * @param[in] size 本次完整记录大小。
 * @param[in,out] scratch 由调用方串行持有的 SDK 状态暂存，至少 PRODUCT_UI_VM_STATUS_SIZE 字节。
 * @return 正读取长度或 VM_DATA_LEN_ERR 原样返回；稳定缺失为 NOT_FOUND，已存在但读取错误为 IO，不可判断为 NOT_READY。
 * @note SDK 的 -510 同时表示缺失或损坏；只有前后均明确不存在才允许上层创建默认值，零读长或缓冲哨兵都不证明缺失。
 */
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
