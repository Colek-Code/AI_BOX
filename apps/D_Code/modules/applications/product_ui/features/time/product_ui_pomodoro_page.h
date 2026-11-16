/** @file product_ui_pomodoro_page.h @brief 番茄钟命令及延后视图切换；后台计时与保存持续超出页面寿命。 */
#ifndef PRODUCT_UI_POMODORO_PAGE_H
#define PRODUCT_UI_POMODORO_PAGE_H
#include "modules/applications/product_ui/product_ui.h"
/**
 * @brief 创建番茄钟框架、计时视图及设置／统计导航，使用服务值初始化草稿。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 完整创建返回 0，任一步失败返回 -1，部分对象交页面树清理。
 */
int product_ui_pomodoro_page_create(lv_obj_t *parent);
/**
 * @brief 注册静态番茄钟页面描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
int product_ui_pomodoro_register_page(int page_type);
/**
 * @brief 在 UI 线程预约返回计时子视图，由页面处理阶段执行。
 * @note 页面未创建或已经在计时视图时忽略；从设置离开仍经过未保存草稿保护。
 */
void product_ui_pomodoro_page_show_timer(void);
#endif
