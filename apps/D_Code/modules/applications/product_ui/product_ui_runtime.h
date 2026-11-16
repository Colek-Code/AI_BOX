/** @file product_ui_runtime.h @brief Product UI 内部周期、模块可用性与状态消费合同。 */

#ifndef PRODUCT_UI_RUNTIME_H
#define PRODUCT_UI_RUNTIME_H

#include "modules/applications/product_ui/product_ui.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

/**
 * @brief 复制当前规范化状态。
 * @param[out] status 接收值副本，NULL 时忽略。
 * @note 互斥量建立后受其保护；不会清除脏标志。
 */
void product_ui_status_snapshot(product_ui_status_t *status);
/**
 * @brief 原子取走并清除状态脏标志。
 * @return 上次状态内容是否曾变化的布尔值。
 * @note 互斥量建立前要求启动串行；多个消费者会相互消费此单一标志。
 */
uint8_t product_ui_status_take_dirty(void);
/**
 * @brief 查询模块注册表中是否存在指定且拥有页面的模块。
 * @param[in] module_id 借用的 NUL 结尾模块标识。
 * @return 存在可用页面返回非零；NULL 或未找到返回零。
 */
uint8_t product_ui_module_available(const char *module_id);
/**
 * @brief 在 UI 执行器中推进公共状态、方向事务、通知、控制中心及状态栏。
 * @param[in] elapsed_ms 本次 UI 周期经过的毫秒数。
 * @note 连接投影不依赖控制中心是否打开；维护期间仍推进已经受理的显示及保存事务，随后按维护门禁跳过普通通知。
 */
void product_ui_process(uint32_t elapsed_ms);

#endif
#endif /* PRODUCT_UI_RUNTIME_H */
