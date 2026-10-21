/**
 * @file d_ui_module.h
 * @brief Static UI page descriptors and registration API.
 * @note app/d_ui_startup.c owns the visible registration order.
 */

#ifndef D_UI_MODULE_H
#define D_UI_MODULE_H

#include "config/app_config.h"
#include "d_error.h"

#ifdef D_LVGL_ENABLE

#define D_MODULE_MAX_COUNT 24

typedef struct d_ui_module {
    const char *id;
    const char *display_name;
    const char *icon;
    int (*register_page)(int page_type);
    d_err_t (*init)(void);
} d_ui_module_t;

#define D_UI_MODULE_DEFINE_ID(symbol, id_text, disp_name, icon_sym, reg_func, init_func) \
    static const d_ui_module_t s_d_ui_module_##symbol = { \
        .id = id_text, \
        .display_name = disp_name, \
        .icon = icon_sym, \
        .register_page = reg_func, \
        .init = init_func, \
    }; \
    const d_ui_module_t *d_ui_module_descriptor_##symbol(void) \
    { \
        return &s_d_ui_module_##symbol; \
    }

#define D_UI_MODULE_DEFINE(name, disp_name, icon_sym, reg_func, init_func) \
    D_UI_MODULE_DEFINE_ID(name, #name, disp_name, icon_sym, reg_func, init_func)

#define D_UI_MODULE_DECLARE(name) \
    const d_ui_module_t *d_ui_module_descriptor_##name(void)

void d_ui_module_init(void);
int d_ui_module_register(const d_ui_module_t *module);
int d_ui_module_get_count(void);
const d_ui_module_t *d_ui_module_get(int index);
int d_ui_module_get_page_type(int index);
int d_ui_module_has_page(const d_ui_module_t *module);
d_err_t d_ui_module_init_all(void);

#endif /* D_LVGL_ENABLE */
#endif /* D_UI_MODULE_H */
