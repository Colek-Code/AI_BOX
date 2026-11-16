/** @file product_ui_schedule_page.h @brief 共享日程值驱动的有限列表，页面只保存排序及视口副本。 */

#ifndef PRODUCT_UI_SCHEDULE_PAGE_H
#define PRODUCT_UI_SCHEDULE_PAGE_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

/**
 * @brief 注册静态日程表页面描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
int product_ui_schedule_register_page(int page_type);
/**
 * @brief 创建日程框架并从真实事件服务呈现排序列表或反馈。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 成功返回 0，空父对象、框架或内容失败返回 -1，部分对象交管理器清理。
 * @note 记录只在本次启动 RAM 内有效；页面销毁不清除时间域日程。
 */
int product_ui_schedule_page_create(lv_obj_t *parent);

#endif
#endif /* PRODUCT_UI_SCHEDULE_PAGE_H */
