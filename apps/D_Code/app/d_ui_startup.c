/**
 * @file d_ui_startup.c
 * @brief Starts the selected root UI and its visible page modules in order.
 */

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "d_ui_startup.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include "framework/compat/d_stdio_compat.h"

#if D_UI_TARGET == D_UI_PRODUCT
#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/shell/app_registry/product_ui_app_registry.h"
#elif D_UI_TARGET == D_UI_TABLET_SHELL
#include "modules/diagnostics/display/tablet_shell/tablet_shell.h"
#elif D_UI_TARGET == D_UI_SCREEN_TEST
#include "modules/diagnostics/display/screen_test/screen_test_page.h"
#elif D_UI_TARGET == D_UI_CIPHER_PANEL
#include "modules/applications/legacy_ui/cipher_panel/cipher_panel_demo.h"
#elif D_UI_TARGET == D_UI_LAUNCHER_DEMO
#include "modules/diagnostics/display/launcher_demo/launcher_demo.h"
#elif D_UI_TARGET == D_UI_LIGHT_SPOT_DEMO
#include "modules/diagnostics/display/light_spot_demo/light_spot_demo.h"
#endif

#define LOG_TAG "[UI_START] "

#if (D_UI_TARGET == D_UI_CIPHER_PANEL) || (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/qwen_subtitle/qwen_subtitle_emoji_font_16.h"
#endif

typedef const d_ui_module_t *(*d_ui_module_descriptor_fn_t)(void);

#if D_FEATURE_AI_CHAT
D_UI_MODULE_DECLARE(ai_chat);
#endif
#if D_FEATURE_CAMERA && !D_CAMERA_PREVIEW_ENABLE
D_UI_MODULE_DECLARE(camera);
#endif
#if D_FEATURE_GALLERY && (D_UI_TARGET != D_UI_PRODUCT)
D_UI_MODULE_DECLARE(gallery);
#endif

static const d_ui_module_descriptor_fn_t s_ui_modules[] = {
#if D_FEATURE_AI_CHAT
    d_ui_module_descriptor_ai_chat,
#endif
#if D_FEATURE_CAMERA && !D_CAMERA_PREVIEW_ENABLE
    d_ui_module_descriptor_camera,
#endif
#if D_FEATURE_GALLERY && (D_UI_TARGET != D_UI_PRODUCT)
    d_ui_module_descriptor_gallery,
#endif
    NULL,
};

const char *d_ui_startup_name(void)
{
#if D_UI_TARGET == D_UI_PRODUCT
    return "Product UI";
#elif D_UI_TARGET == D_UI_TABLET_SHELL
    return "TabletShell";
#elif D_UI_TARGET == D_UI_SCREEN_TEST
    return "ScreenTest";
#elif D_UI_TARGET == D_UI_CIPHER_PANEL
    return "CipherPanel";
#elif D_UI_TARGET == D_UI_LAUNCHER_DEMO
    return "LauncherDemo";
#else
    return "LightSpotDemo";
#endif
}

static int d_ui_startup_register_root(void)
{
#if D_UI_TARGET == D_UI_PRODUCT
    return product_ui_register();
#elif D_UI_TARGET == D_UI_TABLET_SHELL
    return tablet_shell_register();
#elif D_UI_TARGET == D_UI_SCREEN_TEST
    return screen_test_page_register();
#elif D_UI_TARGET == D_UI_CIPHER_PANEL
    return cipher_panel_demo_register();
#elif D_UI_TARGET == D_UI_LAUNCHER_DEMO
    return launcher_demo_register();
#else
    return light_spot_demo_register();
#endif
}

static int d_ui_startup_register_modules(void)
{
    const d_ui_module_descriptor_fn_t *get_module = s_ui_modules;
#if D_UI_TARGET == D_UI_PRODUCT
    const d_ui_module_t *module;
    uint8_t index;
#endif

    while (*get_module != NULL) {
        if (d_ui_module_register((*get_module)()) < 0) {
            return -1;
        }
        get_module++;
    }
#if D_UI_TARGET == D_UI_PRODUCT
    for (index = 0U; index < product_ui_app_registry_count(); index++) {
        module = product_ui_app_registry_module(index);
        if ((module == NULL) || (d_ui_module_register(module) < 0)) {
            return -1;
        }
    }
#endif
    return 0;
}

static int d_ui_startup_prepare(void)
{
#if D_UI_TARGET == D_UI_PRODUCT
    return product_ui_prepare_pages();
#else
    return 0;
#endif
}

const lv_font_t *d_ui_startup_font_fallback_tail(void)
{
#if (D_UI_TARGET == D_UI_CIPHER_PANEL) || (D_UI_TARGET == D_UI_PRODUCT)
    return &qwen_subtitle_emoji_font_16;
#else
    return NULL;
#endif
}

d_err_t d_ui_startup_run(void)
{
    d_err_t init_result;

    d_ui_module_init();
    page_manager_init();

    printf(LOG_TAG "Registering root UI: %s\n", d_ui_startup_name());
    if (d_ui_startup_register_root() != 0) {
        printf(LOG_TAG "Root UI register failed: %s\n", d_ui_startup_name());
        return D_ERR_NOT_READY;
    }
    if (d_ui_startup_register_modules() != 0) {
        printf(LOG_TAG "UI module registration failed\n");
        return D_ERR_NOT_READY;
    }

    init_result = d_ui_module_init_all();
    if (init_result != D_OK) {
        printf(LOG_TAG "UI module initialization failed: %d\n", init_result);
        return init_result;
    }
    if (d_ui_startup_prepare() != 0) {
        printf(LOG_TAG "UI page prepare failed: %s\n", d_ui_startup_name());
        return D_ERR_NOT_READY;
    }
    if (page_manager_switch_to(PAGE_TYPE_MAIN,
                               PAGE_TRANSITION_IMMEDIATE) != 0) {
        printf(LOG_TAG "Main page switch failed\n");
        return D_ERR_NOT_READY;
    }

    printf(LOG_TAG "Page system initialized (%d modules loaded)\n",
           d_ui_module_get_count());
    return D_OK;
}

void d_ui_startup_update(uint32_t elapsed_ms)
{
    page_manager_update(elapsed_ms);
}

#endif /* D_LVGL_ENABLE */
