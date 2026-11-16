/**
 * @file lv_port_disp_gc9307.c
 * @brief 实现 GC9307 LCD-SPI 的 LVGL 显示适配及单执行者刷新作业。
 * D++: 属于 D_Code GC9307/HYN 配置，不修改共用 LVGL 源码。
 */

#define BOOL_DEFINE_CONFLICT
#include "framework/d_lvgl_port.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307_trace.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307_transition.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_indev_gc9307.h"
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

#if LV_COLOR_DEPTH == 16
#define LV_PIXEL_COLOR_T lv_color16_t
#else
#error "GC9307 LVGL port requires LV_COLOR_DEPTH == 16"
#endif

#if (D_UI_TARGET == D_UI_PRODUCT)
/* 两个全宽批次避免浮层动画中六次绘制/刷新交接。
 * 原有三份缓冲一起增大，总计多 153600 静态字节，不新增所有者。 */
#define GC9307_LVGL_PARTIAL_ROWS   120
#else
#define GC9307_LVGL_PARTIAL_ROWS   40
#endif
#define GC9307_LVGL_PARTIAL_PIXELS (MY_DISP_HOR_RES * GC9307_LVGL_PARTIAL_ROWS)
#if (D_UI_TARGET == D_UI_PRODUCT)
#define GC9307_LVGL_TIMER_PERIOD_MS D_PRODUCT_UI_FRAME_PERIOD_MS
#else
#define GC9307_LVGL_TIMER_PERIOD_MS 16U
#endif
#if (D_UI_TARGET == D_UI_PRODUCT)
#define GC9307_FLUSH_TASK_PRIO       D_PRODUCT_UI_FLUSH_TASK_PRIORITY
#else
#define GC9307_FLUSH_TASK_PRIO       2
#endif
#define GC9307_FLUSH_TASK_STACK      2048
#define GC9307_DMA_TIMEOUT_TICKS     20
#define GC9307_ASYNC_DETAIL_LIMIT    10
/* 仅改善缓存局部性，不改变传输大小或面板时序。 */
#define GC9307_PARTIAL_COPY_TILE_PIXELS 16

static bool s_disp_flush_enabled = true;
static bool s_refresh_first = true;
/* UI 请求在一轮刷新首片锁存，后续所有条带复用同一身份。 */
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
/* 仅刷新所有者写此缓冲；传输完成不确定时保留到重启，
 * 与完整合帧及区域缓冲采用各自独立的保留门禁。 */
static bool s_wire_reuse_blocked;

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
    gc9307_frame_orientation_t orientation;
#if D_CAMERA_PREVIEW_ENABLE
    gc9307_region_request_t region;
#endif
} gc9307_flush_job_t;

static gc9307_flush_job_t s_flush_job;
static OS_SEM s_flush_job_sem;
static OS_SEM s_flush_done_sem;
static volatile uint32_t s_flush_submit_count;
static volatile uint32_t s_flush_ready_count;
#if D_DEBUG_UI_PERFORMANCE
/* 只由 LVGL 执行者更新/读取，DMA 工作任务不写这些统计。 */
static gc9307_ui_wait_stats_t s_ui_wait_stats;
#endif
static volatile uint8_t s_flush_job_pending;
static bool s_flush_worker_ready;
static gc9307_rotation_t s_rotation;
static gc9307_frame_orientation_t s_refresh_orientation;
static struct {
    uint32_t request_id;
    uint32_t generation;
    d_err_t result;
    uint8_t pending;
} s_rotation_completion;

#if D_CAMERA_PREVIEW_ENABLE
/* UI 配置复制到每份作业，只有刷新所有者能写 staging 像素。 */
static gc9307_region_request_t s_region_update;
static unsigned int s_region_next_id;
static uint32_t s_region_frame_count;
static uint32_t s_region_report_ms;
static uint32_t s_region_report_frames;

/**
 * @brief 登记并冻结区域收集目标；完整合同见 lv_port_disp_gc9307.h。
 */
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

/**
 * @brief 按编号取消后续区域预约；完整合同见 lv_port_disp_gc9307.h。
 */
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

/**
 * @brief 复制当前渲染方向身份；完整合同见 lv_port_disp_gc9307.h。
 */
void lv_port_disp_gc9307_get_orientation(gc9307_frame_orientation_t *orientation)
{
    if (orientation == NULL) return;
    OS_ENTER_CRITICAL();
    orientation->angle = gc9307_rotation_angle(s_rotation.render_degrees);
    orientation->generation = s_rotation.snapshot.generation;
    orientation->request_id = s_rotation.snapshot.is_busy ? s_rotation.snapshot.request_id : 0U;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 读取方向输入门禁；完整合同见 lv_port_disp_gc9307.h。
 */
uint8_t lv_port_disp_gc9307_input_blocked(void)
{
    return s_rotation.phase != GC9307_ROTATION_IDLE;
}

/**
 * @brief 复制方向事实快照；完整合同见 platform/screen/d_display.h。
 */
void d_display_get_rotation_snapshot(d_display_rotation_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    OS_ENTER_CRITICAL();
    *snapshot = s_rotation.snapshot;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 校验显示准入后受理方向请求；完整合同见 platform/screen/d_display.h。
 */
d_err_t d_display_request_rotation(uint16_t degrees, uint32_t *request_id)
{
    d_err_t result;
    if (request_id) *request_id = 0U;
    if (degrees != 0U && degrees != 180U) return D_ERR_INVALID_ARG;
    if (!s_rotation.snapshot.is_available || !s_disp_flush_enabled) return D_ERR_NOT_READY;
    if (gc9307_transition_get_active_id() != 0U) return D_ERR_BUSY;
    OS_ENTER_CRITICAL();
    result = gc9307_rotation_request(&s_rotation, degrees, request_id);
    OS_EXIT_CRITICAL();
    return result;
}

/**
 * @brief 首帧前装配方向；完整合同见 platform/screen/d_display.h。
 */
d_err_t d_display_set_initial_rotation(uint16_t degrees)
{
    lv_display_t *disp = lv_display_get_default();
    if (degrees != 0U && degrees != 180U) return D_ERR_INVALID_ARG;
    if (!disp || !s_rotation.snapshot.is_available) return D_ERR_NOT_READY;
    if (s_flush_submit_count || s_rotation.snapshot.is_busy) return D_ERR_BUSY;
    OS_ENTER_CRITICAL();
    gc9307_rotation_init(&s_rotation, degrees);
    s_rotation.snapshot.is_available = 1U;
    OS_EXIT_CRITICAL();
    lv_display_set_rotation(disp, degrees ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
    return D_OK;
}

/**
 * @brief 在 UI 线程接纳完成并推进方向事务；完整合同见 platform/screen/d_display.h。
 */
void d_display_process_rotation(void)
{
    lv_display_t *disp = lv_display_get_default();
    gc9307_rotation_phase_e before;
    bool redraw = false;
    d_lcd_gc9307_transfer_stats_t transfer;
    if (!disp || !s_rotation.snapshot.is_busy || s_flush_job_pending || !s_refresh_first) return;
    if (s_flush_worker_ready) disp_flush_wait(disp);
    d_lcd_gc9307_get_transfer_stats(&transfer);
    before = s_rotation.phase;
    OS_ENTER_CRITICAL();
    if (s_rotation_completion.pending) {
        gc9307_rotation_complete(&s_rotation, s_rotation_completion.request_id,
                                  s_rotation_completion.generation, s_rotation_completion.result);
        s_rotation_completion.pending = 0U;
    }
    if (s_rotation.phase == GC9307_ROTATION_DRAIN && gc9307_transition_get_active_id() == 0U) {
        if (transfer.submit_count != transfer.complete_count) {
            /* 工作任务返回不证明更早的 DMA 已停止读取源缓冲。 */
            s_rotation.phase = GC9307_ROTATION_FAULT;
            s_rotation.snapshot.result = D_ERR_IO;
        } else {
            gc9307_rotation_drained(&s_rotation);
            redraw = true;
        }
    }
    redraw |= before != GC9307_ROTATION_ROLLBACK && s_rotation.phase == GC9307_ROTATION_ROLLBACK;
    OS_EXIT_CRITICAL();
    if (redraw) {
        lv_port_indev_gc9307_reset();
        lv_display_set_rotation(disp, s_rotation.render_degrees ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
        lv_port_disp_gc9307_request_composed_frame();
    }
    if (before != GC9307_ROTATION_IDLE && s_rotation.phase == GC9307_ROTATION_IDLE) {
        lv_port_indev_gc9307_reset();
        printf("[DISPLAY-ROTATION] completed=%u actual=%u generation=%u result=%d\n",
               s_rotation.snapshot.completed_id, s_rotation.snapshot.actual_degrees,
               s_rotation.snapshot.generation, s_rotation.snapshot.result);
    }
    if (s_rotation.phase == GC9307_ROTATION_FAULT) {
        s_disp_flush_enabled = false;
    }
}

/**
 * @brief 刷新所有者向 UI 投递当前方向事务的值类型完成。
 * @param[in] orientation 本帧冻结的方向、请求编号及代次，可为 NULL。
 * @param[in] result 已确认 DMA 成功、DMA 前失败或完成不确定的结果。
 * @note 临界区内仅登记匹配当前票据及代次且尚无待取结果的一次完成；不在工作任务调用 LVGL 或保存偏好。
 */
static void gc9307_rotation_post_completion(const gc9307_frame_orientation_t *orientation,
                                           d_err_t result)
{
    if (orientation == NULL || !orientation->request_id) return;
    OS_ENTER_CRITICAL();
    if (!s_rotation.snapshot.is_busy || s_rotation_completion.pending ||
        orientation->request_id != s_rotation.snapshot.request_id ||
        orientation->generation != s_rotation.snapshot.generation) {
        OS_EXIT_CRITICAL();
        return;
    }
    s_rotation_completion.request_id = orientation->request_id;
    s_rotation_completion.generation = orientation->generation;
    s_rotation_completion.result = result;
    s_rotation_completion.pending = 1U;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 收取合帧或区域失败的重绘意图，推迟到刷新就绪事件再失效屏幕。
 * @param[in] disp 显示回调传入的描述符，当前未直接使用。
 * @note flush_wait 可能发生在渲染过程中，不能当场发起屏幕失效；区域取消仅影响对应请求。
 */
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

    /* flush_wait 可能在渲染内部调用，须等 REFR_READY 再使屏幕失效。 */
    s_composed_redraw_pending |= redraw;
}

/**
 * @brief 登记完整页面过渡；完整合同见 lv_port_disp_gc9307.h。
 */
void lv_port_disp_gc9307_begin_full_frame_transition(unsigned int transition_id)
{
    gc9307_transition_arm(transition_id);
}

/**
 * @brief 请求下一轮完整合帧；完整合同见 lv_port_disp_gc9307.h。
 */
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

/**
 * @brief 在 LVGL 刷新就绪事件中消费延迟完整重绘意图。
 * @param[in] event LVGL 事件对象，当前未使用其字段。
 */
static void gc9307_refresh_ready(lv_event_t *event)
{
    (void)event;
    if (s_composed_redraw_pending) {
        s_composed_redraw_pending = false;
        lv_port_disp_gc9307_request_composed_frame();
    }
}


/**
 * @brief 按作业安装角映射逻辑矩形到面板扫描矩形并校验范围。
 * @param[in] logical 待传输逻辑矩形。
 * @param[out] physical 映射矩形；范围错误时可能已写入，不可继续传输。
 * @param[in] angle 作业冻结的安装角，90° 或 270°。
 * @return 映射有效为 true；空指针、倒置或超出物理边界为 false。
 */
static bool gc9307_map_rotated_area(const lv_area_t *logical, lv_area_t *physical, uint16_t angle)
{
    if ((logical == NULL) || (physical == NULL)) {
        return false;
    }

    if (angle == 90U) {
    physical->x1 = (lv_coord_t)(D_GC9307_PHYS_HOR_RES - 1 - logical->y2);
    physical->x2 = (lv_coord_t)(D_GC9307_PHYS_HOR_RES - 1 - logical->y1);
    physical->y1 = logical->x1;
    physical->y2 = logical->x2;
    } else {
    physical->x1 = logical->y1;
    physical->x2 = logical->y2;
    physical->y1 = (lv_coord_t)(D_GC9307_PHYS_VER_RES - 1 - logical->x2);
    physical->y2 = (lv_coord_t)(D_GC9307_PHYS_VER_RES - 1 - logical->x1);
    }

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

/**
 * @brief 把局部 RGB565 区域按行跨度、方向及线序复制到独占 wire 缓冲。
 * @param[in] area 逻辑局部矩形。
 * @param[in] px_map 本次 LVGL 像素缓冲，只读借用至刷新完成。
 * @param[out] physical 本次物理传屏矩形。
 * @param[in] angle 该作业冻结的安装角。
 * @return 成功为静态 wire 缓冲借用指针；资源被冻结、参数或区域无效时为 NULL。
 * @note 只由刷新所有者写入；不使用整帧跨度解释局部行填充，分块复制只改善缓存局部性，不改变缓冲所有权。
 */
static uint8_t *gc9307_prepare_rgb565(const lv_area_t *area, uint8_t *px_map,
                                      lv_area_t *physical, uint16_t angle)
{
    uint32_t src_stride;
    int width;
    int height;
    int pixels;
    int tile_x, x, y;
    int32_t pixel_step;

    if (s_wire_reuse_blocked ||
        (area == NULL) || (px_map == NULL) || (physical == NULL)) {
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

    if (!gc9307_map_rotated_area(area, physical, angle)) {
        return NULL;
    }

    /* D++: 局部矩形的行可能含对齐填充，不能沿用整宽缓冲的无填充假设。 */
    src_stride = lv_draw_buf_width_to_stride((uint32_t)width, LV_COLOR_FORMAT_RGB565);
    if (src_stride == 0) {
        if (!s_bad_stride_logged) {
            s_bad_stride_logged = true;
            printf("[GC9307-LVGL] invalid RGB565 stride width=%d\n", width);
        }
        return NULL;
    }

    pixel_step = angle == 90U ? height * 2 : -height * 2;
    /* 复用合帧复制的局部性思路，但不共享其所有权或物理跨度；
     * 小列块让转置目标的少量缓存行保持可用。 */
    for (tile_x = 0; tile_x < width; tile_x += GC9307_PARTIAL_COPY_TILE_PIXELS) {
        int end_x = tile_x + GC9307_PARTIAL_COPY_TILE_PIXELS;
        if (end_x > width) end_x = width;
        for (y = 0; y < height; y++) {
            const u16 *src_row = (const u16 *)(px_map + (uint32_t)y * src_stride);
            int32_t dst_offset = (angle == 90U ? tile_x * height + height - y - 1 :
                (width - tile_x - 1) * height + y) * 2;
            for (x = tile_x; x < end_x; x++) {
                u16 color = src_row[x];
                s_wire_buf[dst_offset] = (uint8_t)(color >> 8);
                s_wire_buf[dst_offset + 1] = (uint8_t)color;
                dst_offset += pixel_step;
            }
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

/**
 * @brief 准备面板窗口并按需等待 TE，再启动并等待一次像素传输。
 * @param[in] physical 物理扫描矩形，调用方已校验。
 * @param[in] wire 调用期间及任何未确认传输仍借用的像素缓冲。
 * @param[in] bytes 非零有效字节数。
 * @param[in] wait_for_te 是否在发送前沿既有策略等待 TE。
 * @param[out] timing 有效调用时写入等待和传输诊断；参数错误提前返回时未初始化。
 * @return 0 为异步 DMA 已确认；1 表示执行了 void 同步兼容路径，无法据此确认 DMA；负值为参数或传输失败。
 * @note 仅刷新所有者执行；上层按各自缓冲合同处理未确认完成。不能把兼容函数的 void 返回当成硬件完成确认。
 */
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

/**
 * @brief 按已校验逻辑矩形计算 RGB565 有效字节数。
 * @param[in] area 非空且坐标有序的矩形，由内部调用方保证。
 * @return 宽×高×2 的字节数。
 */
static uint32_t gc9307_logical_bytes(const lv_area_t *area)
{
    return (uint32_t)(area->x2 - area->x1 + 1) *
           (uint32_t)(area->y2 - area->y1 + 1) * 2U;
}

#if D_CAMERA_PREVIEW_ENABLE
/**
 * @brief 传送已经完整收集的图像区域，并补齐传输探针。
 * @param[in] frame 区域模块借出的完整帧及冻结方向。
 * @param[in,out] trace 本次刷新探针，补充物理字节及耗时。
 * @return 沿底层返回：0 为 DMA 已确认；其他值不能作为确认成功。
 * @note 非零结果会使区域缓冲保留到重启；UART 诊断放在传输后，避免在图像分片间制造间隙。
 */
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
    /* 物理传输返回后再打印，避免串口诊断在图像条带之间制造间隙。 */
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

/**
 * @brief 收集本轮完整画面，齐帧后传输并通知方向及可选观察者。
 * @param[in] job 本次不可变刷新作业。
 * @param[in,out] trace 累计旋转、等待及传输事实的探针。
 * @return 0 可表示仍在收集或 DMA 已确认；1 为兼容传输路径；负值为合帧/传输失败。
 * @note 按作业冻结的方向与票据工作；失败不把 staged 当作已显示，传输非零保留合帧缓冲。采集失败也回报原票据，由观察者隔离迟到结果。
 */
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
    stage_result = gc9307_transition_stage(&job->orientation, job->transition_id,
                                           &job->logical, job->px_map,
                                           job->is_last, &frame);
    trace->rotate_ms = timer_get_ms() - stage_start_ms;
    if (stage_result == GC9307_TRANSITION_STAGE_READY) {
        result = gc9307_transfer_prepared(&physical, frame.frame_data,
                                           frame.frame_bytes, true, &timing);
        if (result != 0) {
            gc9307_transition_block_reuse();
        }
        gc9307_rotation_post_completion(&frame.orientation, result == 0 ? D_OK : D_ERR_IO);
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
        gc9307_rotation_post_completion(&job->orientation, D_ERR_INVALID_ARG);
    }
    return result;
}

/**
 * @brief 优先收集图像区域，否则转换并传送普通局部刷新。
 * @param[in] job 本次冻结方向及区域身份的刷新作业。
 * @param[in,out] trace 本次刷新探针。
 * @return 0 为已暂存区域或 DMA 已确认；1 为兼容路径；负值为失败。
 * @note 普通 wire 缓冲只在负传输返回后冻结至重启；兼容路径的底层完成合同仍未知，不据此宣布 DMA 成功。
 */
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
        .orientation = job->orientation,
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

    wire = gc9307_prepare_rgb565(&job->logical, job->px_map, &physical, job->orientation.angle);
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
        s_wire_reuse_blocked = true;
        return -1;
    }
    trace->wait_ms = timing.wait_ms;
    trace->te_to_dma_ms = timing.te_to_dma_ms;
    trace->tx_ms = timing.tx_ms;
    trace->wait_result = (int)timing.wait_result;
    trace->physical_dma_completed = (result == 0);
    return result;
}

/**
 * @brief 由唯一刷新所有者按作业类型执行收集/传输并记录帧探针。
 * @param[in] job UI 提交后保持不变的作业副本。
 * @note 刷新关闭时也按原路径完成 LVGL 作业确认；不从工作任务直接操作页面或复位输入。
 */
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

/**
 * @brief 串行取得单槽刷新作业，执行后通知 UI 的 flush-wait 边界。
 * @param[in] priv SDK 任务参数，当前未使用。
 * @note 任务返回本轮作业不等于所有底层 DMA 已确认；未确认缓冲由各自门禁继续保留。
 */
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

/**
 * @brief 创建刷新作业和完成信号量，再启动唯一刷新任务。
 * @return 成功为 0；基础设施或任务创建失败为 -1，并释放本次已创建的信号量。
 */
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

/**
 * @brief 对已存在的 LVGL 刷新和动画定时器应用当前配置周期。
 * @param[in] disp 有效显示描述符。
 * @note 只配置当前已有定时器，不修改触摸轮询、面板时钟或 TE 参数。
 */
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

/**
 * @brief 注册 GC9307 LVGL 显示、双局部缓冲及刷新执行者；通用合同见 framework/d_lvgl_port.h。
 */
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
    gc9307_rotation_init(&s_rotation, D_SCREEN_DEFAULT_ROTATION_DEGREES);
#if (D_UI_TARGET == D_UI_PRODUCT)
    s_rotation.snapshot.is_available = 1U;
#endif
    lv_display_set_rotation(disp, D_SCREEN_DEFAULT_ROTATION_DEGREES ?
                           LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
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
           "partial_rows=%d async=%u buffers=2 frame_te=1 timer_ms=%d flush_prio=%u\n",
           MY_DISP_HOR_RES, MY_DISP_VER_RES, D_GC9307_PHYS_HOR_RES, D_GC9307_PHYS_VER_RES,
           gc9307_rotation_angle(s_rotation.render_degrees), GC9307_LVGL_PARTIAL_ROWS,
           s_flush_worker_ready ? 1U : 0U, GC9307_LVGL_TIMER_PERIOD_MS, GC9307_FLUSH_TASK_PRIO);
}

/**
 * @brief 允许后续刷新作业发送；通用合同见 framework/d_lvgl_port.h。
 */
void disp_enable_update(void)
{
    s_disp_flush_enabled = true;
}

/**
 * @brief 禁止后续刷新作业发送；通用合同见 framework/d_lvgl_port.h。
 */
void disp_disable_update(void)
{
    s_disp_flush_enabled = false;
}

/**
 * @brief 调用板级已存在的 LCD 用户初始化入口。
 */
static void disp_init(void)
{
    user_lcd_init();
}

/**
 * @brief 在 UI 刷新回调中冻结本轮方向及请求身份，并提交唯一刷新作业。
 * @param[in] disp LVGL 显示对象。
 * @param[in] area 本次逻辑局部矩形，立即按值复制。
 * @param[in] px_map LVGL 像素缓冲，须保持到 flush-ready。
 * @note 方向完成待 UI 接纳时暂停新提交并保留重绘意图；无工作任务时沿原同步路径执行，不新增帧副本。
 */
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    gc9307_flush_job_t job;
    bool hold_for_rotation;

    OS_ENTER_CRITICAL();
    hold_for_rotation = s_rotation_completion.pending != 0U;
    OS_EXIT_CRITICAL();
    if (hold_for_rotation) {
        /* 前一作业已到 LVGL 等待边界，但方向完成仍待 UI 接纳；不得先提交后继作业。
         * UI 切换或回滚前仍确认刷新所有者结束，保留排队采集/区域请求，
         * 并补回已被 LVGL 消费的失效重绘意图。 */
        s_refresh_first = lv_display_flush_is_last(disp);
        s_composed_redraw_pending = true;
        lv_display_flush_ready(disp);
        return;
    }

    if (s_refresh_first) {
        lv_port_disp_gc9307_get_orientation(&s_refresh_orientation);
        s_refresh_transition_id = gc9307_transition_get_active_id();
        s_refresh_composed = s_composed_frame_pending && s_refresh_transition_id == 0U;
        s_composed_frame_pending = false;
    }
    job.logical = *area;
    job.orientation = s_refresh_orientation;
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

/**
 * @brief 在 UI 线程等待一个已提交作业并向 LVGL 归还绘制缓冲。
 * @param[in] disp LVGL 显示对象。
 * @note 已有作业未归还时使用既有无限等待信号量；统计包含调度延迟。只确认逻辑作业，底层未确认资源仍由显示门禁保留。
 */
static void disp_flush_wait(lv_display_t *disp)
{
#if D_DEBUG_UI_PERFORMANCE
    uint32_t wait_started_ms;
#endif
    if (s_flush_ready_count == s_flush_submit_count) {
        return;
    }
#if D_DEBUG_UI_PERFORMANCE
    wait_started_ms = timer_get_ms();
#endif
    os_sem_pend(&s_flush_done_sem, 0);
#if D_DEBUG_UI_PERFORMANCE
    s_ui_wait_stats.total_ms += timer_get_ms() - wait_started_ms;
    s_ui_wait_stats.count++;
#endif
    s_flush_ready_count++;
    if (s_flush_ready_count <= GC9307_ASYNC_DETAIL_LIMIT) {
        printf("[GC9307-ASYNC] logical_ready job=%u submit=%u ready=%u\n",
               s_flush_job.sequence, s_flush_submit_count,
               s_flush_ready_count);
    }
    lv_display_flush_ready(disp);
    gc9307_handle_transition_redraw(disp);
}

#if D_DEBUG_UI_PERFORMANCE
/**
 * @brief 复制 UI 等待诊断计数；完整合同见 lv_port_disp_gc9307.h。
 */
void lv_port_disp_gc9307_get_ui_wait_stats(gc9307_ui_wait_stats_t *out)
{
    if (out == NULL) return;
    *out = s_ui_wait_stats;
    out->submitted = s_flush_submit_count;
    out->ready = s_flush_ready_count;
}
#endif

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
