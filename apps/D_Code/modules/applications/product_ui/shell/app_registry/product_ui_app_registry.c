/**
 * @file product_ui_app_registry.c
 * @brief Explicit manifest for every Product UI page module.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shell/app_registry/product_ui_app_registry.h"

D_UI_MODULE_DECLARE(product_settings);
D_UI_MODULE_DECLARE(calculator);
D_UI_MODULE_DECLARE(pomodoro);
D_UI_MODULE_DECLARE(calendar);
D_UI_MODULE_DECLARE(alarm);
D_UI_MODULE_DECLARE(schedule);
D_UI_MODULE_DECLARE(music);
D_UI_MODULE_DECLARE(product_gallery);
D_UI_MODULE_DECLARE(product_ai_chat);
#if D_CAMERA_PREVIEW_ENABLE
D_UI_MODULE_DECLARE(product_camera);
#endif
D_UI_MODULE_DECLARE(product_settings_wifi);
D_UI_MODULE_DECLARE(product_settings_bluetooth);
D_UI_MODULE_DECLARE(product_settings_display);
D_UI_MODULE_DECLARE(product_settings_sound);
D_UI_MODULE_DECLARE(product_settings_wallpaper);
D_UI_MODULE_DECLARE(product_settings_time);
#if D_FEATURE_OTA
D_UI_MODULE_DECLARE(product_settings_ota);
#endif

#define PRODUCT_UI_HOME_ENTRY(module, order, icon, accent) \
    {d_ui_module_descriptor_##module, 1U, order, icon, accent}
#define PRODUCT_UI_DETAIL_ENTRY(module) \
    {d_ui_module_descriptor_##module, 0U, 0U, PRODUCT_UI_ICON_COUNT, \
     PRODUCT_UI_COLOR_TEXT}

static const product_ui_app_entry_t s_product_ui_apps[] = {
    PRODUCT_UI_HOME_ENTRY(music, 0U, PRODUCT_UI_ICON_MUSIC,
                          PRODUCT_UI_COLOR_DANGER),
    PRODUCT_UI_HOME_ENTRY(calendar, 1U, PRODUCT_UI_ICON_CALENDAR,
                          PRODUCT_UI_COLOR_ACCENT),
    PRODUCT_UI_HOME_ENTRY(calculator, 2U, PRODUCT_UI_ICON_CALCULATOR,
                          PRODUCT_UI_COLOR_TEXT),
    PRODUCT_UI_HOME_ENTRY(product_gallery, 3U, PRODUCT_UI_ICON_GALLERY,
                          PRODUCT_UI_COLOR_ACCENT),
    PRODUCT_UI_HOME_ENTRY(alarm, 4U, PRODUCT_UI_ICON_ALARM,
                          PRODUCT_UI_COLOR_WARNING),
    PRODUCT_UI_HOME_ENTRY(pomodoro, 5U, PRODUCT_UI_ICON_TIMER,
                          PRODUCT_UI_COLOR_ACCENT),
    PRODUCT_UI_HOME_ENTRY(schedule, 6U, PRODUCT_UI_ICON_SCHEDULE,
                          PRODUCT_UI_COLOR_SUCCESS),
    PRODUCT_UI_HOME_ENTRY(product_ai_chat, 7U, PRODUCT_UI_ICON_AI_CHAT,
                          PRODUCT_UI_COLOR_ACCENT),
#if D_CAMERA_PREVIEW_ENABLE
    PRODUCT_UI_HOME_ENTRY(product_camera, 8U, PRODUCT_UI_ICON_CAMERA,
                          PRODUCT_UI_COLOR_ACCENT),
    PRODUCT_UI_HOME_ENTRY(product_settings, 9U, PRODUCT_UI_ICON_SETTINGS,
#else
    PRODUCT_UI_HOME_ENTRY(product_settings, 8U, PRODUCT_UI_ICON_SETTINGS,
#endif
                          PRODUCT_UI_COLOR_ACCENT),
    PRODUCT_UI_DETAIL_ENTRY(product_settings_wifi),
    PRODUCT_UI_DETAIL_ENTRY(product_settings_bluetooth),
    PRODUCT_UI_DETAIL_ENTRY(product_settings_display),
    PRODUCT_UI_DETAIL_ENTRY(product_settings_sound),
    PRODUCT_UI_DETAIL_ENTRY(product_settings_wallpaper),
    PRODUCT_UI_DETAIL_ENTRY(product_settings_time),
#if D_FEATURE_OTA
    PRODUCT_UI_DETAIL_ENTRY(product_settings_ota),
#endif
};

uint8_t product_ui_app_registry_count(void)
{
    return (uint8_t)(sizeof(s_product_ui_apps) / sizeof(s_product_ui_apps[0]));
}

const product_ui_app_entry_t *product_ui_app_registry_get(uint8_t index)
{
    if (index >= product_ui_app_registry_count()) {
        return NULL;
    }
    return &s_product_ui_apps[index];
}

const d_ui_module_t *product_ui_app_entry_module(
    const product_ui_app_entry_t *entry)
{
    if ((entry == NULL) || (entry->descriptor == NULL)) {
        return NULL;
    }
    return entry->descriptor();
}

const d_ui_module_t *product_ui_app_registry_module(uint8_t index)
{
    return product_ui_app_entry_module(product_ui_app_registry_get(index));
}

uint8_t product_ui_app_registry_home_count(void)
{
    uint8_t count = 0U;
    uint8_t index;

    for (index = 0U; index < product_ui_app_registry_count(); index++) {
        if (s_product_ui_apps[index].home_visible != 0U) {
            count++;
        }
    }
    return count;
}

const product_ui_app_entry_t *product_ui_app_registry_home_get(uint8_t order)
{
    uint8_t index;

    for (index = 0U; index < product_ui_app_registry_count(); index++) {
        if ((s_product_ui_apps[index].home_visible != 0U) &&
            (s_product_ui_apps[index].home_order == order)) {
            return &s_product_ui_apps[index];
        }
    }
    return NULL;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
