/** @file qwen_hal_storage.h @brief 在 VM16 边界确认 SDK 凭据已完整持久化。 */
#ifndef QWEN_HAL_STORAGE_H
#define QWEN_HAL_STORAGE_H
#include "framework/d_error.h"

/**
 * @brief 导出 SDK 自有格式，完整写入 VM16 并回读比较。
 * @return 完整写入及回读一致返回 D_OK；并发调用返回 D_ERR_BUSY；License 未启用返回 D_ERR_NOT_READY；其余返回 D_ERR_IO。
 * @note 由 Qwen 工作任务在 SDK 初始化后调用；不信任会丢失 HAL 错误的 SDK 保存返回值。失败不证明 VM 无副作用。
 */
d_err_t qwen_hal_storage_save_checked(void);
#endif
