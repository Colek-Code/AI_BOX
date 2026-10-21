/**
 * @file qwen_subtitle_text.h
 * @brief Shared Qwen text cache and sentence preparation for both root UIs.
 */
#ifndef __QWEN_SUBTITLE_TEXT_H__
#define __QWEN_SUBTITLE_TEXT_H__

#include <stdint.h>
#include "lvgl.h"
#include "framework/d_error.h"

#define QWEN_SUBTITLE_TEXT_MAX           2048U
#define QWEN_SUBTITLE_SENTENCE_MAX       32U
#define QWEN_SUBTITLE_SENTENCE_TEXT_MAX  192U

typedef enum {
    QWEN_SUBTITLE_STATE_IDLE = 0,
    QWEN_SUBTITLE_STATE_STREAMING,
    QWEN_SUBTITLE_STATE_FINAL,
} qwen_subtitle_state_t;

typedef struct {
    const char (*sentences)[QWEN_SUBTITLE_SENTENCE_TEXT_MAX];
    uint8_t sentence_count;
    uint8_t is_clipped;
    qwen_subtitle_state_t state;
    d_err_t init_result;
    uint32_t revision;
} qwen_subtitle_render_t;

/** Subscribe once; a failed partial subscription is rolled back. */
d_err_t qwen_subtitle_init(void);
/** LVGL-thread only. Sentence storage remains valid until the next prepare. */
uint8_t qwen_subtitle_prepare(uint8_t force, const lv_font_t *font,
                              qwen_subtitle_render_t *render);
const char *qwen_subtitle_status_text(qwen_subtitle_state_t state,
                                            uint8_t is_clipped);

#endif /* __QWEN_SUBTITLE_TEXT_H__ */
