/**
 * @file lv_port_disp_gc9307_trace.c
 * @brief Bounded GC9307 frame and TE timing diagnostics.
 * D++: Owned by the D_Code GC9307/HYN screen profile.
 */

#define BOOL_DEFINE_CONFLICT
#include "lv_port_disp_gc9307_trace.h"
#include "config/app_config.h"
#include "system/includes.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307) && \
    defined(D_GC9307_FRAME_TRACE_ENABLE)

#include "platform/screen/profiles/gc9307_hyn/lcd_gc9307.h"

#define GC9307_FRAME_DETAIL_LIMIT 10
#define GC9307_FRAME_REPORT_MS    2000
#define GC9307_FRAME_SLOW_MS      33

typedef struct {
    uint32_t frame_id;
    uint32_t frame_start_ms;
    uint32_t logical_parts;
    uint32_t logical_bytes;
    uint32_t physical_dma_count;
    uint32_t physical_bytes;
    uint32_t rotate_ms;
    uint32_t wait_ms;
    uint32_t te_to_dma_ms;
    uint32_t tx_ms;
    uint32_t report_start_ms;
    uint32_t report_frames;
    uint32_t report_total_ms;
    uint32_t report_max_ms;
    uint32_t report_slow_frames;
    uint32_t report_irq_start;
    uint32_t report_timeout_start;
    unsigned int transition_id;
    bool is_active;
} gc9307_port_frame_trace_t;

static gc9307_port_frame_trace_t s_trace;

void gc9307_port_trace_frame_begin(uint32_t now_ms,
                                   unsigned int transition_id)
{
    d_lcd_gc9307_te_stats_t stats;

    s_trace.frame_id++;
    s_trace.frame_start_ms = now_ms;
    s_trace.logical_parts = 0;
    s_trace.logical_bytes = 0;
    s_trace.physical_dma_count = 0;
    s_trace.physical_bytes = 0;
    s_trace.rotate_ms = 0;
    s_trace.wait_ms = 0;
    s_trace.te_to_dma_ms = 0;
    s_trace.tx_ms = 0;
    s_trace.transition_id = transition_id;
    s_trace.is_active = true;
    if (s_trace.report_start_ms != 0) {
        return;
    }

    d_lcd_gc9307_get_te_stats(&stats);
    s_trace.report_start_ms = now_ms;
    s_trace.report_irq_start = stats.irq_count;
    s_trace.report_timeout_start = stats.timeout_count;
}

void gc9307_port_trace_part(const gc9307_port_trace_part_t *part)
{
    if ((part == NULL) || (part->logical == NULL)) {
        return;
    }

    s_trace.logical_parts++;
    s_trace.logical_bytes += part->logical_bytes;
    s_trace.physical_bytes += part->physical_bytes;
    s_trace.rotate_ms += part->rotate_ms;
    s_trace.wait_ms += part->wait_ms;
    s_trace.te_to_dma_ms += part->te_to_dma_ms;
    s_trace.tx_ms += part->tx_ms;
    if (part->physical_dma_completed) {
        s_trace.physical_dma_count++;
    }
    if ((s_trace.frame_id > GC9307_FRAME_DETAIL_LIMIT) &&
        (part->transition_id == 0U)) {
        return;
    }

    printf("[GC9307-FRAME] part frame=%u logical_n=%u area=%d,%d-%d,%d "
           "logical_bytes=%u physical_bytes=%u rot=%u wait=%u "
           "te_dma=%u tx=%u first=%u waited=%u te=%d dma=%d complete=%u "
           "last=%u transition=%u\n",
           s_trace.frame_id, s_trace.logical_parts,
           part->logical->x1, part->logical->y1,
           part->logical->x2, part->logical->y2,
           part->logical_bytes, part->physical_bytes,
           part->rotate_ms, part->wait_ms, part->te_to_dma_ms,
           part->tx_ms, part->is_first ? 1U : 0U,
           part->waited_for_te ? 1U : 0U,
           part->wait_result, part->transfer_result,
           part->physical_dma_completed ? 1U : 0U,
           part->is_last ? 1U : 0U, part->transition_id);
}

static void gc9307_port_trace_report(uint32_t now_ms, uint32_t frame_ms)
{
    d_lcd_gc9307_te_stats_t stats;
    uint32_t elapsed_ms;
    uint32_t average_ms;

    s_trace.report_frames++;
    s_trace.report_total_ms += frame_ms;
    if (frame_ms > s_trace.report_max_ms) {
        s_trace.report_max_ms = frame_ms;
    }
    if (frame_ms > GC9307_FRAME_SLOW_MS) {
        s_trace.report_slow_frames++;
    }

    elapsed_ms = now_ms - s_trace.report_start_ms;
    if (elapsed_ms < GC9307_FRAME_REPORT_MS) {
        return;
    }

    d_lcd_gc9307_get_te_stats(&stats);
    average_ms = s_trace.report_total_ms / s_trace.report_frames;
    printf("[GC9307-FRAME] summary frames=%u avg=%u max=%u over33=%u "
           "te_irq=%u period=%u requests=%u post=%u stale=%u timeout=%u registered=%u\n",
           s_trace.report_frames, average_ms, s_trace.report_max_ms,
           s_trace.report_slow_frames,
           stats.irq_count - s_trace.report_irq_start,
           stats.irq_period_ms, stats.request_count,
           stats.sem_post_count, stats.stale_wakeup_count,
           stats.timeout_count - s_trace.report_timeout_start,
           stats.is_registered);

    s_trace.report_start_ms = now_ms;
    s_trace.report_frames = 0;
    s_trace.report_total_ms = 0;
    s_trace.report_max_ms = 0;
    s_trace.report_slow_frames = 0;
    s_trace.report_irq_start = stats.irq_count;
    s_trace.report_timeout_start = stats.timeout_count;
}

void gc9307_port_trace_frame_finish(uint32_t now_ms)
{
    d_lcd_gc9307_te_stats_t stats;
    d_lcd_gc9307_transfer_stats_t transfer_stats;
    uint32_t frame_ms;
    uint32_t speed_ratio_x1000 = 0;

    if (!s_trace.is_active) {
        return;
    }
    s_trace.is_active = false;
    frame_ms = now_ms - s_trace.frame_start_ms;
    d_lcd_gc9307_get_te_stats(&stats);
    d_lcd_gc9307_get_transfer_stats(&transfer_stats);
    if ((s_trace.tx_ms != 0) && (stats.irq_period_ms != 0)) {
        speed_ratio_x1000 = stats.irq_period_ms * 1000U / s_trace.tx_ms;
    }
    if ((s_trace.frame_id <= GC9307_FRAME_DETAIL_LIMIT) ||
        (s_trace.transition_id != 0U)) {
        printf("[GC9307-FRAME] done frame=%u logical_parts=%u logical_bytes=%u "
               "physical_dma=%u physical_bytes=%u rot=%u wait=%u te_dma=%u "
               "tx=%u total=%u transition=%u request_ms=%u request_irq=%u "
               "edge_ms=%u edge_irq=%u req_edge=%u period=%u ratio_x1000=%u "
               "dma_start=%u physical_count=%u/%u/%u fail=%u\n",
               s_trace.frame_id, s_trace.logical_parts,
               s_trace.logical_bytes, s_trace.physical_dma_count,
               s_trace.physical_bytes, s_trace.rotate_ms,
               s_trace.wait_ms, s_trace.te_to_dma_ms, s_trace.tx_ms,
               frame_ms, s_trace.transition_id,
               stats.last_request_ms, stats.last_request_irq_count,
               stats.last_edge_ms, stats.last_edge_irq_count,
               stats.last_request_to_edge_ms, stats.irq_period_ms,
               speed_ratio_x1000, transfer_stats.last_submit_ms,
               transfer_stats.submit_count,
               transfer_stats.callback_count,
               transfer_stats.complete_count,
               transfer_stats.failure_count);
    }
    gc9307_port_trace_report(now_ms, frame_ms);
}

#endif
