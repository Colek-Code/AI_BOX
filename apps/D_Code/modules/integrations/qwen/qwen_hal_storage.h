/** @file qwen_hal_storage.h @brief 导出 SDK 注册凭据并核对对应存储边界。 */
#ifndef QWEN_HAL_STORAGE_H
#define QWEN_HAL_STORAGE_H
#include "framework/d_error.h"

/**
 * @brief 原槽模式写入并回读物理 VM16；独立证书模式仅写入并回读 RAM 运行副本。
 * @return 完整写入及回读一致返回 D_OK；并发调用返回 D_ERR_BUSY；License 未启用返回 D_ERR_NOT_READY；其余返回 D_ERR_IO。
 * @note 注册成功后由唯一工作任务调用。全托管独立存储的 Flash 写入与验证由 qwen_full_credential_commit 完成。
 */
d_err_t qwen_hal_storage_save_checked(void);
#endif
