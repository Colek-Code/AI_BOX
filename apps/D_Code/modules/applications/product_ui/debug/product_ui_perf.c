/** @file product_ui_perf.c @brief 按宏启用的 UI 聚合性能观察，区分墙钟跨度、等待与实际 worker 统计。 */

#include "config/app_config.h"
#include "modules/applications/product_ui/debug/product_ui_perf.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"
#include "system/includes.h"
#include <string.h>

#if D_DEBUG_UI_PERFORMANCE
#include "framework/d_page_manager.h"
#include "framework/d_lvgl_profile.h"
#include "platform/screen/profiles/gc9307_hyn/lcd_gc9307.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307.h"

#define PRODUCT_UI_PERF_LOG_MS     2000U

typedef enum {
    PRODUCT_UI_PERF_IDLE = 0,
    PRODUCT_UI_PERF_PREPARE,
    PRODUCT_UI_PERF_DRAW,
    PRODUCT_UI_PERF_TAIL
} product_ui_perf_phase_e;

typedef struct {
    product_ui_perf_phase_e phase;
    uint32_t started_ms;
    uint32_t draw_started_ms;
    uint32_t pre_ms;
    uint32_t draw_ms;
    uint32_t refresh_ms;
    uint32_t rendered;
    uint32_t errors;
} product_ui_perf_frame_t;

typedef struct {
    uint32_t ended_ms;
    uint32_t update_ms;
    uint32_t wait_ms;
    uint32_t wait_calls;
    uint32_t submitted;
    uint32_t ready;
    uint8_t wait_valid;
    d_lvgl_profile_t work;
} product_ui_perf_cycle_t;

static product_ui_perf_frame_t s_perf_frame;
static product_ui_perf_frame_t s_perf_peak_frame;
static product_ui_perf_cycle_t s_perf_peak_cycle;
static gc9307_ui_wait_stats_t s_perf_previous_wait;
static uint8_t s_perf_wait_valid;
static uint8_t s_perf_observer_bound;
static uint8_t s_perf_observer_attempted;
static int s_perf_peak_page;
static uint32_t s_perf_accum_ms;
static uint32_t s_perf_max_loop_ms;
static uint32_t s_perf_max_update_ms;
static uint32_t s_perf_max_render_ms;

/**
 * @brief 以显示刷新／渲染事件串行累计阶段跨度及事件顺序错误。
 * @param[in] event UI 线程借用的显示事件，NULL 时忽略。
 * @note 删除事件撤销观察器状态，后续周期可重新绑定。
 */
static void product_ui_perf_display_event(lv_event_t *event)
{
    uint32_t now_ms;
    lv_event_code_t code;
    if (event == NULL) return;
    now_ms = lv_tick_get();
    code = lv_event_get_code(event);
    switch (code) {
    case LV_EVENT_REFR_START:
        if (s_perf_frame.phase != PRODUCT_UI_PERF_IDLE) s_perf_frame.errors++;
        s_perf_frame.phase = PRODUCT_UI_PERF_PREPARE;
        s_perf_frame.started_ms = now_ms;
        break;
    case LV_EVENT_RENDER_START:
        if (s_perf_frame.phase != PRODUCT_UI_PERF_PREPARE) {
            s_perf_frame.errors++;
            break;
        }
        s_perf_frame.pre_ms += now_ms - s_perf_frame.started_ms;
        s_perf_frame.draw_started_ms = now_ms;
        s_perf_frame.phase = PRODUCT_UI_PERF_DRAW;
        break;
    case LV_EVENT_RENDER_READY:
        if (s_perf_frame.phase != PRODUCT_UI_PERF_DRAW) {
            s_perf_frame.errors++;
            break;
        }
        s_perf_frame.draw_ms += now_ms - s_perf_frame.draw_started_ms;
        s_perf_frame.phase = PRODUCT_UI_PERF_TAIL;
        break;
    case LV_EVENT_REFR_READY:
        if (s_perf_frame.phase == PRODUCT_UI_PERF_PREPARE) {
            s_perf_frame.pre_ms += now_ms - s_perf_frame.started_ms;
            s_perf_frame.refresh_ms += now_ms - s_perf_frame.started_ms;
        } else if (s_perf_frame.phase == PRODUCT_UI_PERF_TAIL) {
            s_perf_frame.refresh_ms += now_ms - s_perf_frame.started_ms;
            s_perf_frame.rendered++;
        } else {
            s_perf_frame.errors++;
        }
        s_perf_frame.phase = PRODUCT_UI_PERF_IDLE;
        break;
    case LV_EVENT_DELETE:
        s_perf_frame.phase = PRODUCT_UI_PERF_IDLE;
        s_perf_observer_bound = 0U;
        s_perf_observer_attempted = 0U;
        break;
    default:
        break;
    }
}

/**
 * @brief 在周期结束消费累计等待样本并记录本报告窗口的峰值周期及帧。
 */
void product_ui_perf_cycle(uint32_t update_ms, uint32_t render_ms)
{
    gc9307_ui_wait_stats_t wait;
    product_ui_perf_cycle_t cycle = {0};

    cycle.ended_ms = lv_tick_get();
    cycle.update_ms = update_ms;
    d_lvgl_profile_get_cycle(&cycle.work);
    lv_port_disp_gc9307_get_ui_wait_stats(&wait);
    /* 相邻回调包围 UI 执行器工作，包含更新及 handler 等待；
     * 不是纯绘制／CPU 时间。首个样本只建基线，不将启动历史计入本周期。 */
    if (s_perf_wait_valid) {
        cycle.wait_ms = wait.total_ms - s_perf_previous_wait.total_ms;
        cycle.wait_calls = wait.count - s_perf_previous_wait.count;
        cycle.wait_valid = 1U;
    }
    cycle.submitted = wait.submitted;
    cycle.ready = wait.ready;
    /* 消费每次样本，包括非峰值周期和零毫秒信号量返回。 */
    s_perf_previous_wait = wait;
    s_perf_wait_valid = 1U;
    if (render_ms > s_perf_max_render_ms) {
        s_perf_peak_frame = s_perf_frame;
        s_perf_peak_cycle = cycle;
        if (s_perf_frame.phase != PRODUCT_UI_PERF_IDLE) s_perf_peak_frame.errors++;
        s_perf_peak_page = page_manager_get_current();
    }
    s_perf_max_update_ms = LV_MAX(s_perf_max_update_ms, update_ms);
    s_perf_max_render_ms = LV_MAX(s_perf_max_render_ms, render_ms);
    s_perf_frame.phase = PRODUCT_UI_PERF_IDLE;
    s_perf_frame.pre_ms = 0U;
    s_perf_frame.draw_ms = 0U;
    s_perf_frame.refresh_ms = 0U;
    s_perf_frame.rendered = 0U;
    s_perf_frame.errors = 0U;
}

/**
 * @brief 在 UI 周期按需安装显示观察器，并每两秒输出聚合性能及内存统计。
 */
void product_ui_perf_tick(uint32_t elapsed_ms)
{
    lv_mem_monitor_t monitor;
    d_lcd_gc9307_transfer_stats_t transfer;

    if (!s_perf_observer_bound && !s_perf_observer_attempted) {
        lv_display_t *display = lv_display_get_default();
        if (display != NULL) {
            uint32_t event_count = lv_display_get_event_count(display);
            s_perf_observer_attempted = 1U;
            lv_display_add_event_cb(display, product_ui_perf_display_event, LV_EVENT_ALL, NULL);
            s_perf_observer_bound = lv_display_get_event_count(display) == event_count + 1U;
        }
    }
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
           "dma_completed=%u dma_failed=%u free=%u frag=%u%% "
           "observer=%u peak_page=%d refr=%u pre=%u draw=%u frames=%u phase_errors=%u "
           "peak_uptime=%u peak_update=%u cycle_wait_ms=%u cycle_wait_calls=%u "
           "cycle_wait_valid=%u flush_submit=%u flush_ready=%u "
           "profile_valid=%u layout_ms=%u layout_calls=%u dispatch_ms=%u dispatch_calls=%u "
           "flush_cb_ms=%u flush_cb_calls=%u refresh_layout_ms=%u refresh_layout_calls=%u "
           "profile_clock_faults=%u profile_scope_faults=%u\n",
           s_perf_max_loop_ms, s_perf_max_update_ms, s_perf_max_render_ms,
           transfer.complete_count, transfer.failure_count, monitor.free_size, monitor.frag_pct,
           s_perf_observer_bound, s_perf_peak_page, s_perf_peak_frame.refresh_ms,
           s_perf_peak_frame.pre_ms, s_perf_peak_frame.draw_ms,
           s_perf_peak_frame.rendered, s_perf_peak_frame.errors,
           s_perf_peak_cycle.ended_ms, s_perf_peak_cycle.update_ms,
           s_perf_peak_cycle.wait_ms, s_perf_peak_cycle.wait_calls,
           s_perf_peak_cycle.wait_valid, s_perf_peak_cycle.submitted, s_perf_peak_cycle.ready,
           s_perf_peak_cycle.work.valid,
           s_perf_peak_cycle.work.elapsed_ms[D_LVGL_PROFILE_OBJECT_LAYOUT],
           s_perf_peak_cycle.work.calls[D_LVGL_PROFILE_OBJECT_LAYOUT],
           s_perf_peak_cycle.work.elapsed_ms[D_LVGL_PROFILE_DISPATCH],
           s_perf_peak_cycle.work.calls[D_LVGL_PROFILE_DISPATCH],
           s_perf_peak_cycle.work.elapsed_ms[D_LVGL_PROFILE_FLUSH_CALLBACK],
           s_perf_peak_cycle.work.calls[D_LVGL_PROFILE_FLUSH_CALLBACK],
           s_perf_peak_cycle.work.elapsed_ms[D_LVGL_PROFILE_REFRESH_LAYOUT],
           s_perf_peak_cycle.work.calls[D_LVGL_PROFILE_REFRESH_LAYOUT],
           s_perf_peak_cycle.work.clock_faults, s_perf_peak_cycle.work.scope_faults);
    if (s_perf_max_render_ms > 0U && s_perf_peak_cycle.work.sample_id != 0U) {
        printf("[ProductUI] worker-profile peak_uptime=%u sample_id=%u worker_valid=%u "
               "sw_ms=%u sw_calls=%u sw_span_ms=%u label_ms=%u label_calls=%u "
               "late_events=%u worker_faults=%u\n",
               s_perf_peak_cycle.ended_ms, s_perf_peak_cycle.work.sample_id,
               s_perf_peak_cycle.work.worker_valid,
               s_perf_peak_cycle.work.elapsed_ms[D_LVGL_PROFILE_SW_RASTER],
               s_perf_peak_cycle.work.calls[D_LVGL_PROFILE_SW_RASTER],
               s_perf_peak_cycle.work.worker_span_ms,
               s_perf_peak_cycle.work.elapsed_ms[D_LVGL_PROFILE_SW_LABEL],
               s_perf_peak_cycle.work.calls[D_LVGL_PROFILE_SW_LABEL],
               s_perf_peak_cycle.work.worker_late_events,
               s_perf_peak_cycle.work.worker_faults);
    }
    s_perf_accum_ms %= PRODUCT_UI_PERF_LOG_MS;
    s_perf_max_loop_ms = 0;
    s_perf_max_update_ms = 0U;
    s_perf_max_render_ms = 0U;
    s_perf_peak_frame.pre_ms = 0U;
    s_perf_peak_frame.draw_ms = 0U;
    s_perf_peak_frame.refresh_ms = 0U;
    s_perf_peak_frame.rendered = 0U;
    s_perf_peak_frame.errors = 0U;
    /* 累计基线跨报告窗口保留，窗口重置只清聚合峰值。 */
    memset(&s_perf_peak_cycle, 0, sizeof(s_perf_peak_cycle));
}

/**
 * @brief 按启用宏输出页面创建经过时间。
 */
void product_ui_perf_page_created(const char *name, uint32_t started_at_ms)
{
    if (name != NULL) {
        printf("[ProductUI] page=%s create_ms=%u\n",
               name, lv_tick_get() - started_at_ms);
    }
}

#else

/**
 * @brief 性能诊断关闭时保留该入口并忽略本次参数。
 */
void product_ui_perf_cycle(uint32_t update_ms, uint32_t render_ms)
{
    (void)update_ms;
    (void)render_ms;
}

/**
 * @brief 性能诊断关闭时保留该入口并忽略本次参数。
 */
void product_ui_perf_tick(uint32_t elapsed_ms)
{
    (void)elapsed_ms;
}

/**
 * @brief 性能诊断关闭时保留该入口并忽略本次参数。
 */
void product_ui_perf_page_created(const char *name, uint32_t started_at_ms)
{
    (void)name;
    (void)started_at_ms;
}

#endif /* D_DEBUG_UI_PERFORMANCE */
#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
