/** @file product_ui_alarm_page.h @brief 闹钟页面草稿所有权与延后命令，实例及保存仍由运行时持有。 */

#ifndef PRODUCT_UI_ALARM_PAGE_H
#define PRODUCT_UI_ALARM_PAGE_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

/**
 * @brief 注册静态闹钟页面描述。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
int product_ui_alarm_register_page(int page_type);
/**
 * @brief 创建框架及当前闹钟子视图，从首页进入重建会话，临时返回保留草稿。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 成功返回 0，空父对象或创建失败返回 -1，失败清除视图引用并交管理器清理框架。
 * @note 页面不拥有后台实例或保存任务；临时访问时间设置返回时恢复对应模式视口。
 */
int product_ui_alarm_page_create(lv_obj_t *parent);

#endif
#endif /* PRODUCT_UI_ALARM_PAGE_H */
