/** @file product_ui_ota_notice.h @brief 全局升级进度浮层的 UI 生命周期及导航阻断接口。 */
#ifndef PRODUCT_UI_OTA_NOTICE_H
#define PRODUCT_UI_OTA_NOTICE_H
#include <stdint.h>
/**
 * @brief 在 UI 线程预建全局升级浮层，再允许上层提交升级请求。
 * @return 已存在或完整创建返回 0，任一创建失败返回 -1 并删除局部根对象。
 * @note 成功后持有下拉占用并尝试保持屏幕常亮，底层设置页及草稿继续存在；这里只显示，不执行固件升级。
 */
int product_ui_ota_notice_prepare(void);
/**
 * @brief 仅在 OTA 维护未激活时撤销预建浮层。
 * @note 用于升级受理失败后的 UI 处理阶段；不能借此绕过在途维护保护。
 */
void product_ui_ota_notice_discard(void);
/**
 * @brief 在 UI 周期投影升级进度，只有后台及维护均结束后才允许关闭。
 * @note 准备被 BUSY 拒绝时可自动关闭并提示；底层未确认停止时保留浮层，不能把错误当作可重试。
 */
void product_ui_ota_notice_process(void);
/**
 * @brief 查询全局升级浮层是否存在并阻断普通导航。
 * @return 浮层存在返回非零，否则为零。
 */
uint8_t product_ui_ota_notice_blocking(void);
/**
 * @brief 从升级阶段编号选择静态说明。
 * @param[in] state OTA 服务状态编号。
 * @return 已定义阶段返回对应说明，超出表范围返回状态未知。
 */
const char *product_ui_ota_state_text(unsigned state);
#endif
