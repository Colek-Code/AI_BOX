/**
 * @file lv_port_disp_gc9307.c
 * @brief LVGL display port for the GC9307 LCD-SPI screen profile.
 * D++: Owned by the D_Code GC9307/HYN screen profile, not the shared LVGL tree.
 */

#define BOOL_DEFINE_CONFLICT
#include "framework/d_lvgl_port.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307_trace.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307_transition.h"
#include <stdbool.h>
#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#include "platform/screen/lcd/d_lcd_compat.h"
#include "platform/screen/profiles/gc9307_hyn/lcd_gc9307.h"
#include "system/includes.h"
#include "system/sys_time.h"
#if D_CAMERA_PREVIEW_ENABLE
#include "lv_port_disp_gc9307_region.h"
#endif

#ifndef D_GC9307_PHYS_HOR_RES
#define D_GC9307_PHYS_HOR_RES 240
#endif

#ifndef D_GC9307_PHYS_VER_RES
#define D_GC9307_PHYS_VER_RES 320
#endif

#ifndef MY_DISP_HOR_RES
#define MY_DISP_HOR_RES D_GC9307_PHYS_VER_RES
#endif

#ifndef MY_DISP_VER_RES
#define MY_DISP_VER_RES D_GC9307_PHYS_HOR_RES
#endif

#if D_GC9307_ROTATION_DEGREES == 90
#define GC9307_LVGL_ROTATION LV_DISPLAY_ROTATION_90
#else
#define GC9307_LVGL_ROTATION LV_DISPLAY_ROTATION_270
#endif

#if LV_COLOR_DEPTH == 16
#define LV_PIXEL_COLOR_T lv_color16_t
#else
#error "GC9307 LVGL port requires LV_COLOR_DEPTH == 16"
#endif

#define GC9307_LVGL_PARTIAL_ROWS   40
#define GC9307_LVGL_PARTIAL_PIXELS (MY_DISP_HOR_RES * GC9307_LVGL_PARTIAL_ROWS)
#if (D_UI_TARGET == D_UI_PRODUCT)
#define GC9307_LVGL_TIMER_PERIOD_MS D_PRODUCT_UI_FRAME_PERIOD_MS
#else
#define GC9307_LVGL_TIMER_PERIOD_MS 16U
#endif
#define GC9307_FLUSH_TASK_PRIO       2
#define GC9307_FLUSH_TASK_STACK      2048
#define GC9307_DMA_TIMEOUT_TICKS     20
#define GC9307_ASYNC_DETAIL_LIMIT    10

static bool s_disp_flush_enabled = true;
static bool s_refresh_first = true;
/* LVGL-thread requests are latched for all strips of a refresh. */
static bool s_composed_frame_pending;
static bool s_composed_redraw_pending;
static bool s_refresh_composed;
static unsigned int s_refresh_transition_id;
static uint32_t s_composed_frames;
static uint32_t s_composed_report_ms;
static bool s_bad_area_logged;
static bool s_bad_stride_logged;
static uint8_t s_wire_buf[GC9307_LVGL_PARTIAL_PIXELS * 2]
       __attribute__((aligned(32)));

typedef enum {
    GC9307_FRAME_SYNC_IDLE = 0,
    GC9307_FRAME_SYNC_PREPARED,
    GC9307_FRAME_SYNC_WAIT_TE,
    GC9307_FRAME_SYNC_DMA_ACTIVE,
    GC9307_FRAME_SYNC_COMPLETE,
} gc9307_frame_sync_state_e;

static gc9307_frame_sync_state_e s_frame_sync_state;

typedef struct {
    lv_area_t logical;
    uint8_t *px_map;
    uint32_t sequence;
    unsigned int transition_id;
    bool starts_refresh;
    bool waits_for_te;
    bool is_last;
    bool enabled;
    bool composed_frame;
#if D_CAMERA_PREVIEW_ENABLE
    gc9307_region_request_t region;
#endif
} gc9307_flush_job_t;

static gc9307_flush_job_t s_flush_job;
static OS_SEM s_flush_job_sem;
static OS_SEM s_flush_done_sem;
static volatile uint32_t s_flush_submit_count;
static volatile uint32_t s_flush_ready_count;
static volatile uint8_t s_flush_job_pending;
static bool s_flush_worker_ready;

#if D_CAMERA_PREVIEW_ENABLE
/* UI configuration is copied into each job; only the flush owner touches staging memory. */
static gc9307_region_request_t s_region_update;
static unsigned int s_region_next_id;
static uint32_t s_region_frame_count;
static uint32_t s_region_report_ms;
static uint32_t s_region_report_frames;

unsigned int lv_port_disp_gc9307_begin_region_update(const lv_area_t *area)
{
    if (!gc9307_region_area_valid(area)) {
        s_region_update.id = 0U;
        return 0U;
    }
    if (++s_region_next_id == 0U) {
        ++s_region_next_id;
    }
    s_region_update.logical = *area;
    s_region_update.id = s_region_next_id;
    return s_region_update.id;
}

void lv_port_disp_gc9307_cancel_region_update(unsigned int request_id)
{
    if (request_id != 0U && request_id == s_region_update.id) {
        s_region_update.id = 0U;
    }
}
#endif

static void disp_init(void);
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void disp_flush_wait(lv_display_t *disp);

static void gc9307_handle_transition_redraw(lv_display_t *disp)
{
    bool redraw = gc9307_transition_take_redraw_request();
    (void)disp;
#if D_CAMERA_PREVIEW_ENABLE
    unsigned int region_id = gc9307_region_take_redraw_request();
    if (region_id != 0U) {
        lv_port_disp_gc9307_cancel_region_update(region_id);
        redraw = true;
    }
#endif

    /* flush_wait can run inside rendering; defer invalidation until REFR_READY. */
    s_composed_redraw_pending |= redraw;
}

void lv_port_disp_gc9307_begin_full_frame_transition(unsigned int transition_id)
{
    gc9307_transition_arm(transition_id);
}

void lv_port_disp_gc9307_request_composed_frame(void)
{
#if (D_UI_TARGET == D_UI_PRODUCT)
    lv_display_t *disp = lv_display_get_default();
    lv_obj_t *screen = disp ? lv_display_get_screen_active(disp) : NULL;

    if (screen != NULL) {
        s_composed_frame_pending = true;
        lv_obj_invalidate(screen);
    }
#endif
}

static void gc9307_refresh_ready(lv_event_t *event)
{
    (void)event;
    if (s_composed_redraw_pending) {
        s_composed_redraw_pending = false;
        lv_port_disp_gc9307_request_composed_frame();
    }
}

static bool gc9307_map_rotated_area(const lv_area_t *logical, lv_area_t *physical)
{
    if ((logical == NULL) || (physical == NULL)) {
        return false;
    }

#if D_GC9307_ROTATION_DEGREES == 90
    physical->x1 = (lv_coord_t)(D_GC9307_PHYS_HOR_RES - 1 - logical->y2);
    physical->x2 = (lv_coord_t)(D_GC9307_PHYS_HOR_RES - 1 - logical->y1);
    physical->y1 = logical->x1;
    physical->y2 = logical->x2;
#else
    physical->x1 = logical->y1;
    physical->x2 = logical->y2;
    physical->y1 = (lv_coord_t)(D_GC9307_PHYS_VER_RES - 1 - logical->x2);
    physical->y2 = (lv_coord_t)(D_GC9307_PHYS_VER_RES - 1 - logical->x1);
#endif

    if ((physical->x1 < 0) || (physical->y1 < 0) ||
        (physical->x2 >= D_GC9307_PHYS_HOR_RES) ||
        (physical->y2 >= D_GC9307_PHYS_VER_RES) ||
        (physical->x1 > physical->x2) ||
        (physical->y1 > physical->y2)) {
        if (!s_bad_area_logged) {
            s_bad_area_logged = true;
            printf("[GC9307-LVGL] bad area l=(%d,%d)-(%d,%d) p=(%d,%d)-(%d,%d)\n",
                   logical->x1, logical->y1, logical->x2, logical->y2,
                   physical->x1, physical->y1, physical->x2, physical->y2);
        }
        return false;
    }

    return true;
}

static uint8_t *gc9307_prepare_rgb565(const lv_area_t *area, uint8_t *px_map, lv_area_t *physical)
{
    uint32_t src_stride;
    int width;
    int height;
    int pixels;
    int x;
    int y;

    if ((area == NULL) || (px_map == NULL) || (physical == NULL)) {
        return NULL;
    }

    width = area->x2 - area->x1 + 1;
    height = area->y2 - area->y1 + 1;
    pixels = width * height;
    if ((pixels <= 0) || (pixels > GC9307_LVGL_PARTIAL_PIXELS)) {
        if (!s_bad_area_logged) {
            s_bad_area_logged = true;
            printf("[GC9307-LVGL] drop flush pixels=%d max=%d\n", pixels, GC9307_LVGL_PARTIAL_PIXELS);
        }
        return NULL;
    }

    if (!gc9307_map_rotated_area(area, physical)) {
        return NULL;
    }

    /* D++: Partial areas can have aligned row padding even when the full-width buffer does not. */
    src_stride = lv_draw_buf_width_to_stride((uint32_t)width, LV_COLOR_FORMAT_RGB565);
    if (src_stride == 0) {
        if (!s_bad_stride_logged) {
            s_bad_stride_logged = true;
            printf("[GC9307-LVGL] invalid RGB565 stride width=%d\n", width);
        }
        return NULL;
    }

    for (y = 0; y < height; y++) {
        const u16 *src_row = (const u16 *)(px_map + (uint32_t)y * src_stride);

        for (x = 0; x < width; x++) {
            int dst_index;
            u16 color = src_row[x];

#if D_GC9307_ROTATION_DEGREES == 90
            dst_index = x * height + (height - y - 1);
#else
            dst_index = (width - x - 1) * height + y;
#endif
            s_wire_buf[2 * dst_index] = (uint8_t)((color >> 8) & 0xff);
            s_wire_buf[2 * dst_index + 1] = (uint8_t)(color & 0xff);
        }
    }

    return s_wire_buf;
}

typedef struct {
    d_lcd_gc9307_te_wait_result_e wait_result;
    uint32_t wait_ms;
    uint32_t te_to_dma_ms;
    uint32_t tx_ms;
} gc9307_transfer_timing_t;

static int gc9307_transfer_prepared(const lv_area_t *physical,
                                    uint8_t *wire, uint32_t bytes,
                                    bool wait_for_te,
                                    gc9307_transfer_timing_t *timing)
{
    uint32_t wait_done_ms;
    uint32_t dma_start_ms;
    uint32_t tx_start_ms;
    d_lcd_gc9307_transfer_stats_t transfer_stats;
    int result;

    if ((physical == NULL) || (wire == NULL) ||
        (bytes == 0) || (timing == NULL)) {
        return -1;
    }
    if (s_frame_sync_state != GC9307_FRAME_SYNC_IDLE) {
        printf("[GC9307-FRAME] state reset unexpected=%u\n",
               (unsigned int)s_frame_sync_state);
        s_frame_sync_state = GC9307_FRAME_SYNC_IDLE;
    }
    memset(timing, 0, sizeof(*timing));
    timing->wait_result = D_LCD_GC9307_TE_WAIT_BYPASSED;
    result = d_lcd_gc9307_prepare_area((uint16_t)physical->x1,
                                       (uint16_t)physical->x2,
                                       (uint16_t)physical->y1,
                                       (uint16_t)physical->y2);
    if (result == 0) {
        result = d_lcd_gc9307_prepare_write();
    }
    if (result != 0) {
        tx_start_ms = timer_get_ms();
        lcd_lvgl_full((u16)physical->x1, (u16)physical->x2,
                      (u16)physical->y1, (u16)physical->y2, wire);
        timing->tx_ms = timer_get_ms() - tx_start_ms;
        return 1;
    }

    s_frame_sync_state = GC9307_FRAME_SYNC_PREPARED;
    if (wait_for_te) {
        uint32_t wait_start_ms = timer_get_ms();

        s_frame_sync_state = GC9307_FRAME_SYNC_WAIT_TE;
        timing->wait_result = d_lcd_gc9307_wait_te_frame();
        wait_done_ms = timer_get_ms();
        timing->wait_ms = wait_done_ms - wait_start_ms;
    } else {
        wait_done_ms = timer_get_ms();
    }

    s_frame_sync_state = GC9307_FRAME_SYNC_DMA_ACTIVE;
    result = d_lcd_gc9307_flush_pixels_start(wire, bytes);
    if (result == 0) {
        d_lcd_gc9307_get_transfer_stats(&transfer_stats);
        dma_start_ms = transfer_stats.last_submit_ms;
        timing->te_to_dma_ms = wait_for_te ?
                                   (dma_start_ms - wait_done_ms) : 0;
        result = d_lcd_gc9307_flush_wait(GC9307_DMA_TIMEOUT_TICKS);
        timing->tx_ms = timer_get_ms() - dma_start_ms;
    } else {
        tx_start_ms = timer_get_ms();
        lcd_lvgl_full((u16)physical->x1, (u16)physical->x2,
                      (u16)physical->y1, (u16)physical->y2, wire);
        timing->tx_ms = timer_get_ms() - tx_start_ms;
        result = 1;
    }
    s_frame_sync_state = GC9307_FRAME_SYNC_COMPLETE;
    s_frame_sync_state = GC9307_FRAME_SYNC_IDLE;
    return result;
}

static uint32_t gc9307_logical_bytes(const lv_area_t *area)
{
    return (uint32_t)(area->x2 - area->x1 + 1) *
           (uint32_t)(area->y2 - area->y1 + 1) * 2U;
}

#if D_CAMERA_PREVIEW_ENABLE
static int gc9307_transfer_region(const gc9307_region_frame_t *frame,
                                   gc9307_port_trace_part_t *trace)
{
    gc9307_transfer_timing_t timing;
    d_lcd_gc9307_te_stats_t te;
    uint32_t now_ms;
    int result = gc9307_transfer_prepared(&frame->physical, frame->pixels, frame->bytes, true, &timing);
    if (result != 0) {
        gc9307_region_block_reuse();
        printf("[GC9307-REGION] unconfirmed DMA result=%d; staging buffer retained until reboot\n", result);
    }
    trace->physical_bytes = frame->bytes;
    trace->wait_ms = timing.wait_ms;
    trace->te_to_dma_ms = timing.te_to_dma_ms;
    trace->tx_ms = timing.tx_ms;
    trace->wait_result = (int)timing.wait_result;
    trace->waited_for_te = true;
    trace->physical_dma_completed = result == 0;
    now_ms = timer_get_ms();
    s_region_frame_count++;
    /* Report after DMA, so UART logging cannot create gaps between image strips. */
    if (s_region_frame_count <= 3U || (uint32_t)(now_ms - s_region_report_ms) >= 2000U) {
        d_lcd_gc9307_get_te_stats(&te);
        printf("[GC9307-REGION] id=%u parts=%u bytes=%u collect=%u wait=%u te_dma=%u tx=%u "
               "result=%d te=%d period=%u frames=%u elapsed=%u\n",
               frame->request_id, frame->parts, frame->bytes, frame->collect_ms,
               timing.wait_ms, timing.te_to_dma_ms, timing.tx_ms, result, (int)timing.wait_result,
               te.irq_period_ms, s_region_frame_count - s_region_report_frames, now_ms - s_region_report_ms);
        s_region_report_ms = now_ms;
        s_region_report_frames = s_region_frame_count;
    }
    return result;
}
#endif

static int gc9307_process_transition(const gc9307_flush_job_t *job,
                                     gc9307_port_trace_part_t *trace)
{
    gc9307_transition_frame_t frame;
    gc9307_transition_stage_result_e stage_result;
    gc9307_transfer_timing_t timing;
    lv_area_t physical = {
        .x1 = 0,
        .y1 = 0,
        .x2 = D_GC9307_PHYS_HOR_RES - 1,
        .y2 = D_GC9307_PHYS_VER_RES - 1,
    };
    uint32_t stage_start_ms = timer_get_ms();
    int result = 0;

    if (job->composed_frame && job->starts_refresh) {
        gc9307_transition_arm_composed_frame();
    }
    memset(&frame, 0, sizeof(frame));
    stage_result = gc9307_transition_stage(job->transition_id,
                                           &job->logical, job->px_map,
                                           job->is_last, &frame);
    trace->rotate_ms = timer_get_ms() - stage_start_ms;
    if (stage_result == GC9307_TRANSITION_STAGE_READY) {
        result = gc9307_transfer_prepared(&physical, frame.frame_data,
                                           frame.frame_bytes, true, &timing);
        if (result < 0) {
            gc9307_transition_block_reuse();
        }
        if (!job->composed_frame) {
            gc9307_transition_publish_frame(&frame, result);
        } else {
            uint32_t now_ms = timer_get_ms();
            s_composed_frames++;
            if (s_composed_frames <= 3U || now_ms - s_composed_report_ms >= 2000U || result < 0) {
                printf("[GC9307-COMPOSE] frames=%u parts=%u bytes=%u collect=%u "
                       "wait=%u te_dma=%u tx=%u result=%d\n",
                       s_composed_frames, frame.logical_parts, frame.frame_bytes,
                       frame.collect_ms, timing.wait_ms, timing.te_to_dma_ms,
                       timing.tx_ms, result);
                s_composed_report_ms = now_ms;
            }
        }
        trace->physical_bytes = frame.frame_bytes;
        trace->wait_ms = timing.wait_ms;
        trace->te_to_dma_ms = timing.te_to_dma_ms;
        trace->tx_ms = timing.tx_ms;
        trace->wait_result = (int)timing.wait_result;
        trace->waited_for_te = true;
        trace->physical_dma_completed = (result == 0);
    } else if (stage_result == GC9307_TRANSITION_STAGE_ERROR) {
        result = -1;
    }
    return result;
}

static int gc9307_process_partial(const gc9307_flush_job_t *job,
                                  gc9307_port_trace_part_t *trace)
{
    gc9307_transfer_timing_t timing;
    lv_area_t physical;
    uint8_t *wire;
    uint32_t rotate_start_ms = timer_get_ms();
    int result;

#if D_CAMERA_PREVIEW_ENABLE
    gc9307_region_frame_t frame;
    gc9307_region_part_t part = {
        .logical = &job->logical, .pixels = job->px_map,
        .stride = lv_draw_buf_width_to_stride((uint32_t)(job->logical.x2 - job->logical.x1 + 1),
                                               LV_COLOR_FORMAT_RGB565),
        .now_ms = rotate_start_ms, .is_last_refresh = job->is_last,
    };
    gc9307_region_result_e region_result = gc9307_region_stage(&job->region, &part, &frame);
    if (region_result != GC9307_REGION_BYPASS) {
        trace->rotate_ms = timer_get_ms() - rotate_start_ms;
        if (region_result == GC9307_REGION_STAGED) {
            return 0;
        }
        if (region_result == GC9307_REGION_READY) {
            return gc9307_transfer_region(&frame, trace);
        }
        printf("[GC9307-REGION] staging failed id=%u; full redraw requested\n", job->region.id);
        return -1;
    }
#endif

    wire = gc9307_prepare_rgb565(&job->logical, job->px_map, &physical);
    trace->rotate_ms = timer_get_ms() - rotate_start_ms;
    if (wire == NULL) {
        return -1;
    }
    trace->physical_bytes = (uint32_t)(physical.x2 - physical.x1 + 1) *
                            (uint32_t)(physical.y2 - physical.y1 + 1) * 2U;
    trace->waited_for_te = job->waits_for_te;
    trace->wait_result = D_LCD_GC9307_TE_WAIT_BYPASSED;
    result = gc9307_transfer_prepared(&physical, wire,
                                      trace->physical_bytes,
                                      job->waits_for_te, &timing);
    if (result < 0) {
        return -1;
    }
    trace->wait_ms = timing.wait_ms;
    trace->te_to_dma_ms = timing.te_to_dma_ms;
    trace->tx_ms = timing.tx_ms;
    trace->wait_result = (int)timing.wait_result;
    trace->physical_dma_completed = (result == 0);
    return result;
}

static void gc9307_process_flush_job(const gc9307_flush_job_t *job)
{
    gc9307_port_trace_part_t trace;
    uint32_t start_ms = timer_get_ms();
    int transfer_result = 0;

    memset(&trace, 0, sizeof(trace));
    trace.logical = &job->logical;
    trace.logical_bytes = gc9307_logical_bytes(&job->logical);
    trace.wait_result = D_LCD_GC9307_TE_WAIT_BYPASSED;
    trace.is_first = job->starts_refresh;
    trace.is_last = job->is_last;
    trace.transition_id = job->transition_id;
    if (job->starts_refresh) {
        gc9307_port_trace_frame_begin(start_ms, job->transition_id);
    }
    if (job->enabled) {
        if (job->transition_id != 0U || job->composed_frame) {
#if D_CAMERA_PREVIEW_ENABLE
            gc9307_region_discard();
#endif
            transfer_result = gc9307_process_transition(job, &trace);
        } else {
            transfer_result = gc9307_process_partial(job, &trace);
        }
    }
    trace.transfer_result = transfer_result;
    gc9307_port_trace_part(&trace);
    if (job->is_last) {
        gc9307_port_trace_frame_finish(timer_get_ms());
    }
    if (job->sequence <= GC9307_ASYNC_DETAIL_LIMIT) {
        d_lcd_gc9307_transfer_stats_t stats;

        d_lcd_gc9307_get_transfer_stats(&stats);
        printf("[GC9307-ASYNC] job=%u logical=%u physical=%u result=%d "
               "physical_count=%u/%u/%u fail=%u transition=%u\n",
               job->sequence, trace.logical_bytes, trace.physical_bytes,
               transfer_result, stats.submit_count, stats.callback_count,
               stats.complete_count, stats.failure_count,
               job->transition_id);
    }
}

static void gc9307_flush_task(void *priv)
{
    gc9307_flush_job_t job;

    (void)priv;
    for (;;) {
        os_sem_pend(&s_flush_job_sem, 0);
        job = s_flush_job;
        gc9307_process_flush_job(&job);
        s_flush_job_pending = 0;
        os_sem_post(&s_flush_done_sem);
    }
}

static int gc9307_flush_worker_init(void)
{
    int result;

    if (os_sem_create(&s_flush_job_sem, 0) != OS_NO_ERR) {
        return -1;
    }
    if (os_sem_create(&s_flush_done_sem, 0) != OS_NO_ERR) {
        os_sem_del(&s_flush_job_sem, 1);
        return -1;
    }
    result = thread_fork("gc9307_flush", GC9307_FLUSH_TASK_PRIO,
                         GC9307_FLUSH_TASK_STACK, 0, NULL,
                         gc9307_flush_task, NULL);
    if (result != OS_NO_ERR) {
        os_sem_del(&s_flush_done_sem, 1);
        os_sem_del(&s_flush_job_sem, 1);
        return -1;
    }
    s_flush_worker_ready = true;
    return 0;
}

static void gc9307_set_timer_periods(lv_display_t *disp)
{
    lv_timer_t *timer;

    timer = lv_display_get_refr_timer(disp);
    if (timer != NULL) {
        lv_timer_set_period(timer, GC9307_LVGL_TIMER_PERIOD_MS);
    }
    timer = lv_anim_get_timer();
    if (timer != NULL) {
        lv_timer_set_period(timer, GC9307_LVGL_TIMER_PERIOD_MS);
    }
}

void lv_port_disp_init(void)
{
    static LV_PIXEL_COLOR_T buf_1[GC9307_LVGL_PARTIAL_PIXELS] __attribute__((aligned(32)));
    static LV_PIXEL_COLOR_T buf_2[GC9307_LVGL_PARTIAL_PIXELS] __attribute__((aligned(32)));
    lv_display_t *disp;

    disp_init();

    disp = lv_display_create(D_GC9307_PHYS_HOR_RES, D_GC9307_PHYS_VER_RES);
    if (disp == NULL) {
        printf("[GC9307-LVGL] display create failed\n");
        return;
    }
    lv_display_set_rotation(disp, GC9307_LVGL_ROTATION);
    lv_display_set_flush_cb(disp, disp_flush);
    if (gc9307_flush_worker_init() == 0) {
        lv_display_set_flush_wait_cb(disp, disp_flush_wait);
    } else {
        printf("[GC9307-ASYNC] worker init failed, synchronous fallback\n");
    }
    lv_display_set_buffers(disp, buf_1, buf_2, NULL, sizeof(buf_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    gc9307_set_timer_periods(disp);
    lv_display_add_event_cb(disp, gc9307_refresh_ready, LV_EVENT_REFR_READY, NULL);

    printf("[GC9307-LVGL] display profile=gc9307_hyn logical=%dx%d physical=%dx%d rotation_deg=%d "
           "partial_rows=%d async=%u buffers=2 frame_te=1 timer_ms=%d\n",
           MY_DISP_HOR_RES, MY_DISP_VER_RES, D_GC9307_PHYS_HOR_RES, D_GC9307_PHYS_VER_RES,
           D_GC9307_ROTATION_DEGREES, GC9307_LVGL_PARTIAL_ROWS,
           s_flush_worker_ready ? 1U : 0U, GC9307_LVGL_TIMER_PERIOD_MS);
}

void disp_enable_update(void)
{
    s_disp_flush_enabled = true;
}

void disp_disable_update(void)
{
    s_disp_flush_enabled = false;
}

static void disp_init(void)
{
    user_lcd_init();
}

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    gc9307_flush_job_t job;

    if (s_refresh_first) {
        s_refresh_transition_id = gc9307_transition_get_active_id();
        s_refresh_composed = s_composed_frame_pending && s_refresh_transition_id == 0U;
        s_composed_frame_pending = false;
    }
    job.logical = *area;
    job.px_map = px_map;
    job.sequence = ++s_flush_submit_count;
    job.transition_id = s_refresh_transition_id;
    job.composed_frame = s_refresh_composed;
    job.starts_refresh = s_refresh_first;
    job.waits_for_te = job.starts_refresh && (job.transition_id == 0U) && !job.composed_frame;
    job.is_last = lv_display_flush_is_last(disp);
    job.enabled = s_disp_flush_enabled;
#if D_CAMERA_PREVIEW_ENABLE
    job.region = s_region_update;
#endif
    s_refresh_first = job.is_last;

    if (!s_flush_worker_ready) {
        gc9307_process_flush_job(&job);
        lv_display_flush_ready(disp);
        gc9307_handle_transition_redraw(disp);
        return;
    }
    os_sem_set(&s_flush_done_sem, 0);
    s_flush_job = job;
    s_flush_job_pending = 1;
    if (job.sequence <= GC9307_ASYNC_DETAIL_LIMIT) {
        printf("[GC9307-ASYNC] logical_submit job=%u area=%d,%d-%d,%d first=%u te=%u last=%u transition=%u\n",
               job.sequence, area->x1, area->y1, area->x2, area->y2,
               job.starts_refresh ? 1U : 0U, job.waits_for_te ? 1U : 0U,
               job.is_last ? 1U : 0U, job.transition_id);
    }
    os_sem_post(&s_flush_job_sem);
}

static void disp_flush_wait(lv_display_t *disp)
{
    if (s_flush_ready_count == s_flush_submit_count) {
        return;
    }
    os_sem_pend(&s_flush_done_sem, 0);
    s_flush_ready_count++;
    if (s_flush_ready_count <= GC9307_ASYNC_DETAIL_LIMIT) {
        printf("[GC9307-ASYNC] logical_ready job=%u submit=%u ready=%u\n",
               s_flush_job.sequence, s_flush_submit_count,
               s_flush_ready_count);
    }
    lv_display_flush_ready(disp);
    gc9307_handle_transition_redraw(disp);
}

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
