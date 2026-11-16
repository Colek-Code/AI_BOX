/** @file product_ui_calendar_page.h @brief 日历会话控制器，结构变更只在页面更新阶段执行。 */

#ifndef PRODUCT_UI_CALENDAR_PAGE_H
#define PRODUCT_UI_CALENDAR_PAGE_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

/**
 * @brief 注册静态万年历页面描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
int product_ui_calendar_register_page(int page_type);
/**
 * @brief 创建日历页面及当前查询视图，首次无可信今天时只使用浏览锚点。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 完整创建返回 0，空父对象或创建失败返回 -1，临时视图状态通过回退路径清理。
 * @note 首页进入视为新访问；从临时设置页返回时保留控制器拥有的查询日期及独立滚动位置。浏览锚点不代表真实今天。
 */
int product_ui_calendar_page_create(lv_obj_t *parent);

#endif
#endif /* PRODUCT_UI_CALENDAR_PAGE_H */
