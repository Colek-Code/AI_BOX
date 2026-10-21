#ifndef D_LCD_COMPAT_H
#define D_LCD_COMPAT_H

#include <stdint.h>

#include "framework/compat/d_stdio_compat.h"

// LCD 调试开关
#define LCD_DEBUG_ENABLE    0

#if (LCD_DEBUG_ENABLE == 0)
#define lcd_d(...)
#define lcd_e(fmt, ...)	printf("[LCD ERROR]: "fmt, ##__VA_ARGS__)
#else
#define lcd_d(fmt, ...)	printf("[LCD DEBUG]: "fmt, ##__VA_ARGS__)
#define lcd_e(fmt, ...)	printf("[LCD ERROR]: "fmt, ##__VA_ARGS__)
#endif

// 旋转角度宏
#define ROTATE_DEGREE_0     0
#define ROTATE_DEGREE_90    1
#define ROTATE_DEGREE_180   2
#define ROTATE_DEGREE_270   3

// RGB565 颜色定义
#define BLACK     0x0000
#define BLUE      0x001F
#define RED       0xF800
#define GREEN     0x07E0
#define CYAN      0x07FF
#define MAGENTA   0xF81F
#define YELLOW    0xFFE0
#define WHITE     0xFFFF

// LCD 接口类型
enum LCD_INTERFACE {
    LCD_SPI,
    LCD_EMI,
    LCD_PAP,
    LCD_IMD,
};

// LCD 颜色格式
enum LCD_COLOR {
    LCD_COLOR_RGB565,
    LCD_COLOR_RGB666,
    LCD_COLOR_RGB888,
    LCD_COLOR_MONO,
};

// LCD 背光控制
enum LCD_BL {
    BL_OFF,
    BL_ON,
};

// LCD 板级配置数据
struct lcd_platform_data {
    uint32_t rst_pin;           // 复位引脚
    uint32_t cs_pin;            // 片选引脚
    uint32_t rs_pin;            // 数据/命令选择引脚
    uint32_t bl_pin;            // 背光引脚
    uint32_t te_pin;            // TE 信号引脚
    uint32_t check_pin_D6;      // 屏幕识别引脚 D6
    uint32_t check_pin_D7;      // 屏幕识别引脚 D7
    uint32_t touch_int_pin;     // 触摸中断引脚
    uint32_t touch_reset_pin;   // 触摸复位引脚（部分方案与 LCD 复位共用）
    const char *spi_id;    // 硬件 SPI 设备名（LCD_SPI 使用）
    uint32_t spi_clk_pin;       // SPI 时钟引脚（日志/校验用）
    uint32_t spi_mosi_pin;      // SPI MOSI 引脚（日志/校验用）
    enum LCD_INTERFACE lcd_if;  // LCD 接口类型
};

// LCD 设备驱动结构
struct lcd_device {
    char *name;
    uint16_t lcd_width;
    uint16_t lcd_height;
    uint8_t color_format;
    uint8_t column_addr_align;
    uint8_t row_addr_align;
    int (*LCD_Init)(void);
    void (*SetDrawArea)(uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye);
    void (*LCD_Draw)(uint8_t *map, uint32_t size);
    void (*LCD_DrawToDev)(uint8_t *map, uint32_t size);  // 摄像头数据推屏
    void (*LCD_Lvgl_Full)(uint16_t xs, uint16_t xe, uint16_t ys,
                          uint16_t ye, char *img);
    void (*LCD_ClearScreen)(uint32_t color);
    void (*Reset)(void);
};

#define REGISTER_LCD_DEV(lcd_dev) const struct lcd_device lcd_dev

// 全局变量
extern uint8_t lcd_bk_on;
extern uint8_t lcd_bk_off;
extern uint16_t lcd_w;
extern uint16_t lcd_h;

// LCD 驱动函数声明
void lcd_rst_pinstate(uint8_t state);
void lcd_cs_pinstate(uint8_t state);
void lcd_rs_pinstate(uint8_t state);
void lcd_bl_pinstate(uint8_t state);
int lcd_send_byte(uint8_t data);
int lcd_send_map(uint8_t *map, uint32_t len);
void WriteCOM(uint8_t cmd);
void WriteDAT_8(uint8_t dat);
void WriteDAT_DMA(uint8_t *dat, uint32_t len);
void lcd_interface_non_block_wait(void);

#ifdef D_LCD_EMI_ILI948X_ENABLE
void d_lcd_write_solid_pixels(uint16_t color, uint32_t pixel_count);
#endif

#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
int d_lcd_spi_async_is_ready(void);
int d_lcd_spi_async_wait(uint32_t timeout_ticks);
void d_lcd_spi_async_get_counts(uint32_t *submit_count,
                                uint32_t *callback_count);
uint32_t d_lcd_spi_async_get_last_submit_ms(void);
#endif

// 延时函数
void msleep(unsigned int ms);
#define lcd_delay(x) msleep(x)

// LCD 初始化和控制函数
const struct lcd_platform_data *d_get_lcd_pdata(void);
int d_lcd_init(const struct lcd_platform_data *pdata);
void d_lcd_clear_screen(uint16_t color);
uint16_t d_lcd_get_width(void);
uint16_t d_lcd_get_height(void);
void d_lcd_fill_color(uint16_t color);
void user_lcd_init(void);
void lcd_lvgl_full(uint16_t xs, uint16_t xe, uint16_t ys,
                   uint16_t ye, uint8_t *img);

// 显式导出的 LCD 驱动
#ifdef D_LCD_EMI_ILI948X_ENABLE
extern const struct lcd_device d_lcd_ili9481;
extern const struct lcd_device d_lcd_ili9488;
#endif
#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
extern const struct lcd_device d_lcd_gc9307;
void d_lcd_gc9307_te_init(const struct lcd_platform_data *pdata);
#endif

#endif
