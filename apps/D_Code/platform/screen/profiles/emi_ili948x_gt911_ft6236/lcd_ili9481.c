#include "config/app_config.h"
#include "system/includes.h"
#include "platform/screen/lcd/d_lcd_compat.h"

#define ILI9481_LCD_WIDTH     320U
#define ILI9481_LCD_HEIGHT    480U

#define REGFLAG_DELAY 0x45

// ILI9481 初始化序列
typedef struct {
    u8 cmd;
    u8 cnt;
    u8 dat[128];
} InitCode;

static const InitCode ili9481_init_seq[] = {
    {0x01, 0},
    {REGFLAG_DELAY, 200},
    {0x13, 1, {0x00}},
    {0x35, 1, {0x00}}, // 开 TE
    {0x44, 2, {0x01, 0X50}},
    {0xc5, 1, {0x04}},
    {0xc5, 1, {0x07}},
    {0xE4, 1, {0xA0}},
    {0xd0, 3, {0x05, 0x40, 0x08}},
    {0xd1, 3, {0x00, 0x00, 0x10}},
    {0xd2, 2, {0x01, 0x00}},
    {0xc0, 5, {0x00, 0x3b, 0x00, 0x02, 0x11}},
    {0xC8, 12, {0X00, 0x26, 0x21, 0x00, 0x00, 0x1f, 0x65, 0x23, 0x77, 0x00, 0x0f, 0x00}},
    {0x2A, 4, {0X00, 0x00, 0x01, 0x3F}},
    {0x2B, 4, {0X00, 0x00, 0x01, 0xDF}},
    {0x3A, 1, {0x55}}, // RGB565
    {0x11, 0}, // Sleep out
    {REGFLAG_DELAY, 200},
    {0x29, 0}, // Display on
};

// 设置绘图区域
static void ili9481_set_draw_area(u16 xs, u16 xe, u16 ys, u16 ye)
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

// 设置扫描方向
static void ili9481_set_direction(u8 dir)
{
    WriteCOM(0x36);
    
    if (dir == ROTATE_DEGREE_0) {
        WriteDAT_8(0x4c);
    } else if (dir == ROTATE_DEGREE_180) {
        WriteDAT_8(0x04);
    }
    
    ili9481_set_draw_area(0, ILI9481_LCD_WIDTH - 1, 0,
                          ILI9481_LCD_HEIGHT - 1);
}

// 清屏函数
static void ili9481_clear_screen(u32 color)
{
    // 设置全屏区域
    ili9481_set_draw_area(0, ILI9481_LCD_WIDTH - 1, 0,
                          ILI9481_LCD_HEIGHT - 1);
    WriteCOM(0x2c); // Memory write
    d_lcd_write_solid_pixels((u16)color,
                             ILI9481_LCD_WIDTH * ILI9481_LCD_HEIGHT);
}

// 填充矩形区域
static void ili9481_fill_rect(u16 xs, u16 xe, u16 ys, u16 ye, u8 *img)
{
    u32 len = (xe + 1 - xs) * (ye + 1 - ys) * 2;
    
    ili9481_set_draw_area(xs, xe, ys, ye);
    WriteCOM(0x2c);
    WriteDAT_DMA(img, len);
}

// 复位函数
static void ili9481_reset(void)
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

// 执行初始化序列
static void ili9481_init_code(const InitCode *code, u8 cnt)
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
static void ili9481_fill(u8 *map, u32 size);

// 初始化函数
static int ili9481_init(void)
{
    lcd_d("ILI9481 init start\n");

    // 复位
    ili9481_reset();

    // 执行初始化序列
    ili9481_init_code(ili9481_init_seq, sizeof(ili9481_init_seq) / sizeof(ili9481_init_seq[0]));

    // 设置屏幕方向
    ili9481_set_direction(ROTATE_DEGREE_0);

    // 注册 TE 推屏回调（摄像头显示需要）
    extern void init_TE(void (*cb)(u8 *buf, u32 buf_size));
    init_TE(ili9481_fill);
    lcd_d("ILI9481 TE callback registered\n");

    lcd_d("ILI9481 init success\n");
    return 0;
}

// 推送数据到屏幕（TE 同步）
static void ili9481_fill(u8 *map, u32 size)
{
    extern void lcd_interface_non_block_wait(void);
    lcd_interface_non_block_wait();
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

// 推送数据到屏幕（用于 LVGL）
static void ili9481_draw(u8 *map, u32 size)
{
    ili9481_set_draw_area(0, ILI9481_LCD_WIDTH - 1, 0,
                          ILI9481_LCD_HEIGHT - 1);
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

// 摄像头数据推屏（用于图像合成引擎，无需等待）
static void ili9481_draw_to_dev(u8 *map, u32 size)
{
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

// LCD 驱动导出（显式注册）
const struct lcd_device d_lcd_ili9481 = {
    .name              = "ili9481",
    .lcd_width         = ILI9481_LCD_WIDTH,
    .lcd_height        = ILI9481_LCD_HEIGHT,
    .color_format      = LCD_COLOR_RGB565,
    .LCD_Init          = ili9481_init,
    .SetDrawArea       = ili9481_set_draw_area,
    .LCD_Draw          = ili9481_draw,
    .LCD_DrawToDev     = ili9481_draw_to_dev,
    .LCD_ClearScreen   = ili9481_clear_screen,
    .Reset             = ili9481_reset,
};
