/**
 * @file tablet_shell_panel.h
 * @brief Control and settings overlays for the GC9307 tablet shell.
 */

#ifndef D_TABLET_SHELL_PANEL_H
#define D_TABLET_SHELL_PANEL_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_TABLET_SHELL)

#include "lvgl.h"

int tablet_shell_panel_create(lv_obj_t *parent, lv_event_cb_t gesture_cb);
uint8_t tablet_shell_panel_is_scrim(lv_obj_t *obj);
void tablet_shell_panel_show_control(uint8_t visible);
void tablet_shell_panel_toggle_settings(void);
void tablet_shell_panel_hide_all(void);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_TABLET_SHELL) */
#endif /* D_TABLET_SHELL_PANEL_H */
