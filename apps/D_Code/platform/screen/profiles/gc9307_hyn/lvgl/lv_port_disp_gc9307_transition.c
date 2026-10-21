/**
 * @file lv_port_disp_gc9307_transition.c
 * @brief Collect LVGL partial strips into one physical GC9307 frame.
 * D++: Owned by the D_Code GC9307/HYN screen profile.
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
} gc9307_transition_context_t;

static uint8_t s_transition_frame[GC9307_FRAME_BYTES]
       __attribute__((aligned(32)));
static gc9307_transition_context_t s_transition;
static bool s_frame_reuse_blocked;
static gc9307_transition_frame_observer_t s_frame_observer;
static void *s_frame_observer_user_data;

static void gc9307_transition_reset(bool preserve_redraw)
{
    bool redraw_pending = s_transition.redraw_pending;

    memset(&s_transition, 0, sizeof(s_transition));
    if (preserve_redraw) {
        s_transition.redraw_pending = redraw_pending;
    }
}

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

static bool gc9307_transition_copy(const lv_area_t *area,
                                   const uint8_t *px_map)
{
    uint32_t src_stride;
    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;
    int x;
    int y;

    src_stride = lv_draw_buf_width_to_stride((uint32_t)width,
                                             LV_COLOR_FORMAT_RGB565);
    if ((src_stride == 0) || (px_map == NULL)) {
        return false;
    }

    for (y = 0; y < height; y++) {
        const uint16_t *src_row =
            (const uint16_t *)(px_map + (uint32_t)y * src_stride);
        uint32_t logical_y = (uint32_t)area->y1 + (uint32_t)y;

        for (x = 0; x < width; x++) {
            uint32_t logical_x = (uint32_t)area->x1 + (uint32_t)x;
            uint32_t physical_x;
            uint32_t physical_y;
            uint32_t dst_offset;
            uint16_t color = src_row[x];

#if D_GC9307_ROTATION_DEGREES == 90
            physical_x = D_GC9307_PHYS_HOR_RES - 1U - logical_y;
            physical_y = logical_x;
#else
            physical_x = logical_y;
            physical_y = D_GC9307_PHYS_VER_RES - 1U - logical_x;
#endif
            dst_offset = (physical_y * D_GC9307_PHYS_HOR_RES +
                          physical_x) * 2U;
            s_transition_frame[dst_offset] = (uint8_t)(color >> 8);
            s_transition_frame[dst_offset + 1U] = (uint8_t)color;
        }
    }
    return true;
}

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

void gc9307_transition_arm_composed_frame(void)
{
    gc9307_transition_reset(false);
    s_transition.state = GC9307_TRANSITION_ARMED;
}

void gc9307_transition_block_reuse(void)
{
    /* A timed-out DMA may still read this buffer; only a reboot makes reuse safe. */
    s_frame_reuse_blocked = true;
}

unsigned int gc9307_transition_get_active_id(void)
{
    return s_transition.transition_id;
}

gc9307_transition_stage_result_e gc9307_transition_stage(
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
    if (!gc9307_transition_area_valid(area)) {
        return gc9307_transition_fail(area, is_last);
    }
    if (s_transition.state == GC9307_TRANSITION_ARMED) {
        s_transition.state = GC9307_TRANSITION_COLLECTING;
        s_transition.collect_start_ms = timer_get_ms();
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
    gc9307_transition_reset(false);
    return GC9307_TRANSITION_STAGE_READY;
}

bool gc9307_transition_take_redraw_request(void)
{
    bool redraw_pending = s_transition.redraw_pending;

    s_transition.redraw_pending = false;
    return redraw_pending;
}

void gc9307_transition_set_frame_observer(
    gc9307_transition_frame_observer_t observer, void *user_data)
{
    s_frame_observer = observer;
    s_frame_observer_user_data = user_data;
}

void gc9307_transition_publish_frame(
    const gc9307_transition_frame_t *frame, int transfer_result)
{
    if ((frame != NULL) && (s_frame_observer != NULL)) {
        s_frame_observer(frame, transfer_result,
                         s_frame_observer_user_data);
    }
}

#else

void gc9307_transition_arm(unsigned int transition_id)
{
    (void)transition_id;
}

void gc9307_transition_arm_composed_frame(void)
{
}

void gc9307_transition_block_reuse(void)
{
}

unsigned int gc9307_transition_get_active_id(void)
{
    return 0;
}

gc9307_transition_stage_result_e gc9307_transition_stage(
    unsigned int transition_id, const lv_area_t *area,
    const uint8_t *px_map, bool is_last,
    gc9307_transition_frame_t *frame)
{
    (void)transition_id;
    (void)area;
    (void)px_map;
    (void)is_last;
    (void)frame;
    return GC9307_TRANSITION_STAGE_IGNORED;
}

bool gc9307_transition_take_redraw_request(void)
{
    return false;
}

void gc9307_transition_set_frame_observer(
    gc9307_transition_frame_observer_t observer, void *user_data)
{
    (void)observer;
    (void)user_data;
}

void gc9307_transition_publish_frame(
    const gc9307_transition_frame_t *frame, int transfer_result)
{
    (void)frame;
    (void)transfer_result;
}

#endif /* (D_UI_TARGET == D_UI_PRODUCT) */

#endif
