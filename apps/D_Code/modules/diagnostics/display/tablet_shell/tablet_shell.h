/**
 * @file tablet_shell.h
 * @brief GC9307 横屏平板外壳演示页注册入口。
 */

#ifndef D_TABLET_SHELL_H
#define D_TABLET_SHELL_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_TABLET_SHELL)

/**
 * @brief 将GC9307 横屏平板外壳演示页描述注册到页面管理器。
 * @return 页面管理器的注册结果，0 成功，负值失败。
 * @note 仅注册描述，不创建页面对象；实际页面由页面管理器按需创建。
 */
int tablet_shell_register(void);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_TABLET_SHELL) */
#endif /* D_TABLET_SHELL_H */
