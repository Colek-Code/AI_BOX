/**
 * @file lv_port_disp_gc9307_trace.h
 * @brief Bounded GC9307 frame and TE timing diagnostics.
 * D++: Owned by the D_Code GC9307/HYN screen profile.
 */

#ifndef __LV_PORT_DISP_GC9307_TRACE_H__
#define __LV_PORT_DISP_GC9307_TRACE_H__

#include <stdbool.h>
#include <stdint.h>
#include "config/app_config.h"
#include "lvgl.h"

typedef struct {
    const lv_area_t *logical;
    uint32_t logical_bytes;
    uint32_t physical_bytes;
    uint32_t rotate_ms;
    uint32_t wait_ms;
    uint32_t te_to_dma_ms;
    uint32_t tx_ms;
    int wait_result;
    int transfer_result;
    bool physical_dma_completed;
    bool is_first;
    bool waited_for_te;
    bool is_last;
    unsigned int transition_id;
} gc9307_port_trace_part_t;

#ifdef D_GC9307_FRAME_TRACE_ENABLE
void gc9307_port_trace_frame_begin(uint32_t now_ms,
                                   unsigned int transition_id);
void gc9307_port_trace_part(const gc9307_port_trace_part_t *part);
void gc9307_port_trace_frame_finish(uint32_t now_ms);
#else
#define gc9307_port_trace_frame_begin(now_ms, transition_id) ((void)0)
#define gc9307_port_trace_part(part) ((void)0)
#define gc9307_port_trace_frame_finish(now_ms) ((void)0)
#endif

#endif /* __LV_PORT_DISP_GC9307_TRACE_H__ */
