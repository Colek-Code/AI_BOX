/**
 * @file lv_port_disp_gc9307_region.c
 * @brief Stage native RGB565 strips in physical scan order, with one bounded buffer.
 * @author D_Code
 * @date 2026-09-12
 */
#include "config/app_config.h"
#if D_CAMERA_PREVIEW_ENABLE
#include "lv_port_disp_gc9307_region.h"
#include <string.h>

typedef struct {
    gc9307_region_request_t request;
    uint32_t expected_y;
    uint32_t parts;
    uint32_t started_ms;
    unsigned int redraw_id;
    bool is_collecting;
    bool is_finished;
} gc9307_region_context_t;

static uint8_t s_region_pixels[GC9307_REGION_CAPACITY_BYTES] __attribute__((aligned(32)));
static gc9307_region_context_t s_region;
static bool s_region_reuse_blocked;

void gc9307_region_block_reuse(void)
{
    s_region_reuse_blocked = true;
}

bool gc9307_region_area_valid(const lv_area_t *area)
{
    uint32_t pixels;
    if (area == NULL || area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 || area->y2 < area->y1 ||
        area->x2 >= MY_DISP_HOR_RES || area->y2 >= MY_DISP_VER_RES) {
        return false;
    }
    pixels = (uint32_t)(area->x2 - area->x1 + 1) * (uint32_t)(area->y2 - area->y1 + 1);
    return pixels <= GC9307_REGION_CAPACITY_BYTES / 2U;
}

void gc9307_region_discard(void)
{
    /* Metadata only. The single flush worker completes DMA before writing pixels again. */
    memset(&s_region, 0, sizeof(s_region));
}

static gc9307_region_result_e gc9307_region_fail(void)
{
    s_region.redraw_id = s_region.request.id;
    s_region.is_collecting = false;
    s_region.is_finished = true;
    return GC9307_REGION_ERROR;
}

static void gc9307_region_copy(const gc9307_region_part_t *part)
{
    const lv_area_t *region = &s_region.request.logical;
    uint32_t region_height = (uint32_t)(region->y2 - region->y1 + 1);
    uint32_t width = (uint32_t)(region->x2 - region->x1 + 1);
    int row;
    uint32_t column;
    for (row = part->logical->y1; row <= part->logical->y2; row++) {
        const uint16_t *source = (const uint16_t *)(part->pixels +
            (uint32_t)(row - part->logical->y1) * part->stride);
        for (column = 0U; column < width; column++) {
            uint32_t offset;
            uint16_t color = source[column];
#if D_GC9307_ROTATION_DEGREES == 90
            offset = (column * region_height + (uint32_t)(region->y2 - row)) * 2U;
#else
            offset = ((width - column - 1U) * region_height + (uint32_t)(row - region->y1)) * 2U;
#endif
            s_region_pixels[offset] = (uint8_t)(color >> 8);
            s_region_pixels[offset + 1U] = (uint8_t)color;
        }
    }
}

gc9307_region_result_e gc9307_region_stage(const gc9307_region_request_t *request,
                                          const gc9307_region_part_t *part,
                                          gc9307_region_frame_t *frame)
{
    const lv_area_t *area;
    const lv_area_t *region;
    bool overlaps;
    if (request == NULL || part == NULL || part->logical == NULL || frame == NULL) {
        return GC9307_REGION_ERROR;
    }
    memset(frame, 0, sizeof(*frame));
    if (s_region_reuse_blocked) {
        return GC9307_REGION_BYPASS;
    }
    if (request->id == 0U || !gc9307_region_area_valid(&request->logical)) {
        return GC9307_REGION_BYPASS;
    }
    if (request->id != s_region.request.id) {
        unsigned int redraw_id = s_region.redraw_id;
        gc9307_region_discard();
        s_region.redraw_id = redraw_id;
        s_region.request = *request;
        s_region.expected_y = (uint32_t)request->logical.y1;
    }
    area = part->logical;
    region = &s_region.request.logical;
    overlaps = area->x1 <= region->x2 && area->x2 >= region->x1 &&
               area->y1 <= region->y2 && area->y2 >= region->y1;
    if (!overlaps) {
        if (part->is_last_refresh && s_region.is_collecting) {
            return gc9307_region_fail();
        }
        return GC9307_REGION_BYPASS;
    }
    if (s_region.is_finished) {
        return GC9307_REGION_BYPASS;
    }
    if (area->x1 != region->x1 || area->x2 != region->x2 ||
        area->y1 != (int)s_region.expected_y || area->y2 < area->y1 || area->y2 > region->y2) {
        if (s_region.is_collecting) {
            return gc9307_region_fail();
        }
        /* A merged full-screen/overlay redraw uses the established port path for this request. */
        s_region.is_finished = true;
        return GC9307_REGION_BYPASS;
    }
    if (part->pixels == NULL || ((uintptr_t)part->pixels & 1U) != 0U ||
        (part->stride & 1U) != 0U || part->stride < (uint32_t)(area->x2 - area->x1 + 1) * 2U) {
        return gc9307_region_fail();
    }
    if (!s_region.is_collecting) {
        s_region.is_collecting = true;
        s_region.started_ms = part->now_ms;
    }
    gc9307_region_copy(part);
    s_region.expected_y = (uint32_t)area->y2 + 1U;
    s_region.parts++;
    if (s_region.expected_y != (uint32_t)region->y2 + 1U) {
        return part->is_last_refresh ? gc9307_region_fail() : GC9307_REGION_STAGED;
    }

    frame->pixels = s_region_pixels;
    frame->bytes = (uint32_t)(region->x2 - region->x1 + 1) *
                   (uint32_t)(region->y2 - region->y1 + 1) * 2U;
    frame->parts = s_region.parts;
    frame->collect_ms = part->now_ms - s_region.started_ms;
    frame->request_id = request->id;
#if D_GC9307_ROTATION_DEGREES == 90
    frame->physical.x1 = D_GC9307_PHYS_HOR_RES - 1 - region->y2;
    frame->physical.x2 = D_GC9307_PHYS_HOR_RES - 1 - region->y1;
    frame->physical.y1 = region->x1;
    frame->physical.y2 = region->x2;
#else
    frame->physical.x1 = region->y1;
    frame->physical.x2 = region->y2;
    frame->physical.y1 = D_GC9307_PHYS_VER_RES - 1 - region->x2;
    frame->physical.y2 = D_GC9307_PHYS_VER_RES - 1 - region->x1;
#endif
    s_region.is_collecting = false;
    s_region.is_finished = true;
    return GC9307_REGION_READY;
}

unsigned int gc9307_region_take_redraw_request(void)
{
    unsigned int id = s_region.redraw_id;
    s_region.redraw_id = 0U;
    return id;
}
#endif
