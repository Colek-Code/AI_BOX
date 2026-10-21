/**
 * @file lv_port_disp_gc9307_transition.h
 * @brief GC9307 page-transition full-frame staging.
 * D++: Owned by the D_Code GC9307/HYN screen profile.
 */

#ifndef __LV_PORT_DISP_GC9307_TRANSITION_H__
#define __LV_PORT_DISP_GC9307_TRANSITION_H__

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef enum {
    GC9307_TRANSITION_STAGE_IGNORED = 0,
    GC9307_TRANSITION_STAGE_STAGED,
    GC9307_TRANSITION_STAGE_READY,
    GC9307_TRANSITION_STAGE_ERROR,
} gc9307_transition_stage_result_e;

typedef struct {
    uint8_t *frame_data;
    uint32_t frame_bytes;
    uint32_t logical_parts;
    uint32_t logical_bytes;
    uint32_t collect_ms;
    unsigned int transition_id;
} gc9307_transition_frame_t;

typedef void (*gc9307_transition_frame_observer_t)(
    const gc9307_transition_frame_t *frame, int transfer_result,
    void *user_data);

void gc9307_transition_arm(unsigned int transition_id);
/* Flush worker only. Animation frames use id zero and share the staging buffer. */
void gc9307_transition_arm_composed_frame(void);
void gc9307_transition_block_reuse(void);
unsigned int gc9307_transition_get_active_id(void);
gc9307_transition_stage_result_e gc9307_transition_stage(
    unsigned int transition_id, const lv_area_t *area,
    const uint8_t *px_map, bool is_last,
    gc9307_transition_frame_t *frame);
bool gc9307_transition_take_redraw_request(void);
void gc9307_transition_set_frame_observer(
    gc9307_transition_frame_observer_t observer, void *user_data);
void gc9307_transition_publish_frame(
    const gc9307_transition_frame_t *frame, int transfer_result);

#endif /* __LV_PORT_DISP_GC9307_TRANSITION_H__ */
