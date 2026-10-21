/**
 * @file product_ui.c
 * @brief Product UI main-page registration anchor.
 */

#include "config/app_config.h"
#include "modules/applications/product_ui/features/time/product_ui_pomodoro_notice.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_notice.h"
#if D_FEATURE_OTA
#include "modules/applications/product_ui/features/settings/product_ui_ota_notice.h"
#include "modules/services/ota/ota_service.h"
#endif

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/shared/motion/product_ui_motion.h"
#include "modules/applications/product_ui/shell/home/product_ui_home.h"
#include "modules/applications/product_ui/debug/product_ui_perf.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain.h"
#include "modules/applications/product_ui/features/time/product_ui_clock.h"
#include "modules/applications/product_ui/features/media/product_ui_media_storage.h"
#include "modules/applications/product_ui/features/media/product_ui_media_control.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"
#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "modules/applications/product_ui/debug/product_ui_visual_capture.h"
#if (D_BOARD_IS_GC9307)
#include "modules/applications/product_ui/adapters/gc9307/product_ui_gc9307_adapter.h"
#include "platform/screen/power/d_screen_power.h"
#if (D_FEATURE_WIFI)
#endif
#endif
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include "os/os_api.h"
#include "system/includes.h"
#include "lvgl.h"
#include <string.h>

static OS_MUTEX s_status_mutex;
static uint8_t s_status_mutex_ready;
static uint8_t s_status_dirty;
/* Capabilities become visible only after a real service publishes them. */
static product_ui_status_t s_status;

#define PRODUCT_UI_NAVIGATION_DEPTH 8U

static int s_navigation_stack[PRODUCT_UI_NAVIGATION_DEPTH];
static uint8_t s_navigation_depth;

static int product_ui_find_module_page(const char *module_id)
{
    const d_ui_module_t *module;
    int index;

    for (index = 0; index < d_ui_module_get_count(); index++) {
        module = d_ui_module_get(index);
        if ((module != NULL) && (module->id != NULL) &&
            (strcmp(module->id, module_id) == 0) &&
            d_ui_module_has_page(module)) {
            return d_ui_module_get_page_type(index);
        }
    }
    return PAGE_TYPE_INVALID;
}

static void product_ui_main_enter(void)
{
    product_ui_navigation_reset();
    product_ui_home_enter();
}

static const page_ops_t s_product_ui_ops = {
    .create = product_ui_home_create,
    .enter = product_ui_main_enter,
    .exit = product_ui_home_exit,
    .update = product_ui_home_update,
    .flags = PAGE_FLAG_KEEP_ALIVE,
};

static const page_desc_t s_product_ui_desc = {
    .page_type = PAGE_TYPE_MAIN,
    .name = "ProductUI",
    .ops = &s_product_ui_ops,
};

static int product_ui_status_runtime_init(void)
{
#if !(D_BOARD_IS_GC9307)
    if (product_ui_service_publish(
            PRODUCT_UI_SERVICE_TIME, PRODUCT_UI_SERVICE_UNAVAILABLE,
            0U, 0) != 0) {
        return -1;
    }
#endif
    if (!s_status_mutex_ready) {
        if (os_mutex_create(&s_status_mutex) != OS_NO_ERR) {
            return -1;
        }
        s_status_mutex_ready = 1U;
    }
#if (D_BOARD_IS_GC9307)
    if (product_ui_status_gc9307_init() != 0) {
        return -1;
    }
#endif
    s_status_dirty = 1U;
    return 0;
}

int product_ui_register(void)
{
    product_ui_navigation_reset();
    if (product_ui_theme_init() != 0) {
        return -1;
    }
    if (product_ui_visual_capture_init() != 0) {
        return -1;
    }
    if (product_ui_service_registry_init() != 0) {
        return -1;
    }
    product_ui_time_domain_init();
    (void)product_ui_clock_init();
    if (product_ui_media_storage_init() != D_OK) {
        return -1;
    }
    if (product_ui_settings_service_init() != 0) {
        return -1;
    }
    /* A storage/worker failure only disables saves; the desktop still opens. */
    (void)product_ui_appearance_init();
#if (D_BOARD_IS_GC9307)
    if (product_ui_settings_gc9307_init() != 0) {
        return -1;
    }
#if (D_FEATURE_WIFI)
    /*
     * Connection is independent of Settings. Failure only marks CONNECTION
     * UNAVAILABLE — never rolls back Settings or aborts Product UI.
     */
    if (product_ui_connection_gc9307_init() != 0) {
        (void)product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                         PRODUCT_UI_SERVICE_UNAVAILABLE, 0,
                                         -1);
    }
#else
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                                     PRODUCT_UI_SERVICE_UNAVAILABLE, 0, 0);
#endif
#else
    product_ui_service_publish(PRODUCT_UI_SERVICE_SETTINGS,
                               PRODUCT_UI_SERVICE_UNAVAILABLE, 0, 0);
    product_ui_service_publish(PRODUCT_UI_SERVICE_CONNECTION,
                               PRODUCT_UI_SERVICE_UNAVAILABLE, 0, 0);
#endif
    product_ui_service_publish(PRODUCT_UI_SERVICE_EVENT,
                               PRODUCT_UI_SERVICE_READY, 0, 0);
    product_ui_service_publish(PRODUCT_UI_SERVICE_ALARM,
                               PRODUCT_UI_SERVICE_UNAVAILABLE, 0, 0);
    product_ui_service_publish(PRODUCT_UI_SERVICE_STORAGE,
                               PRODUCT_UI_SERVICE_UNAVAILABLE, 0, 0);
    product_ui_service_publish(PRODUCT_UI_SERVICE_MEDIA,
                               PRODUCT_UI_SERVICE_UNAVAILABLE, 0, 0);
    /* Registers only when a real audio backend exists; no-op today. */
    (void)product_ui_media_control_try_register();
    if (product_ui_status_runtime_init() != 0) {
        return -1;
    }
    return page_manager_register(&s_product_ui_desc);
}

int product_ui_prepare_pages(void)
{
    int page_type;

    page_type = product_ui_find_module_page("product_settings");
    if (page_type == PAGE_TYPE_INVALID) {
        PRODUCT_UI_TRACE("prepare_pages settings=unavailable\n");
        return -1;
    }
    return 0;
}

static int product_ui_switch_to_page(int page_type, uint8_t is_back)
{
    int result;
    /* page_manager_switch_to owns target validation and preparation. */
    product_ui_control_center_close_for_page_transition();
    product_ui_feedback_clear();
    product_ui_motion_cancel_tree(page_manager_get_current_container());
    result = page_manager_switch_to(page_type, PAGE_TRANSITION_TE_FRAME);
    product_ui_motion_apply_tree(page_manager_get_current_container());
    if (result == 0) product_ui_page_enter(page_manager_get_current_container(), is_back);
    return result;
}

void product_ui_navigation_reset(void)
{
    memset(s_navigation_stack, 0, sizeof(s_navigation_stack));
    s_navigation_depth = 0U;
}

int product_ui_navigate_to(const char *module_id)
{
    int current_page;
    int page_type;

    if (product_ui_alarm_notice_blocking()) return -1;
#if D_FEATURE_OTA
    if (product_ui_ota_notice_blocking() || ota_service_maintenance_active()) return -1;
#endif

    if ((module_id == NULL) || (module_id[0] == '\0')) {
        return -1;
    }

    page_type = product_ui_find_module_page(module_id);
    if (page_type == PAGE_TYPE_INVALID) {
        PRODUCT_UI_TRACE("open_module id=%s result=unavailable\n", module_id);
        return -1;
    }
    current_page = page_manager_get_current();
    if (current_page == page_type) {
        product_ui_control_center_close_for_page_transition();
        return 0;
    }
    if (s_navigation_depth >= PRODUCT_UI_NAVIGATION_DEPTH) {
        PRODUCT_UI_TRACE("open_module id=%s result=stack_full\n", module_id);
        return -2;
    }
    PRODUCT_UI_TRACE("open_module id=%s page=%d result=switch\n",
                     module_id, page_type);
    if (product_ui_switch_to_page(page_type, 0U) != 0) {
        PRODUCT_UI_TRACE("open_module id=%s page=%d result=switch_failed\n",
                         module_id, page_type);
        return -1;
    }
    s_navigation_stack[s_navigation_depth] = current_page;
    s_navigation_depth++;
    return 0;
}

int product_ui_navigation_back(void)
{
    int target_page;

    if (product_ui_alarm_notice_blocking()) return -1;
#if D_FEATURE_OTA
    if (product_ui_ota_notice_blocking() || ota_service_maintenance_active()) return -1;
#endif

    if (page_manager_get_current() == PAGE_TYPE_MAIN) {
        product_ui_navigation_reset();
        return 0;
    }
    target_page = s_navigation_depth > 0U ?
                  s_navigation_stack[s_navigation_depth - 1U] :
                  PAGE_TYPE_MAIN;
    if (product_ui_switch_to_page(target_page, 1U) != 0) {
        return -1;
    }
    if (target_page == PAGE_TYPE_MAIN) {
        product_ui_navigation_reset();
    } else if (s_navigation_depth > 0U) {
        s_navigation_depth--;
    }
    return 0;
}

int product_ui_open_module(const char *module_id)
{
    return product_ui_navigate_to(module_id);
}

product_ui_status_result_e product_ui_status_update(
    const product_ui_status_t *status)
{
    product_ui_status_t next;
    product_ui_status_result_e result;

    result = product_ui_status_normalize(status, &next);
    if (result == PRODUCT_UI_STATUS_INVALID_ARGUMENT) {
        return result;
    }

    if (s_status_mutex_ready) {
        os_mutex_pend(&s_status_mutex, 0);
    }
    if (memcmp(&s_status, &next, sizeof(next)) != 0) {
        s_status = next;
        s_status_dirty = 1U;
    }
    if (s_status_mutex_ready) {
        os_mutex_post(&s_status_mutex);
    }
    return result;
}

void product_ui_status_snapshot(product_ui_status_t *status)
{
    if (status == NULL) {
        return;
    }
    if (s_status_mutex_ready) {
        os_mutex_pend(&s_status_mutex, 0);
    }
    *status = s_status;
    if (s_status_mutex_ready) {
        os_mutex_post(&s_status_mutex);
    }
}

uint8_t product_ui_status_take_dirty(void)
{
    uint8_t is_dirty;

    if (s_status_mutex_ready) {
        os_mutex_pend(&s_status_mutex, 0);
    }
    is_dirty = s_status_dirty;
    s_status_dirty = 0;
    if (s_status_mutex_ready) {
        os_mutex_post(&s_status_mutex);
    }
    return is_dirty;
}

uint8_t product_ui_module_available(const char *module_id)
{
    if (module_id == NULL) {
        return 0;
    }
    return product_ui_find_module_page(module_id) != PAGE_TYPE_INVALID;
}

void product_ui_process(uint32_t elapsed_ms)
{
    product_ui_status_t status;
    uint8_t refresh_required;
#if (D_BOARD_IS_GC9307) && (D_FEATURE_WIFI)
    /* Connection producer: independent of control-center open state. */
    static uint32_t s_connection_sync_elapsed_ms;
    const uint32_t connection_sync_period_ms = 200U;
#endif

#if (D_BOARD_IS_GC9307)
    d_screen_power_update(elapsed_ms);
    product_ui_status_gc9307_process(elapsed_ms);
    product_ui_settings_gc9307_process();
#if (D_FEATURE_WIFI)
    /*
     * Publish CONNECTION snapshot on a short period so Settings and other
     * consumers are not stale when the control center is closed. Adapter
     * itself skips full wifi_service_get_status when version is unchanged.
     */
    s_connection_sync_elapsed_ms += elapsed_ms;
    if (s_connection_sync_elapsed_ms >= connection_sync_period_ms) {
        s_connection_sync_elapsed_ms = 0U;
        product_ui_connection_service_sync();
    }
#endif
#endif
    product_ui_motion_process(elapsed_ms);
    product_ui_perf_tick(elapsed_ms);
#if D_FEATURE_OTA
    product_ui_ota_notice_process();
    if (ota_service_maintenance_active()) return;
#endif
    product_ui_alarm_notice_process();
#if D_FEATURE_OTA
    /* After maintenance releases, a due alarm takes precedence over the OTA
     * result panel. The underlying settings page remains available afterward. */
    if (product_ui_alarm_notice_blocking()) product_ui_ota_notice_discard();
#endif
    product_ui_control_center_refresh();
    product_ui_pomodoro_notice_process();
    refresh_required = product_ui_status_take_dirty();
    if (!refresh_required) {
        return;
    }
    product_ui_status_snapshot(&status);
    product_ui_status_bar_refresh(&status);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
