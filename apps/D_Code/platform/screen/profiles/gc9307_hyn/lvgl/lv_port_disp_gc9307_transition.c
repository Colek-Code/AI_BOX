/**
 * @file lv_port_disp_gc9307_transition.c
 * @brief 将 LVGL 局部条带收集成一张物理完整帧，不新增帧副本。
 * D++: 归属 D_Code GC9307/HYN 显示配置。
 */

#define BOOL_DEFINE_CONFLICT
#include "lv_port_disp_gc9307_transition.h"
#include "config/app_config.h"
#include "system/includes.h"
#include "system/sys_time.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#if (D_UI_TARGET == D_UI_PRODUCT)

#define GC9307_FRAME_PIXELS \
    (D_GC9307_PHYS_HOR_RES * D_GC9307_PHYS_VER_RES)
#define GC9307_FRAME_BYTES (GC9307_FRAME_PIXELS * 2U)
#define GC9307_TRANSITION_COPY_TILE_PIXELS 16

typedef enum {
    GC9307_TRANSITION_IDLE = 0,
    GC9307_TRANSITION_ARMED,
    GC9307_TRANSITION_COLLECTING,
} gc9307_transition_state_e;

typedef struct {
    gc9307_transition_state_e state;
    unsigned int transition_id;
    uint32_t expected_y;
    uint32_t logical_parts;
    uint32_t logical_bytes;
    uint32_t collect_start_ms;
    bool redraw_pending;
    gc9307_frame_orientation_t orientation;
} gc9307_transition_context_t;

static uint8_t s_transition_frame[GC9307_FRAME_BYTES]
       __attribute__((aligned(32)));
static gc9307_transition_context_t s_transition;
static bool s_frame_reuse_blocked;
static gc9307_transition_frame_observer_t s_frame_observer;
static void *s_frame_observer_user_data;

/**
 * @brief 重置收集元数据，并按参数保留重绘意图。
 * @param[in] preserve_redraw 非零表示保留已有重绘标志。
 * @note 不清像素数据或永久复用门禁；不是底层传输停止操作。
 */
static void gc9307_transition_reset(bool preserve_redraw)
{
    bool redraw_pending = s_transition.redraw_pending;

    memset(&s_transition, 0, sizeof(s_transition));
    if (preserve_redraw) {
        s_transition.redraw_pending = redraw_pending;
    }
}

/**
 * @brief 校验条带是否覆盖完整逻辑宽度且紧接预期行。
 * @param[in] area 待收集的逻辑矩形。
 * @return 参数及范围有效为 true，否则为 false。
 */
static bool gc9307_transition_area_valid(const lv_area_t *area)
{
    if (area == NULL) {
        return false;
    }
    if ((area->x1 != 0) ||
        (area->x2 != (MY_DISP_HOR_RES - 1)) ||
        (area->y1 != (lv_coord_t)s_transition.expected_y) ||
        (area->y2 < area->y1) ||
        (area->y2 >= MY_DISP_VER_RES)) {
        return false;
    }
    return true;
}

/**
 * @brief 按冻结方向将一片 RGB565 数据转置写入完整帧缓冲。
 * @param[in] area 已通过顺序和范围校验的条带。
 * @param[in] px_map 按 LVGL 行跨度布局的有效像素缓冲。
 * @return 跨度和源指针有效且复制完成为 true，否则为 false。
 * @note 分块只改变写入顺序，不改变线序、事务身份或缓冲借用周期。
 */
static bool gc9307_transition_copy(const lv_area_t *area,
                                   const uint8_t *px_map)
{
    uint32_t src_stride;
    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;
    int tile_x, x, y;
    const int32_t pixel_step = s_transition.orientation.angle == 90U ?
        (int32_t)D_GC9307_PHYS_HOR_RES * 2 : -(int32_t)D_GC9307_PHYS_HOR_RES * 2;

    src_stride = lv_draw_buf_width_to_stride((uint32_t)width,
                                             LV_COLOR_FORMAT_RGB565);
    if ((src_stride == 0) || (px_map == NULL)) {
        return false;
    }

    /* 小源列块保持转置目标缓存局部性；只改变写入顺序，
     * 不改变事务身份、线序或缓冲借用周期。 */
    for (tile_x = 0; tile_x < width; tile_x += GC9307_TRANSITION_COPY_TILE_PIXELS) {
        int end_x = LV_MIN(tile_x + GC9307_TRANSITION_COPY_TILE_PIXELS, width);
        for (y = 0; y < height; y++) {
            const uint16_t *src_row = (const uint16_t *)(px_map + (uint32_t)y * src_stride);
            uint32_t logical_y = (uint32_t)area->y1 + (uint32_t)y;
            uint32_t logical_x = (uint32_t)area->x1 + (uint32_t)tile_x;
            int32_t dst_offset;
            if (s_transition.orientation.angle == 90U) {
                dst_offset = (int32_t)((logical_x * D_GC9307_PHYS_HOR_RES +
                    D_GC9307_PHYS_HOR_RES - 1U - logical_y) * 2U);
            } else {
                dst_offset = (int32_t)(((D_GC9307_PHYS_VER_RES - 1U - logical_x) *
                    D_GC9307_PHYS_HOR_RES + logical_y) * 2U);
            }
            for (x = tile_x; x < end_x; x++) {
                uint16_t color = src_row[x];
                s_transition_frame[dst_offset] = (uint8_t)(color >> 8);
                s_transition_frame[dst_offset + 1] = (uint8_t)color;
                dst_offset += pixel_step;
            }
        }
    }
    return true;
}

/**
 * @brief 报告收集失败并保留一次完整重绘意图。
 * @param[in] area 失败条带，可为 NULL，仅作诊断。
 * @param[in] is_last 本片是否最后一片，仅作诊断。
 * @return GC9307_TRANSITION_STAGE_ERROR。
 * @note 重置事务元数据不意味着未确认的 DMA 已释放。
 */
static gc9307_transition_stage_result_e gc9307_transition_fail(
    const lv_area_t *area, bool is_last)
{
    printf("[GC9307-TRANSITION] staging failed id=%u area=%d,%d-%d,%d "
           "expected_y=%u last=%u\n",
           s_transition.transition_id,
           area ? area->x1 : -1, area ? area->y1 : -1,
           area ? area->x2 : -1, area ? area->y2 : -1,
           s_transition.expected_y, is_last ? 1U : 0U);
    s_transition.redraw_pending = true;
    gc9307_transition_reset(true);
    return GC9307_TRANSITION_STAGE_ERROR;
}

/**
 * @brief 按非零过渡编号登记下一轮完整画面收集。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
void gc9307_transition_arm(unsigned int transition_id)
{
    if (transition_id == 0U) {
        return;
    }
    gc9307_transition_reset(false);
    s_transition.state = GC9307_TRANSITION_ARMED;
    s_transition.transition_id = transition_id;
    printf("[GC9307-TRANSITION] arm id=%u mode=full_frame\n",
           transition_id);
}

/**
 * @brief 由刷新所有者登记编号为零的普通动画合帧。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
void gc9307_transition_arm_composed_frame(void)
{
    gc9307_transition_reset(false);
    s_transition.state = GC9307_TRANSITION_ARMED;
}

/**
 * @brief 完成无法确认时永久禁止复用完整帧缓冲。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
void gc9307_transition_block_reuse(void)
{
    /* 超时 DMA 可能仍读取缓冲，当前实现将其保留至重启，不因迟到 IRQ 解锁。 */
    s_frame_reuse_blocked = true;
}

/**
 * @brief 读取当前页面过渡编号。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
unsigned int gc9307_transition_get_active_id(void)
{
    return s_transition.transition_id;
}

/**
 * @brief 按方向身份、条带顺序和末片事实收集一张完整物理帧。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
gc9307_transition_stage_result_e gc9307_transition_stage(
    const gc9307_frame_orientation_t *orientation,
    unsigned int transition_id, const lv_area_t *area,
    const uint8_t *px_map, bool is_last,
    gc9307_transition_frame_t *frame)
{
    uint32_t pixels;

    if (s_frame_reuse_blocked) {
        return GC9307_TRANSITION_STAGE_ERROR;
    }
    if ((transition_id != s_transition.transition_id) ||
        (s_transition.state == GC9307_TRANSITION_IDLE)) {
        return GC9307_TRANSITION_STAGE_IGNORED;
    }
    if (orientation == NULL || (orientation->angle != 90U && orientation->angle != 270U) ||
        !gc9307_transition_area_valid(area)) {
        return gc9307_transition_fail(area, is_last);
    }
    if (s_transition.state == GC9307_TRANSITION_ARMED) {
        s_transition.orientation = *orientation;
        s_transition.state = GC9307_TRANSITION_COLLECTING;
        s_transition.collect_start_ms = timer_get_ms();
    }
    if (orientation->angle != s_transition.orientation.angle ||
        orientation->generation != s_transition.orientation.generation ||
        orientation->request_id != s_transition.orientation.request_id) {
        return gc9307_transition_fail(area, is_last);
    }
    if (!gc9307_transition_copy(area, px_map)) {
        return gc9307_transition_fail(area, is_last);
    }

    pixels = (uint32_t)(area->x2 - area->x1 + 1) *
             (uint32_t)(area->y2 - area->y1 + 1);
    s_transition.expected_y = (uint32_t)area->y2 + 1U;
    s_transition.logical_parts++;
    s_transition.logical_bytes += pixels * 2U;
    if (!is_last) {
        if (s_transition.expected_y >= MY_DISP_VER_RES) {
            return gc9307_transition_fail(area, is_last);
        }
        return GC9307_TRANSITION_STAGE_STAGED;
    }
    if ((s_transition.expected_y != MY_DISP_VER_RES) || (frame == NULL)) {
        return gc9307_transition_fail(area, is_last);
    }

    frame->frame_data = s_transition_frame;
    frame->frame_bytes = sizeof(s_transition_frame);
    frame->logical_parts = s_transition.logical_parts;
    frame->logical_bytes = s_transition.logical_bytes;
    frame->collect_ms = timer_get_ms() - s_transition.collect_start_ms;
    frame->transition_id = s_transition.transition_id;
    frame->orientation = s_transition.orientation;
    gc9307_transition_reset(false);
    return GC9307_TRANSITION_STAGE_READY;
}

/**
 * @brief 取走并清除完整合帧失败后的重绘意图。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
bool gc9307_transition_take_redraw_request(void)
{
    bool redraw_pending = s_transition.redraw_pending;

    s_transition.redraw_pending = false;
    return redraw_pending;
}

/**
 * @brief 登记完整页面过渡帧的借用观察者及上下文。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
void gc9307_transition_set_frame_observer(
    gc9307_transition_frame_observer_t observer, void *user_data)
{
    s_frame_observer = observer;
    s_frame_observer_user_data = user_data;
}

/**
 * @brief 把本次完整页面过渡帧及传输结果通知已绑定观察者。 完整合同见 lv_port_disp_gc9307_transition.h。
 */
void gc9307_transition_publish_frame(
    const gc9307_transition_frame_t *frame, int transfer_result)
{
    if ((frame != NULL) && (s_frame_observer != NULL)) {
        s_frame_observer(frame, transfer_result,
                         s_frame_observer_user_data);
    }
}

#else

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_arm。
 */
void gc9307_transition_arm(unsigned int transition_id)
{
    (void)transition_id;
}

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_arm_composed_frame。
 */
void gc9307_transition_arm_composed_frame(void)
{
}

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_block_reuse。
 */
void gc9307_transition_block_reuse(void)
{
}

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_get_active_id。
 */
unsigned int gc9307_transition_get_active_id(void)
{
    return 0;
}

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_stage。
 */
gc9307_transition_stage_result_e gc9307_transition_stage(
    const gc9307_frame_orientation_t *orientation,
    unsigned int transition_id, const lv_area_t *area,
    const uint8_t *px_map, bool is_last,
    gc9307_transition_frame_t *frame)
{
    (void)orientation;
    (void)transition_id;
    (void)area;
    (void)px_map;
    (void)is_last;
    (void)frame;
    return GC9307_TRANSITION_STAGE_IGNORED;
}

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_take_redraw_request。
 */
bool gc9307_transition_take_redraw_request(void)
{
    return false;
}

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_set_frame_observer。
 */
void gc9307_transition_set_frame_observer(
    gc9307_transition_frame_observer_t observer, void *user_data)
{
    (void)observer;
    (void)user_data;
}

/**
 * @brief 未启用 Product UI 时保留兼容空实现；完整合同见 lv_port_disp_gc9307_transition.h 中的 gc9307_transition_publish_frame。
 */
void gc9307_transition_publish_frame(
    const gc9307_transition_frame_t *frame, int transfer_result)
{
    (void)frame;
    (void)transfer_result;
}

#endif /* (D_UI_TARGET == D_UI_PRODUCT) */

#endif
