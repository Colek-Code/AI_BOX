#include "config/app_config.h"
#include "system/includes.h"
#include "platform/screen/lcd/d_lcd_compat.h"

#define ILI9488_LCD_WIDTH     320U
#define ILI9488_LCD_HEIGHT    480U

#define REGFLAG_DELAY 0x45

typedef struct {
    u8 cmd;
    u8 cnt;
    u8 dat[128];
} InitCode;

static const InitCode ili9488_init_seq[] = {
    {0x21, 1, {0x00}},
    {0x13, 1, {0x00}},
    {0x35, 1, {0x00}}, // 开启 TE
    {0xB1, 2, {0x90, 0x11}},
    {0x36, 1, {0xd8}},
    {0xC1, 1, {0x41}},
    {0XF7, 4, {0xA9, 0x51, 0x2C, 0x82}},
    {0xC1, 1, {0x41}},
    {0xC0, 2, {0x0f, 0x0f}},
    {0xC2, 1, {0x22}},
    {0XC5, 3, {0x00, 0x53, 0x80}},
    {0xB4, 1, {0x02}},
    {0xB7, 1, {0xc6}},
    {0xB6, 2, {0x02, 0x42}},
    {0xBE, 2, {0x00, 0x04}},
    {0xE9, 1, {0x00}},
    {0x3a, 1, {0x55}}, // RGB565
    {0x11, 0}, // Sleep out
    {REGFLAG_DELAY, 200},
    {0x29, 0}, // Display on
};

static void ili9488_set_draw_area(u16 xs, u16 xe, u16 ys, u16 ye)
{
    WriteCOM(0x2A);
    WriteDAT_8(xs >> 8);
    WriteDAT_8(xs);
    WriteDAT_8(xe >> 8);
    WriteDAT_8(xe);
    
    WriteCOM(0x2B);
    WriteDAT_8(ys >> 8);
    WriteDAT_8(ys);
    WriteDAT_8(ye >> 8);
    WriteDAT_8(ye);
}

static void ili9488_set_direction(u8 dir)
{
    WriteCOM(0x36);
    
    if (dir == ROTATE_DEGREE_0) {
        WriteDAT_8(0x00);
    } else if (dir == ROTATE_DEGREE_180) {
        WriteDAT_8(0x80);
    }
    
    ili9488_set_draw_area(0, ILI9488_LCD_WIDTH - 1, 0,
                          ILI9488_LCD_HEIGHT - 1);
}

static void ili9488_clear_screen(u32 color)
{
    ili9488_set_draw_area(0, ILI9488_LCD_WIDTH - 1, 0,
                          ILI9488_LCD_HEIGHT - 1);
    WriteCOM(0x2c);
    d_lcd_write_solid_pixels((u16)color,
                             ILI9488_LCD_WIDTH * ILI9488_LCD_HEIGHT);
}

static void ili9488_reset(void)
{
    lcd_rst_pinstate(1);
    lcd_rs_pinstate(1);
    lcd_cs_pinstate(1);
    
    lcd_rst_pinstate(1);
    lcd_delay(60);
    lcd_rst_pinstate(0);
    lcd_delay(10);
    lcd_rst_pinstate(1);
    lcd_delay(100);
}

static void ili9488_init_code(const InitCode *code, u8 cnt)
{
    u8 i, j;
    
    for (i = 0; i < cnt; i++) {
        if (code[i].cmd == REGFLAG_DELAY) {
            lcd_delay(code[i].cnt);
        } else {
            WriteCOM(code[i].cmd);
            for (j = 0; j < code[i].cnt; j++) {
                WriteDAT_8(code[i].dat[j]);
            }
        }
    }
}

// 前向声明
static void ili9488_fill(u8 *map, u32 size);

static int ili9488_init(void)
{
    lcd_d("ILI9488 init start\n");

    ili9488_reset();

    ili9488_init_code(ili9488_init_seq, sizeof(ili9488_init_seq) / sizeof(ili9488_init_seq[0]));

    // 注册 TE 推屏回调（摄像头显示需要）
    extern void init_TE(void (*cb)(u8 *buf, u32 buf_size));
    init_TE(ili9488_fill);
    lcd_d("ILI9488 TE callback registered\n");

    lcd_d("ILI9488 init success\n");
    return 0;
}

// 推送数据到屏幕（TE 同步）
static void ili9488_fill(u8 *map, u32 size)
{
    extern void lcd_interface_non_block_wait(void);
    lcd_interface_non_block_wait();
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

static void ili9488_draw(u8 *map, u32 size)
{
    /* 不再重置绘制区域，使用之前 SetDrawArea 设置的区域 */
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

// 摄像头数据推屏（用于图像合成引擎，无需等待）
static void ili9488_draw_to_dev(u8 *map, u32 size)
{
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

// LCD 驱动导出（显式注册）
const struct lcd_device d_lcd_ili9488 = {
    .name              = "ili9488",
    .lcd_width         = ILI9488_LCD_WIDTH,
    .lcd_height        = ILI9488_LCD_HEIGHT,
    .color_format      = LCD_COLOR_RGB565,
    .LCD_Init          = ili9488_init,
    .SetDrawArea       = ili9488_set_draw_area,
    .LCD_Draw          = ili9488_draw,
    .LCD_DrawToDev     = ili9488_draw_to_dev,
    .LCD_ClearScreen   = ili9488_clear_screen,
    .Reset             = ili9488_reset,
};
