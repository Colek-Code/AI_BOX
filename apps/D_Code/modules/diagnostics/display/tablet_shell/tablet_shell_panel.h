/**
 * @file tablet_shell_panel.h
 * @brief 横屏平板外壳的控制与设置覆盖面板接口。
 */

#ifndef D_TABLET_SHELL_PANEL_H
#define D_TABLET_SHELL_PANEL_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_TABLET_SHELL)

#include "lvgl.h"

/**
 * @brief 创建遮罩、控制中心和设置面板。
 * @param[in,out] parent 非空页面父容器。
 * @param[in] gesture_cb 非空面板事件回调，随对象使用期间持续有效。
 * @return 成功返回 0；参数或创建失败返回 -1。
 * @note 在既有 LVGL 调用上下文中使用；失败清引用，已创建对象随父容器清理。
 */
int tablet_shell_panel_create(lv_obj_t *parent, lv_event_cb_t gesture_cb);
/**
 * @brief 按对象身份检查是否为当前面板遮罩。
 * @param[in] obj 待比较对象指针。
 * @return 与当前遮罩指针相同返回 1，否则为 0；均为空时也相同。
 */
uint8_t tablet_shell_panel_is_scrim(lv_obj_t *obj);
/**
 * @brief 以竖向动画显示或隐藏控制中心，并同步遮罩。
 * @param[in] visible 非零显示，0 隐藏。
 * @note 显示时隐藏设置面板；隐藏动画结束后才隐藏控制中心对象。
 */
void tablet_shell_panel_show_control(uint8_t visible);
/**
 * @brief 切换设置面板可见性，显示设置时关闭控制中心。
 */
void tablet_shell_panel_toggle_settings(void);
/**
 * @brief 隐藏设置面板并请求控制中心收起动画。
 */
void tablet_shell_panel_hide_all(void);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_TABLET_SHELL) */
#endif /* D_TABLET_SHELL_PANEL_H */
