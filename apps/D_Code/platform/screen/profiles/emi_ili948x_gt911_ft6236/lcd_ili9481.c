/**
 * @file lcd_ili9481.c
 * @brief 适配 ILI9481 的既有 EMI 命令及回调，不承担逻辑屏幕翻转策略。
 */

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
    {0x11, 0}, // 退出休眠
    {REGFLAG_DELAY, 200},
    {0x29, 0}, // 开启显示
};

/**
 * @brief 按面板物理坐标提交绘制窗口。
 * @param[in] xs 起始列。
 * @param[in] xe 结束列。
 * @param[in] ys 起始行。
 * @param[in] ye 结束行。
 * @note 内部调用方保证矩形有序及范围正确；接口不单独返回命令写错误。
 */
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

/**
 * @brief 按既有 0°/180° 命令设置面板扫描方向并恢复整屏窗口。
 * @param[in] dir 内部使用的 ROTATE_DEGREE_0 或 ROTATE_DEGREE_180，不是产品运行方向角。
 * @note 未识别的值不会发送方向数据；不用于 GC9307 的运行方向功能，不更改初始化常量。
 */
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

/**
 * @brief 设置整屏窗口并用共用静态颜色块提交清屏像素。
 * @param[in] color 低 16 位为 RGB565 颜色。
 * @note 兼容 void 入口不提供独立完成或错误票据。
 */
static void ili9481_clear_screen(u32 color)
{
    // 设置全屏区域
    ili9481_set_draw_area(0, ILI9481_LCD_WIDTH - 1, 0,
                          ILI9481_LCD_HEIGHT - 1);
    WriteCOM(0x2c); // 写像素存储
    d_lcd_write_solid_pixels((u16)color,
                             ILI9481_LCD_WIDTH * ILI9481_LCD_HEIGHT);
}

/**
 * @brief 按已给物理矩形计算 RGB565 字节数并提交数据。
 * @param[in] xs 起始列。
 * @param[in] xe 结束列。
 * @param[in] ys 起始行。
 * @param[in] ye 结束行。
 * @param[in] img 足量的有效像素缓冲。
 * @note 内部调用方保证矩形有序、边界及缓冲容量；本静态函数不自行校验。
 */
static void ili9481_fill_rect(u16 xs, u16 xe, u16 ys, u16 ye, u8 *img)
{
    u32 len = (xe + 1 - xs) * (ye + 1 - ys) * 2;
    
    ili9481_set_draw_area(xs, xe, ys, ye);
    WriteCOM(0x2c);
    WriteDAT_DMA(img, len);
}

/**
 * @brief 沿原电平与延时序列复位面板并预置命令/片选引脚。
 */
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

/**
 * @brief 顺序执行原初始化表，特殊延时项仅按既有时长休眠。
 * @param[in] code 至少 cnt 项的有效静态初始化表。
 * @param[in] cnt 要执行的表项数量。
 * @note 不检查表内参数长度；内部固定表负责满足数据容量，不修改任何寄存器值。
 */
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

/**
 * @brief 复位并执行初始化表，注册既有 TE 推屏回调。
 * @return 当前固定为 0，表示初始化调用序列结束，兼容 void 命令没有逐项硬件确认。
 */
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

/**
 * @brief 在 TE 推屏回调中等待前次接口操作，再提交当前像素块。
 * @param[in] map SDK 回调提供的有效像素缓冲。
 * @param[in] size 有效字节数。
 * @note 通过既有非阻塞 EMI 链路工作，不接管像素缓冲所有权。
 */
static void ili9481_fill(u8 *map, u32 size)
{
    extern void lcd_interface_non_block_wait(void);
    lcd_interface_non_block_wait();
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

/**
 * @brief 恢复整屏窗口后提交像素块。
 * @param[in] map 有效 RGB565 字节缓冲。
 * @param[in] size 待提交字节数。
 * @note 这是已有面板驱动行为；本函数不新增区域校验或独立 DMA 完成接口。
 */
static void ili9481_draw(u8 *map, u32 size)
{
    ili9481_set_draw_area(0, ILI9481_LCD_WIDTH - 1, 0,
                          ILI9481_LCD_HEIGHT - 1);
    WriteCOM(0x2c);
    WriteDAT_DMA(map, size);
}

/**
 * @brief 供原图像合成链路向当前窗口提交像素块。
 * @param[in] map SDK 合成链路持有的像素缓冲。
 * @param[in] size 有效字节数。
 * @note 此入口不额外等待 EMI 完成，不因 void 返回而转移或释放源缓冲。
 */
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
