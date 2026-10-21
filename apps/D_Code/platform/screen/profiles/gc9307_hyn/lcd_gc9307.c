#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_LCD_GC9307_SDK_LCD_ENABLE)

#include "system/includes.h"
#include "asm/gpio.h"
#include "asm/port_waked_up.h"
#include "platform/screen/lcd/d_lcd_compat.h"
#include "platform/screen/profiles/gc9307_hyn/lcd_gc9307.h"
#include "system/sys_time.h"

#define GC9307_LCD_W              240
#define GC9307_LCD_H              320
#define GC9307_INIT_MAX_DATA      6
#define GC9307_CLEAR_ROWS         8
#define GC9307_TE_WAIT_TICKS      4
#define GC9307_TE_TIMEOUT_LOG_STEP 50

#if D_GC9307_TE_EDGE_POSITIVE
#define GC9307_TE_WAKE_EDGE EDGE_POSITIVE
#define GC9307_TE_EDGE_NAME "positive"
#else
#define GC9307_TE_WAKE_EDGE EDGE_NEGATIVE
#define GC9307_TE_EDGE_NAME "negative"
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

typedef struct {
    u8 cmd;
    u8 len;
    u8 data[GC9307_INIT_MAX_DATA];
    u16 delay_ms;
} gc9307_init_cmd_t;

static const gc9307_init_cmd_t gc9307_init_cmds[] = {
    {0xfe, 0, {0}, 0},
    {0xef, 0, {0}, 0},
    {0x36, 1, {D_GC9307_MADCTL_VALUE}, 0},
    {0x3a, 1, {0x05}, 0},
    {0x86, 1, {0x98}, 0},
    {0x89, 1, {0x13}, 0},
    {0x8b, 1, {0x80}, 0},
    {0x8d, 1, {0x33}, 0},
    {0x8e, 1, {0x0f}, 0},
    {0xe8, 2, {D_GC9307_FRAME_RATE_E8_0, D_GC9307_FRAME_RATE_E8_1}, 0},
    {0xec, 3, {0x33, 0x07, 0x48}, 0},
    {0xff, 1, {0x62}, 0},
    {0x99, 1, {0x3e}, 0},
    {0x9d, 1, {0x4b}, 0},
    {0x98, 1, {0x3e}, 0},
    {0x9c, 1, {0x4b}, 0},
    {0xc3, 1, {0x16}, 0},
    {0xc4, 1, {0x26}, 0},
    {0xc9, 1, {0x29}, 0},
    {0xf0, 6, {0x0a, 0x0a, 0x07, 0x05, 0x02, 0x23}, 0},
    {0xf1, 6, {0x37, 0x57, 0x54, 0x21, 0x23, 0x5f}, 0},
    {0xf2, 6, {0x0a, 0x0d, 0x09, 0x07, 0x02, 0x2b}, 0},
    {0xf3, 6, {0x41, 0xbb, 0xbe, 0x21, 0x23, 0xdf}, 0},
    {0x35, 1, {0x00}, 0},
    {0x44, 2, {(D_GC9307_TE_SCANLINE >> 8) & 0xff,
               D_GC9307_TE_SCANLINE & 0xff}, 0},
    {0x11, 0, {0}, 120},
    {0x29, 0, {0}, 0},
};

static u8 gc9307_clear_buf[GC9307_LCD_W * GC9307_CLEAR_ROWS * 2] __attribute__((aligned(32)));
static u32 gc9307_transfer_submit_count;
static u32 gc9307_transfer_callback_count;
static u32 gc9307_transfer_complete_count;
static u32 gc9307_transfer_failure_count;
static u32 gc9307_transfer_callback_base;
static u8 gc9307_transfer_active;

#ifdef D_GC9307_TE_SYNC_ENABLE
static u32 gc9307_te_pin = (u32)-1;
static void *gc9307_te_wakeup_hdl = NULL;
static volatile u32 gc9307_te_irq_count = 0;
static u32 gc9307_te_timeout_count = 0;
static u32 gc9307_te_wait_ok_count = 0;
static volatile u32 gc9307_te_sem_post_count = 0;
static u32 gc9307_te_stale_wakeup_count = 0;
static u32 gc9307_te_request_count = 0;
static volatile u32 gc9307_te_last_irq_ms = 0;
static volatile u32 gc9307_te_irq_period_ms = 0;
static u32 gc9307_te_last_request_ms = 0;
static u32 gc9307_te_last_request_irq_count = 0;
static u32 gc9307_te_last_edge_ms = 0;
static u32 gc9307_te_last_edge_irq_count = 0;
static u32 gc9307_te_last_request_to_edge_ms = 0;
static OS_SEM gc9307_te_sem;
static volatile u8 gc9307_te_waiter_armed = 0;
static u8 gc9307_te_sem_ready = 0;
static u8 gc9307_te_registered = 0;
#endif

static void gc9307_reset(void)
{
    lcd_rst_pinstate(1);
    lcd_rs_pinstate(1);
    lcd_cs_pinstate(1);
    lcd_delay(50);

    lcd_rst_pinstate(0);
    lcd_delay(50);

    lcd_rst_pinstate(1);
    lcd_delay(120);
}

static void gc9307_set_draw_area(u16 xs, u16 xe, u16 ys, u16 ye)
{
    WriteCOM(0x2a);
    WriteDAT_8((u8)(xs >> 8));
    WriteDAT_8((u8)xs);
    WriteDAT_8((u8)(xe >> 8));
    WriteDAT_8((u8)xe);

    WriteCOM(0x2b);
    WriteDAT_8((u8)(ys >> 8));
    WriteDAT_8((u8)ys);
    WriteDAT_8((u8)(ye >> 8));
    WriteDAT_8((u8)ye);
}

static int gc9307_area_valid(u16 xs, u16 xe, u16 ys, u16 ye)
{
    if ((xs > xe) || (ys > ye)) {
        return 0;
    }
    if ((xe >= GC9307_LCD_W) || (ye >= GC9307_LCD_H)) {
        return 0;
    }
    return 1;
}

static void gc9307_write_init_cmd(const gc9307_init_cmd_t *init_cmd)
{
    u8 i;

    WriteCOM(init_cmd->cmd);
    for (i = 0; i < init_cmd->len; i++) {
        WriteDAT_8(init_cmd->data[i]);
    }
    if (init_cmd->delay_ms != 0) {
        lcd_delay(init_cmd->delay_ms);
    }
}

static void gc9307_write_init_sequence(void)
{
    u32 i;

    for (i = 0; i < ARRAY_SIZE(gc9307_init_cmds); i++) {
        gc9307_write_init_cmd(&gc9307_init_cmds[i]);
    }
}

static void gc9307_fill_clear_buf(u32 color)
{
    u8 hi = (u8)(color >> 8);
    u8 lo = (u8)color;
    u32 i;

    for (i = 0; i < (sizeof(gc9307_clear_buf) / 2); i++) {
        gc9307_clear_buf[(i * 2)] = hi;
        gc9307_clear_buf[(i * 2) + 1] = lo;
    }
}

#ifdef D_GC9307_TE_SYNC_ENABLE
static void gc9307_te_irq_cb(void)
{
    u32 now_ms = timer_get_ms();

    if (gc9307_te_last_irq_ms != 0) {
        gc9307_te_irq_period_ms = now_ms - gc9307_te_last_irq_ms;
    }
    gc9307_te_last_irq_ms = now_ms;
    gc9307_te_irq_count++;
    if (gc9307_te_sem_ready && gc9307_te_waiter_armed) {
        gc9307_te_sem_post_count++;
        os_sem_post(&gc9307_te_sem);
    }
}

static void gc9307_te_prepare_pin(u32 pin)
{
    gpio_set_direction(pin, 1);
    gpio_set_die(pin, 1);
}

static int gc9307_te_capture_edge(u32 start_irq, u32 request_ms)
{
    u32 edge_irq;
    u32 edge_ms;

    local_irq_disable();
    edge_irq = gc9307_te_irq_count;
    edge_ms = gc9307_te_last_irq_ms;
    local_irq_enable();
    if (edge_irq == start_irq) {
        return 0;
    }

    gc9307_te_wait_ok_count++;
    gc9307_te_last_edge_ms = edge_ms;
    gc9307_te_last_edge_irq_count = edge_irq;
    gc9307_te_last_request_to_edge_ms = edge_ms - request_ms;
    return 1;
}
#endif

void d_lcd_gc9307_te_init(const struct lcd_platform_data *pdata)
{
#ifdef D_GC9307_TE_SYNC_ENABLE
    if ((pdata == NULL) || (pdata->te_pin == (u32)-1)) {
        printf("[GC9307_LCD] TE disabled: invalid pin\n");
        return;
    }

    gc9307_te_pin = pdata->te_pin;
    gc9307_te_prepare_pin(gc9307_te_pin);
    if (!gc9307_te_sem_ready) {
        if (os_sem_create(&gc9307_te_sem, 0) != OS_NO_ERR) {
            printf("[GC9307_LCD] TE semaphore create failed pin=%u\n",
                   gc9307_te_pin);
            return;
        }
        gc9307_te_sem_ready = 1;
    }
    if (gc9307_te_wakeup_hdl == NULL) {
        gc9307_te_wakeup_hdl = port_wakeup_reg(EVENT_IO_1, gc9307_te_pin,
                                               GC9307_TE_WAKE_EDGE,
                                               gc9307_te_irq_cb);
    }

    gc9307_te_registered = (gc9307_te_wakeup_hdl != NULL);
    if (gc9307_te_registered) {
        printf("[GC9307_LCD] TE registered pin=%u edge=%s scanline=%u wait_ticks=%u\n",
               gc9307_te_pin, GC9307_TE_EDGE_NAME,
               D_GC9307_TE_SCANLINE, GC9307_TE_WAIT_TICKS);
    } else {
        printf("[GC9307_LCD] TE register failed pin=%u frame_wait=1\n",
               gc9307_te_pin);
    }
#else
    (void)pdata;
#endif
}

d_lcd_gc9307_te_wait_result_e d_lcd_gc9307_wait_te_frame(void)
{
#ifdef D_GC9307_TE_SYNC_ENABLE
    u32 start_irq;
    u32 request_ms;
    int wait_result;

    if (!gc9307_te_registered || !gc9307_te_sem_ready) {
        return D_LCD_GC9307_TE_WAIT_BYPASSED;
    }

    gc9307_te_request_count++;
    os_sem_set(&gc9307_te_sem, 0);
    local_irq_disable();
    start_irq = gc9307_te_irq_count;
    request_ms = timer_get_ms();
    gc9307_te_last_request_ms = request_ms;
    gc9307_te_last_request_irq_count = start_irq;
    gc9307_te_waiter_armed = 1;
    local_irq_enable();
    if (gc9307_te_capture_edge(start_irq, request_ms)) {
        gc9307_te_waiter_armed = 0;
        os_sem_set(&gc9307_te_sem, 0);
        return D_LCD_GC9307_TE_WAIT_EDGE;
    }

    wait_result = os_sem_pend(&gc9307_te_sem, GC9307_TE_WAIT_TICKS);

    gc9307_te_waiter_armed = 0;
    os_sem_set(&gc9307_te_sem, 0);
    if (gc9307_te_capture_edge(start_irq, request_ms)) {
        return D_LCD_GC9307_TE_WAIT_EDGE;
    }
    if (wait_result == OS_NO_ERR) {
        gc9307_te_stale_wakeup_count++;
    }

    gc9307_te_timeout_count++;
    if ((gc9307_te_timeout_count <= 5) ||
        ((gc9307_te_timeout_count % GC9307_TE_TIMEOUT_LOG_STEP) == 0)) {
        printf("[GC9307_LCD] TE wait timeout pin=%u count=%u irq=%u stale=%u\n",
               gc9307_te_pin, gc9307_te_timeout_count,
               gc9307_te_irq_count, gc9307_te_stale_wakeup_count);
    }
    return D_LCD_GC9307_TE_WAIT_TIMEOUT;
#else
    return D_LCD_GC9307_TE_WAIT_BYPASSED;
#endif
}

int d_lcd_gc9307_prepare_area(uint16_t xs, uint16_t xe,
                              uint16_t ys, uint16_t ye)
{
    if (!gc9307_area_valid(xs, xe, ys, ye) ||
        !d_lcd_spi_async_is_ready()) {
        return -1;
    }

    gc9307_set_draw_area(xs, xe, ys, ye);
    return 0;
}

int d_lcd_gc9307_prepare_write(void)
{
    if (!d_lcd_spi_async_is_ready()) {
        return -1;
    }
    WriteCOM(0x2c);
    return 0;
}

int d_lcd_gc9307_flush_pixels_start(uint8_t *data, uint32_t len)
{
    u32 callback_count;
    int result;

    if ((data == NULL) || (len == 0) || !d_lcd_spi_async_is_ready() ||
        gc9307_transfer_active) {
        return -1;
    }

    d_lcd_spi_async_get_counts(NULL, &callback_count);
    lcd_cs_pinstate(0);
    lcd_rs_pinstate(1);
    result = lcd_send_map(data, len);
    if (result < 0) {
        lcd_cs_pinstate(1);
        return -1;
    }
    gc9307_transfer_callback_base = callback_count;
    gc9307_transfer_submit_count++;
    gc9307_transfer_active = 1;
    return 0;
}

int d_lcd_gc9307_flush_start(const d_lcd_gc9307_flush_t *flush)
{
    if ((flush == NULL) || (flush->data == NULL) || (flush->len == 0)) {
        return -1;
    }
    if (d_lcd_gc9307_prepare_area(flush->xs, flush->xe,
                                  flush->ys, flush->ye) != 0) {
        return -1;
    }
    if (d_lcd_gc9307_prepare_write() != 0) {
        return -1;
    }
    return d_lcd_gc9307_flush_pixels_start(flush->data, flush->len);
}

int d_lcd_gc9307_flush_wait(uint32_t timeout_ticks)
{
    u32 callback_count;
    int result;

    if (!gc9307_transfer_active) {
        lcd_cs_pinstate(1);
        return -1;
    }

    result = d_lcd_spi_async_wait(timeout_ticks);
    d_lcd_spi_async_get_counts(NULL, &callback_count);

    lcd_cs_pinstate(1);
    if ((result == 0) &&
        (callback_count != gc9307_transfer_callback_base)) {
        gc9307_transfer_callback_count++;
        gc9307_transfer_complete_count++;
    } else {
        if (result == 0) {
            printf("[GC9307-SPI] flush callback missing submit=%u\n",
                   gc9307_transfer_submit_count);
            result = -1;
        }
        gc9307_transfer_failure_count++;
    }
    gc9307_transfer_active = 0;
    return result;
}

void d_lcd_gc9307_get_transfer_stats(
    d_lcd_gc9307_transfer_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    stats->submit_count = gc9307_transfer_submit_count;
    stats->callback_count = gc9307_transfer_callback_count;
    stats->complete_count = gc9307_transfer_complete_count;
    stats->failure_count = gc9307_transfer_failure_count;
    stats->last_submit_ms = d_lcd_spi_async_get_last_submit_ms();
}

void d_lcd_gc9307_get_te_stats(d_lcd_gc9307_te_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

#ifdef D_GC9307_TE_SYNC_ENABLE
    stats->irq_count = gc9307_te_irq_count;
    stats->wait_ok_count = gc9307_te_wait_ok_count;
    stats->timeout_count = gc9307_te_timeout_count;
    stats->sem_post_count = gc9307_te_sem_post_count;
    stats->stale_wakeup_count = gc9307_te_stale_wakeup_count;
    stats->request_count = gc9307_te_request_count;
    stats->last_irq_ms = gc9307_te_last_irq_ms;
    stats->irq_period_ms = gc9307_te_irq_period_ms;
    stats->last_request_ms = gc9307_te_last_request_ms;
    stats->last_request_irq_count = gc9307_te_last_request_irq_count;
    stats->last_edge_ms = gc9307_te_last_edge_ms;
    stats->last_edge_irq_count = gc9307_te_last_edge_irq_count;
    stats->last_request_to_edge_ms = gc9307_te_last_request_to_edge_ms;
    stats->is_registered = gc9307_te_registered;
#else
    memset(stats, 0, sizeof(*stats));
#endif
}

static void gc9307_clear_screen(u32 color)
{
    u16 y;

    gc9307_fill_clear_buf(color);
    lcd_interface_non_block_wait();
    gc9307_set_draw_area(0, GC9307_LCD_W - 1, 0, GC9307_LCD_H - 1);
    WriteCOM(0x2c);
    for (y = 0; y < GC9307_LCD_H; y += GC9307_CLEAR_ROWS) {
        WriteDAT_DMA(gc9307_clear_buf, sizeof(gc9307_clear_buf));
    }
    lcd_interface_non_block_wait();
}

static void gc9307_draw(u8 *map, u32 size)
{
    if ((map == NULL) || (size == 0)) {
        return;
    }

    lcd_interface_non_block_wait();
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
    lcd_interface_non_block_wait();
}

static void gc9307_lvgl_full(u16 xs, u16 xe, u16 ys, u16 ye, char *img)
{
    u32 len;

    if ((img == NULL) || !gc9307_area_valid(xs, xe, ys, ye)) {
        printf("[GC9307_LCD] invalid lvgl area img=%x area=%u,%u-%u,%u\n",
               (u32)img, xs, ys, xe, ye);
        return;
    }

    len = (u32)(xe - xs + 1) * (ye - ys + 1) * 2;
    lcd_interface_non_block_wait();
    gc9307_set_draw_area(xs, xe, ys, ye);
    WriteCOM(0x2c);
    WriteDAT_DMA((u8 *)img, len);
    lcd_interface_non_block_wait();
}

static int gc9307_init(void)
{
    printf("[GC9307_LCD] SDK LCD init start\n");
    gc9307_reset();
    gc9307_write_init_sequence();
    gc9307_set_draw_area(0, GC9307_LCD_W - 1, 0, GC9307_LCD_H - 1);
    gc9307_clear_screen(BLACK);
    printf("[GC9307_LCD] SDK LCD init done %ux%u\n", GC9307_LCD_W, GC9307_LCD_H);
    return 0;
}

REGISTER_LCD_DEV(d_lcd_gc9307) = {
    .name              = "gc9307",
    .lcd_width         = GC9307_LCD_W,
    .lcd_height        = GC9307_LCD_H,
    .color_format      = LCD_COLOR_RGB565,
    .column_addr_align = 1,
    .row_addr_align    = 1,
    .LCD_Init          = gc9307_init,
    .SetDrawArea       = gc9307_set_draw_area,
    .LCD_Draw          = gc9307_draw,
    .LCD_Lvgl_Full     = gc9307_lvgl_full,
    .LCD_ClearScreen   = gc9307_clear_screen,
    .Reset             = gc9307_reset,
};

#endif
