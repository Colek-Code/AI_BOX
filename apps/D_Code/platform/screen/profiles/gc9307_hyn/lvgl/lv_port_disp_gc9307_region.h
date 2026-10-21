/**
 * @file lv_port_disp_gc9307_region.h
 * @brief Collect one requested image region before a single physical transfer.
 * @author D_Code
 * @date 2026-09-12
 */
#ifndef __LV_PORT_DISP_GC9307_REGION_H__
#define __LV_PORT_DISP_GC9307_REGION_H__
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#define GC9307_REGION_CAPACITY_BYTES (64U * 1024U)

typedef struct {
    unsigned int id;
    lv_area_t logical;
} gc9307_region_request_t;

typedef struct {
    const lv_area_t *logical;
    const uint8_t *pixels;
    uint32_t stride;
    uint32_t now_ms;
    bool is_last_refresh;
} gc9307_region_part_t;

typedef struct {
    uint8_t *pixels;
    lv_area_t physical;
    uint32_t bytes;
    uint32_t parts;
    uint32_t collect_ms;
    unsigned int request_id;
} gc9307_region_frame_t;

typedef enum {
    GC9307_REGION_BYPASS = 0,
    GC9307_REGION_STAGED,
    GC9307_REGION_READY,
    GC9307_REGION_ERROR,
} gc9307_region_result_e;

bool gc9307_region_area_valid(const lv_area_t *area);
/* Flush owner only. A READY buffer stays borrowed until its DMA has completed. */
gc9307_region_result_e gc9307_region_stage(const gc9307_region_request_t *request,
                                          const gc9307_region_part_t *part,
                                          gc9307_region_frame_t *frame);
void gc9307_region_discard(void);
/* Unconfirmed transport completion pins this buffer until reboot; ordinary partials may continue. */
void gc9307_region_block_reuse(void);
/* UI reads this only after the flush owner acknowledges completion. */
unsigned int gc9307_region_take_redraw_request(void);
#endif
