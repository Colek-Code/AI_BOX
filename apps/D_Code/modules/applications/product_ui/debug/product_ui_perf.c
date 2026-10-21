/**
 * @file product_ui_perf.c
 * @brief Macro-controlled Product UI performance diagnostics.
 */

#include "config/app_config.h"
#include "modules/applications/product_ui/debug/product_ui_perf.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"
#include "system/includes.h"

#if D_DEBUG_UI_PERFORMANCE
#include "platform/screen/profiles/gc9307_hyn/lcd_gc9307.h"

#define PRODUCT_UI_PERF_LOG_MS     2000U

static uint32_t s_perf_accum_ms;
static uint32_t s_perf_max_loop_ms;
static uint32_t s_perf_max_update_ms;
static uint32_t s_perf_max_render_ms;

void product_ui_perf_cycle(uint32_t update_ms, uint32_t render_ms)
{
    s_perf_max_update_ms = LV_MAX(s_perf_max_update_ms, update_ms);
    s_perf_max_render_ms = LV_MAX(s_perf_max_render_ms, render_ms);
}

void product_ui_perf_tick(uint32_t elapsed_ms)
{
    lv_mem_monitor_t monitor;
    d_lcd_gc9307_transfer_stats_t transfer;

    if (elapsed_ms > s_perf_max_loop_ms) {
        s_perf_max_loop_ms = elapsed_ms;
    }
    s_perf_accum_ms += elapsed_ms;
    if (s_perf_accum_ms < PRODUCT_UI_PERF_LOG_MS) {
        return;
    }

    lv_mem_monitor(&monitor);
    d_lcd_gc9307_get_transfer_stats(&transfer);
    printf("[ProductUI] perf loop_interval_max=%u update_max=%u render_max=%u "
           "dma_completed=%u dma_failed=%u free=%u frag=%u%%\n",
           s_perf_max_loop_ms, s_perf_max_update_ms, s_perf_max_render_ms,
           transfer.complete_count, transfer.failure_count, monitor.free_size, monitor.frag_pct);
    s_perf_accum_ms %= PRODUCT_UI_PERF_LOG_MS;
    s_perf_max_loop_ms = 0;
    s_perf_max_update_ms = 0U;
    s_perf_max_render_ms = 0U;
}

void product_ui_perf_page_created(const char *name, uint32_t started_at_ms)
{
    if (name != NULL) {
        printf("[ProductUI] page=%s create_ms=%u\n",
               name, lv_tick_get() - started_at_ms);
    }
}

#else

void product_ui_perf_cycle(uint32_t update_ms, uint32_t render_ms)
{
    (void)update_ms;
    (void)render_ms;
}

void product_ui_perf_tick(uint32_t elapsed_ms)
{
    (void)elapsed_ms;
}

void product_ui_perf_page_created(const char *name, uint32_t started_at_ms)
{
    (void)name;
    (void)started_at_ms;
}

#endif /* D_DEBUG_UI_PERFORMANCE */
#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
