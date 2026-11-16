/**
 * @file d_lcd_compat.h
 * @brief 适配板级 LCD 设备、传输及兼容接口；业务和 LVGL 页面策略留在上层。
 */

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
    char *name; /**< 借用的静态驱动名称。 */
    uint16_t lcd_width; /**< 驱动声明的面板宽度，不是运行方向下的逻辑宽度。 */
    uint16_t lcd_height; /**< 驱动声明的面板高度。 */
    uint8_t color_format; /**< LCD_COLOR_* 像素格式标识。 */
    uint8_t column_addr_align; /**< 驱动声明的列地址对齐粒度，本兼容层不自动裁剪。 */
    uint8_t row_addr_align; /**< 驱动声明的行地址对齐粒度。 */
    int (*LCD_Init)(void); /**< 驱动初始化入口，0 表示未报告失败；逐命令硬件确认由驱动合同解释。 */
    void (*SetDrawArea)(uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye); /**< 设置物理矩形，调用方负责有效边界。 */
    void (*LCD_Draw)(uint8_t *map, uint32_t size); /**< 提交借用像素及字节数，完成及复用服从对应驱动。 */
    void (*LCD_DrawToDev)(uint8_t *map, uint32_t size); /**< 供 SDK 图像合成链路提交借用像素。 */
    /** 物理矩形及其借用 RGB565 数据；可选兼容入口，void 不证明传输成功。 */
    void (*LCD_Lvgl_Full)(uint16_t xs, uint16_t xe, uint16_t ys,
                          uint16_t ye, char *img);
    void (*LCD_ClearScreen)(uint32_t color); /**< 按驱动解释颜色并执行清屏，不提供独立错误票据。 */
    void (*Reset)(void); /**< 执行面板复位，不代表显示任务/资源已销毁。 */
};

#define REGISTER_LCD_DEV(lcd_dev) const struct lcd_device lcd_dev

// 全局变量
extern uint8_t lcd_bk_on;
extern uint8_t lcd_bk_off;
extern uint16_t lcd_w;
extern uint16_t lcd_h;

/**
 * @brief 按绑定平台描述符设置面板复位引脚电平。
 * @param[in] state 要输出的原始电平。
 * @note 调用前须绑定有效平台描述符；未配置的引脚不操作，不转换复位有效电平。
 */
void lcd_rst_pinstate(uint8_t state);
/**
 * @brief 设置面板片选引脚原始电平。
 * @param[in] state 要输出的原始电平。
 * @note 调用前须绑定有效平台描述符；单次传输的片选时序由调用方组织。
 */
void lcd_cs_pinstate(uint8_t state);
/**
 * @brief 设置面板命令/数据选择引脚电平。
 * @param[in] state 原始电平，驱动按 0 命令、1 数据使用。
 * @note 调用前须绑定有效平台描述符；无有效引脚时不操作。
 */
void lcd_rs_pinstate(uint8_t state);
/**
 * @brief 设置板级背光引脚的原始电平。
 * @param[in] state 原始电平，不自动换算亮度或有效极性。
 * @note 平台描述符或引脚缺失时不操作；产品亮度应通过 d_display 接口处理。
 */
void lcd_bl_pinstate(uint8_t state);
/**
 * @brief 向已打开的 LCD 接口提交一个字节。
 * @param[in] data 待提交字节。
 * @return 已打开时沿用 dev_write 返回值；没有设备句柄时为 -1。
 * @note 不单独返回 DMA 完成票据；由面板命令组合调用既有等待路径。
 */
int lcd_send_byte(uint8_t data);
/**
 * @brief 向 LCD 接口提交一段像素或命令数据。
 * @param[in] map 调用方持有的有效字节缓冲。
 * @param[in] len 待提交字节数，由调用方保证与缓冲匹配。
 * @return 沿用设备写入结果；没有设备句柄时为 -1。
 * @note GC9307 SPI 异步可用时转交异步提交；其他路径直接 dev_write。受理与硬件完成不同，源缓冲须保持到所属驱动确认。
 */
int lcd_send_map(uint8_t *map, uint32_t len);
/**
 * @brief 组合片选及命令电平提交一个命令字节，并调用既有 SPI 等待。
 * @param[in] cmd 面板命令字节。
 * @note 单显示所有者串行调用；void 返回不暴露底层写入或等待失败。
 */
void WriteCOM(uint8_t cmd);
/**
 * @brief 组合片选及数据电平提交一个数据字节，并调用既有 SPI 等待。
 * @param[in] dat 待提交的数据字节。
 * @note void 返回不提供独立硬件完成确认。
 */
void WriteDAT_8(uint8_t dat);
/**
 * @brief 组合片选及数据电平提交数据块，并沿原路径等待。
 * @param[in] dat 调用方持有的字节缓冲。
 * @param[in] len 有效字节数。
 * @note 不从此接口返回异步票据或错误；调用方须遵守显示所有者的缓冲借用合同，不能仅凭 void 返回判断传输成功。
 */
void WriteDAT_DMA(uint8_t *dat, uint32_t len);
/**
 * @brief 按当前总线调用已有非阻塞写入的等待/刷新入口。
 * @note 可能等待；GC9307 异步路径有既定超时及降级。void 返回隐藏底层结果，不证明超时后 DMA 已停止。
 */
void lcd_interface_non_block_wait(void);

#ifdef D_LCD_EMI_ILI948X_ENABLE
/**
 * @brief 在 EMI 配置中用静态小块缓冲重复提交纯色 RGB565 像素。
 * @param[in] color RGB565 颜色值。
 * @param[in] pixel_count 需要提交的像素数，零时不操作。
 * @note 只由显示所有者串行调用；每块沿原等待路径处理，发送负返回时停止后续块，但 void 不上报完成。
 */
void d_lcd_write_solid_pixels(uint16_t color, uint32_t pixel_count);
#endif

#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
/**
 * @brief 查询 GC9307 SPI 异步适配是否已准备且未降级。
 * @return 可用为非零，否则为零；不是总线空闲或资源释放证明。
 */
int d_lcd_spi_async_is_ready(void);
/**
 * @brief 等待本适配的在途 SPI 完成通知。
 * @param[in] timeout_ticks 传给 SDK 信号量的等待 tick 数。
 * @return 0 表示适配已收到完成或没有在途标志；不可用或等待失败为 -1。
 * @note 等待失败至多尝试一次底层 flush，然后清本地在途标志并降级；该清标志不证明硬件已经停止读取源缓冲。
 */
int d_lcd_spi_async_wait(uint32_t timeout_ticks);
/**
 * @brief 读取累计异步提交和回调计数，供显示所有者诊断。
 * @param[out] submit_count 可为 NULL，接收非负提交次数。
 * @param[out] callback_count 可为 NULL，接收匹配在途标志的回调次数。
 * @note 分别读取 volatile 值，不承诺两个字段为同一时刻的原子快照，也不能据计数单独推断 SDK 所有资源已释放。
 */
void d_lcd_spi_async_get_counts(uint32_t *submit_count,
                                uint32_t *callback_count);
/**
 * @brief 读取本适配最近一次异步提交前记录的时钟。
 * @return timer_get_ms 毫秒计数，未提交时为零；允许自然回绕。
 */
uint32_t d_lcd_spi_async_get_last_submit_ms(void);
#endif

// 延时函数
void msleep(unsigned int ms);
#define lcd_delay(x) msleep(x)

/**
 * @brief 按板型借用持续有效的 LCD 平台描述符。
 * @return 板级静态描述符，不转移所有权；实现归 board/wl82。
 */
const struct lcd_platform_data *d_get_lcd_pdata(void);
/**
 * @brief 绑定平台参数、选择面板驱动并初始化总线及面板。
 * @param[in] pdata 持续有效的板级描述符；本层保存借用指针。
 * @return 0 为驱动初始化成功；参数、驱动选择、总线或面板初始化失败为 -1。
 * @note 启动阶段单显示所有者串行调用，不提供运行期重置或完整失败回滚。面板尺寸在驱动初始化前登记，不能只凭尺寸非零判断成功。
 */
int d_lcd_init(const struct lcd_platform_data *pdata);
/**
 * @brief 调用已选面板驱动的可选清屏入口。
 * @param[in] color RGB565 颜色值。
 * @note 没有驱动或清屏能力时不操作；void 不提供错误确认。
 */
void d_lcd_clear_screen(uint16_t color);
/**
 * @brief 读取已选驱动登记的宽度。
 * @return 未选择驱动时为 0；非零不单独证明硬件已初始化。
 */
uint16_t d_lcd_get_width(void);
/**
 * @brief 读取已选驱动登记的高度。
 * @return 未选择驱动时为 0；不按产品运行方向换算逻辑尺寸。
 */
uint16_t d_lcd_get_height(void);
/**
 * @brief 调用共用清屏入口填充纯色。
 * @param[in] color RGB565 颜色值。
 */
void d_lcd_fill_color(uint16_t color);
/**
 * @brief 保留 LVGL 所需的兼容初始化入口，当前不重复初始化面板。
 * @note 真实面板初始化已由启动层通过 d_lcd_init 完成。
 */
void user_lcd_init(void);
/**
 * @brief 将一块 UI 像素转交所选面板或 EMI 图像合成链路。
 * @param[in] xs 物理起始横坐标。
 * @param[in] xe 物理结束横坐标。
 * @param[in] ys 物理起始纵坐标。
 * @param[in] ye 物理结束纵坐标。
 * @param[in] img 调用方保持的像素数据，遵循驱动/合成器借用合同。
 * @note GC9307 直接调用其驱动；EMI 按摄像头合成状态决定提交或忽略。void 返回不证明实际传屏成功，不在此复用未确认的缓冲。
 */
void lcd_lvgl_full(uint16_t xs, uint16_t xe, uint16_t ys,
                   uint16_t ye, uint8_t *img);

// 显式导出的 LCD 驱动
#ifdef D_LCD_EMI_ILI948X_ENABLE
extern const struct lcd_device d_lcd_ili9481;
extern const struct lcd_device d_lcd_ili9488;
#endif
#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
extern const struct lcd_device d_lcd_gc9307;
/**
 * @brief 按板级参数初始化 GC9307 TE 采样和中断能力。
 * @param[in] pdata 板级平台描述符；具体可用性由 GC9307 驱动记录。
 * @note 由显示启动链调用，不改面板 TE 时序或业务帧策略；后续等待结果须查询驱动。
 */
void d_lcd_gc9307_te_init(const struct lcd_platform_data *pdata);
#endif

#endif
