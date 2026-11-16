/** @file product_ui_calculator_page.h @brief 计算器键盘、结果面板及页面注册；算术状态由模型持有。 */

#ifndef __PRODUCT_UI_CALCULATOR_PAGE_H__
#define __PRODUCT_UI_CALCULATOR_PAGE_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

/**
 * @brief 创建计算器页面框架、显示面板和二十键网格并初始化模型。
 * @param[in,out] parent 页面管理器拥有的父对象。
 * @return 完整创建返回 0；任一创建失败返回 -1，部分对象由页面管理器清理。
 */
int product_ui_calculator_page_create(lv_obj_t *parent);
/**
 * @brief 把静态计算器页面描述注册到页面管理器。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
int product_ui_calculator_register_page(int page_type);

#endif
#endif /* __PRODUCT_UI_CALCULATOR_PAGE_H__ */
