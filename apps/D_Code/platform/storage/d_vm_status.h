/**
 * @file d_vm_status.h
 * @brief 按已核实的 AC79 VM ABI 只读查询索引是否存在，不修改 VM 内容。
 */
#ifndef D_VM_STATUS_H
#define D_VM_STATUS_H
#include <stdint.h>

/* ABI 来源：AC79NN 1.2.14 cfg_tool.a，摘要为
 * 906341ad25ed6d80b1bb552f0d2fa58c3dd6a76fadc8fbadf7bed3e97b1f06fd。
 * 由 check_alarm_sdk_vm.py 核实；调用方提供对齐的临时缓冲，本层不保存共享缓冲或业务策略。 */
#define D_VM_STATUS_SIZE 624U
#define D_VM_INDEX_OFFSET 24U
#define D_VM_FLAGS_OFFSET 623U
struct _VM_STRUCT;
extern int vm_status(struct _VM_STRUCT *status);

/**
 * @brief 使用调用方临时缓冲查询 VM 索引的存在位。
 * @param[in] id VM 索引，范围为 0 至 255。
 * @param[out] scratch 调用方拥有且按 uint32_t 对齐的临时数组，至少 D_VM_STATUS_SIZE/4 项。
 * @return 1 表示存在，0 表示不存在；缓冲为空、索引越界、SDK 查询失败或状态无效时为 -1。
 * @note 依赖上方固定 SDK ABI；只判断存在，不校验业务记录版本、CRC 或保存是否完成。
 */
static inline int d_vm_present(uint16_t id, uint32_t *scratch)
{
    const uint8_t *bytes = (const uint8_t *)scratch;
    uint16_t position = (uint16_t)(D_VM_INDEX_OFFSET + id * 2U);
    if (!scratch || id >= 256U || vm_status((struct _VM_STRUCT *)(void *)scratch) != 0 ||
        (bytes[D_VM_FLAGS_OFFSET] & 1U) == 0U) return -1;
    return (bytes[position] | bytes[position + 1U]) != 0U;
}
#endif
