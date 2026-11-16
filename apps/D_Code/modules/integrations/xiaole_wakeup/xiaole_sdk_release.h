/** @file xiaole_sdk_release.h @brief 两个小乐库任务退出前保留缓冲的受检释放合同。 */
#ifndef XIAOLE_SDK_RELEASE_H
#define XIAOLE_SDK_RELEASE_H

#include "framework/d_error.h"
#include <stdint.h>

/**
 * @brief 在释放阶段把小乐库缓冲登记到有界延迟释放表。
 * @param[in] memory 待释放内存；为空时无操作。
 * @note 仅构建适配器将固定 WakeupApi.o 绑定到此入口，非全局分配钩子。未处于延迟释放阶段时直接 free；重复或溢出锁存错误并保留内存。
 */
void xiaole_sdk_free(void *memory);
/**
 * @brief 由小乐资源所有者在 lx_aec_free 前开始延迟释放阶段。
 * @return 建立成功返回 D_OK；旧阶段仍活动返回 D_ERR_BUSY；已锁存错误时返回该错误。
 * @note 不得由 SDK 回调启动。
 */
d_err_t xiaole_sdk_release_begin(void);
/**
 * @brief 在 lx_aec_free 已成功等待两个内部工作任务退出后释放登记缓冲。
 * @param[out] released 可选释放数量输出；开始置 0，完整成功后写入数量。
 * @return 完整释放返回 D_OK；未开始返回 D_ERR_NOT_READY；重复或表满等锁存错误直接返回并保留内存。
 * @note 所有者必须先证明两个库任务都已退出；free 在自旋锁外执行。
 */
d_err_t xiaole_sdk_release_complete(uint16_t *released);

#endif
