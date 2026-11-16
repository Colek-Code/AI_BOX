/**
 * @file d_ui_startup.h
 * @brief 声明根界面装配、页面注册和周期更新入口，不持有业务资源。
 */

#ifndef D_UI_STARTUP_H
#define D_UI_STARTUP_H

#include "config/app_config.h"
#include "framework/d_error.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

/**
 * @brief 读取编译期所选根界面的诊断名称。
 * @return 静态只读字符串，调用方不得释放。
 */
const char *d_ui_startup_name(void);
/**
 * @brief 初始化注册表、注册根页面与模块，并同步进入主页面。
 * @return D_OK 表示页面装配及主页面切换成功；否则为初始化错误或 D_ERR_NOT_READY。
 * @note 仅在 UI/LVGL 线程的启动阶段调用，不是运行期重置接口。失败不代表所有先前初始化项均已回滚。
 * @note 成功提交页面生命周期，不代表显示 DMA 已完成；显示完成由平台接口确认。
 */
d_err_t d_ui_startup_run(void);
/**
 * @brief 取得当前根界面的附加字体回退尾节点。
 * @return 静态字体描述符；不需要附加尾节点的界面返回 NULL。
 */
const lv_font_t *d_ui_startup_font_fallback_tail(void);
/**
 * @brief 在 UI 线程更新当前页面。
 * @param[in] elapsed_ms 本轮经过的毫秒数，传给页面更新入口。
 * @note 仅由 UI 线程调用。
 */
void d_ui_startup_update(uint32_t elapsed_ms);

#endif /* D_LVGL_ENABLE */
#endif /* D_UI_STARTUP_H */
