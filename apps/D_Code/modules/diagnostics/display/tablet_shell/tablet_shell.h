/**
 * @file tablet_shell.h
 * @brief GC9307 landscape tablet shell page.
 */

#ifndef D_TABLET_SHELL_H
#define D_TABLET_SHELL_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_TABLET_SHELL)

int tablet_shell_register(void);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_TABLET_SHELL) */
#endif /* D_TABLET_SHELL_H */
