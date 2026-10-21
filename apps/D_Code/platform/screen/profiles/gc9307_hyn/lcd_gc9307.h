/**
 * @file lcd_gc9307.h
 * @brief GC9307-specific frame synchronization interface.
 */

#ifndef __LCD_GC9307_H__
#define __LCD_GC9307_H__

#include <stdint.h>

typedef enum {
    D_LCD_GC9307_TE_WAIT_TIMEOUT = -1,
    D_LCD_GC9307_TE_WAIT_EDGE = 0,
    D_LCD_GC9307_TE_WAIT_BYPASSED = 1,
} d_lcd_gc9307_te_wait_result_e;

typedef struct {
    uint32_t irq_count;
    uint32_t wait_ok_count;
    uint32_t timeout_count;
    uint32_t sem_post_count;
    uint32_t stale_wakeup_count;
    uint32_t request_count;
    uint32_t last_irq_ms;
    uint32_t irq_period_ms;
    uint32_t last_request_ms;
    uint32_t last_request_irq_count;
    uint32_t last_edge_ms;
    uint32_t last_edge_irq_count;
    uint32_t last_request_to_edge_ms;
    uint8_t is_registered;
} d_lcd_gc9307_te_stats_t;

typedef struct {
    uint32_t submit_count;
    uint32_t callback_count;
    uint32_t complete_count;
    uint32_t failure_count;
    uint32_t last_submit_ms;
} d_lcd_gc9307_transfer_stats_t;

typedef struct {
    uint16_t xs;
    uint16_t xe;
    uint16_t ys;
    uint16_t ye;
    uint8_t *data;
    uint32_t len;
} d_lcd_gc9307_flush_t;

/* D++: The LVGL port owns frame boundaries; the LCD driver only waits for one edge on request. */
d_lcd_gc9307_te_wait_result_e d_lcd_gc9307_wait_te_frame(void);
void d_lcd_gc9307_get_te_stats(d_lcd_gc9307_te_stats_t *stats);

int d_lcd_gc9307_flush_start(const d_lcd_gc9307_flush_t *flush);
int d_lcd_gc9307_prepare_area(uint16_t xs, uint16_t xe,
                              uint16_t ys, uint16_t ye);
int d_lcd_gc9307_prepare_write(void);
int d_lcd_gc9307_flush_pixels_start(uint8_t *data, uint32_t len);
int d_lcd_gc9307_flush_wait(uint32_t timeout_ticks);
void d_lcd_gc9307_get_transfer_stats(
    d_lcd_gc9307_transfer_stats_t *stats);

#endif /* __LCD_GC9307_H__ */
