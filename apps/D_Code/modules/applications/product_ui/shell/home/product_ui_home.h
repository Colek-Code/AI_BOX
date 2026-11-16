/** @file product_ui_home.h @brief Product UI 首页壁纸、有限应用槽、横向分页及统一导航。 */

#ifndef PRODUCT_UI_HOME_H
#define PRODUCT_UI_HOME_H

#include "lvgl.h"
#include <stdint.h>

/**
 * @brief 创建首页框架、已确认壁纸及最多两页六格应用，绑定横向分页。
 * @param[in,out] parent 页面管理器拥有的根容器。
 * @return 完整创建返回 0，空父对象或必要对象失败返回 -1；部分树交页面管理器删除。
 * @note 第二层创建失败时保留单层重绑回退，首页页数仍由静态应用目录决定；所有状态只在 UI 线程使用。
 */
int product_ui_home_create(lv_obj_t *parent);
/**
 * @brief 把外观服务最新确认的壁纸应用到现有背景，未变时不重绑。
 * @note 不把待保存目标当作已确认壁纸，不执行存储 IO。
 */
void product_ui_home_enter(void);
/**
 * @brief 退出时收敛到起始或已预约页面，撤销所有应用槽的旧按压资格。
 * @note 只收敛 UI 状态，不取消对应业务模块的后台工作。
 */
void product_ui_home_exit(void);
/**
 * @brief 推进公共 UI 与壁纸，分页静止时更新提醒标记和真实模块可用性。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 * @note 拖动或吸附动画期间不重绑应用槽，避免把活跃手势移给其他条目。
 */
void product_ui_home_update(uint32_t elapsed_ms);

#endif /* PRODUCT_UI_HOME_H */
