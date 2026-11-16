/**
 * @file d_lcd_compat.c
 * @brief 适配板级 LCD 设备、传输及兼容接口；业务和 LVGL 页面策略留在上层。
 */

#include "config/app_config.h"
#include "system/includes.h"
#include "device/device.h"
#include "asm/emi.h"
#include "asm/spi.h"
#include "platform/screen/lcd/d_lcd_compat.h"

// LCD 驱动全局变量
static struct lcd_device *lcd_dev = NULL;
static const struct lcd_platform_data *lcd_pdata = NULL;
static void *lcd_hdl = NULL;

#ifdef D_LCD_EMI_ILI948X_ENABLE
#define D_LCD_SOLID_CHUNK_PIXELS 320U
static u8 s_lcd_solid_chunk[D_LCD_SOLID_CHUNK_PIXELS * 2U];
#endif

#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
#define LCD_SPI_ASYNC_COMMAND_TIMEOUT_TICKS 20

static OS_SEM s_spi_tx_done_sem;
static struct spi_cb s_spi_callback;
static volatile u32 s_spi_submit_count;
static volatile u32 s_spi_callback_count;
static volatile u32 s_spi_last_submit_ms;
static volatile u8 s_spi_transfer_pending;
static u8 s_spi_async_ready;
static u8 s_spi_async_degraded;
static u8 s_spi_recovery_used;

/**
 * @brief 在 SDK SPI 回调中结束本地在途标志并通知等待者。
 * @param[in] priv 注册上下文，当前未使用。
 * @param[in] data SDK 传入的数据指针地址，当前不读取或修改。
 * @param[in] len SDK 传入的长度地址，当前不读取或修改。
 * @return 当前始终为 0。
 * @note 仅在本地仍有在途标志时计回调并发信号；不在此释放上层像素缓冲或调用 LVGL。
 */
static int lcd_spi_complete_cb(void *priv, u8 **data, u32 *len)
{
    (void)priv;
    (void)data;
    (void)len;
    if (s_spi_transfer_pending) {
        s_spi_callback_count++;
        s_spi_transfer_pending = 0;
        os_sem_post(&s_spi_tx_done_sem);
    }
    return 0;
}

/**
 * @brief 创建信号量并注册 SPI 回调、非阻塞及信号量配置。
 * @return 全部配置成功为 0，否则为 -1；SDK 配置失败时标记降级。
 * @note 本层不完整回滚已执行的 SDK 设置，不能将失败解释为接口未持有任何资源。
 */
static int lcd_spi_async_setup(void)
{
    int cb_result;
    int sem_result;
    int mode_result;

    if (os_sem_create(&s_spi_tx_done_sem, 0) != OS_NO_ERR) {
        return -1;
    }
    s_spi_callback.cb_func = lcd_spi_complete_cb;
    s_spi_callback.cb_priv = NULL;
    cb_result = dev_ioctl(lcd_hdl, IOCTL_SPI_SET_CB,
                          (u32)&s_spi_callback);
    sem_result = dev_ioctl(lcd_hdl, IOCTL_SPI_SET_USE_SEM, 0);
    mode_result = dev_ioctl(lcd_hdl, IOCTL_SPI_NON_BLOCK, 1);
    if ((cb_result != 0) || (sem_result != 0) || (mode_result != 0)) {
        printf("[GC9307-SPI] async setup failed cb=%d sem=%d mode=%d\n",
               cb_result, sem_result, mode_result);
        s_spi_async_degraded = 1;
        return -1;
    }

    s_spi_async_ready = 1;
    printf("[GC9307-SPI] async ready callback=1 os_sem=1 nonblock=1\n");
    return 0;
}

/**
 * @brief 提交一段 SPI 字节并登记本地在途事实。
 * @param[in] data 有效源数据，须按上层确认合同保持。
 * @param[in] len 非零字节数。
 * @return 参数或句柄无效为 -1；其余沿用 dev_write 结果。
 * @note 异步不可用时直接转交原写入路径；提交负返回只清本地标志，不声明硬件全局空闲。
 */
static int lcd_spi_async_submit(u8 *data, u32 len)
{
    int result;

    if ((data == NULL) || (len == 0) || (lcd_hdl == NULL)) {
        return -1;
    }
    if (!s_spi_async_ready || s_spi_async_degraded) {
        return dev_write(lcd_hdl, data, len);
    }

    os_sem_set(&s_spi_tx_done_sem, 0);
    s_spi_transfer_pending = 1;
    s_spi_last_submit_ms = timer_get_ms();
    result = dev_write(lcd_hdl, data, len);
    if (result < 0) {
        s_spi_transfer_pending = 0;
    } else {
        s_spi_submit_count++;
    }
    return result;
}

/**
 * @brief 查询 GC9307 SPI 异步适配是否已准备且未降级。 完整合同见 d_lcd_compat.h。
 */
int d_lcd_spi_async_is_ready(void)
{
    return s_spi_async_ready && !s_spi_async_degraded;
}

/**
 * @brief 等待本适配的在途 SPI 完成通知。 完整合同见 d_lcd_compat.h。
 */
int d_lcd_spi_async_wait(u32 timeout_ticks)
{
    int result;

    if (!d_lcd_spi_async_is_ready()) {
        return -1;
    }
    if (!s_spi_transfer_pending) {
        return 0;
    }

    result = os_sem_pend(&s_spi_tx_done_sem, (int)timeout_ticks);
    if ((result == OS_NO_ERR) && !s_spi_transfer_pending) {
        return 0;
    }

    printf("[GC9307-SPI] DMA timeout submit=%u callback=%u pending=%u\n",
           s_spi_submit_count, s_spi_callback_count,
           s_spi_transfer_pending);
    if (!s_spi_recovery_used) {
        s_spi_recovery_used = 1;
        dev_ioctl(lcd_hdl, IOCTL_SPI_WRITE_NON_BLOCK_FLUSH, 0);
    }
    s_spi_transfer_pending = 0;
    s_spi_async_degraded = 1;
    return -1;
}

/**
 * @brief 读取累计异步提交和回调计数，供显示所有者诊断。 完整合同见 d_lcd_compat.h。
 */
void d_lcd_spi_async_get_counts(u32 *submit_count, u32 *callback_count)
{
    if (submit_count != NULL) {
        *submit_count = s_spi_submit_count;
    }
    if (callback_count != NULL) {
        *callback_count = s_spi_callback_count;
    }
}

/**
 * @brief 读取本适配最近一次异步提交前记录的时钟。 完整合同见 d_lcd_compat.h。
 */
u32 d_lcd_spi_async_get_last_submit_ms(void)
{
    return s_spi_last_submit_ms;
}
#endif

u8 lcd_bk_on = BL_ON;
u8 lcd_bk_off = BL_OFF;
/* 只有 d_lcd_init 选中配置驱动后才公布面板尺寸。 */
u16 lcd_w = 0;
u16 lcd_h = 0;

static const struct lcd_device *const d_lcd_drivers[] = {
#ifdef D_LCD_EMI_ILI948X_ENABLE
    &d_lcd_ili9481,
    &d_lcd_ili9488,
#endif
#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
    &d_lcd_gc9307,
#endif
};

/**
 * @brief 在编译期驱动列表中按名称查找面板描述符。
 * @param[in] name 非空且以 NUL 结尾的驱动名，由内部调用方保证。
 * @return 借用的静态驱动描述符；未找到为 NULL。
 */
static const struct lcd_device *d_find_lcd_driver(const char *name)
{
    for (u32 i = 0; i < (sizeof(d_lcd_drivers) / sizeof(d_lcd_drivers[0])); i++) {
        if (d_lcd_drivers[i] && d_lcd_drivers[i]->name && (strcmp(d_lcd_drivers[i]->name, name) == 0)) {
            return d_lcd_drivers[i];
        }
    }
    return NULL;
}

/**
 * @brief 按绑定平台描述符设置面板复位引脚电平。 完整合同见 d_lcd_compat.h。
 */
void lcd_rst_pinstate(u8 state)
{
    if (lcd_pdata->rst_pin != (u32)-1) {
        gpio_direction_output(lcd_pdata->rst_pin, state);
    }
}

/**
 * @brief 设置面板片选引脚原始电平。 完整合同见 d_lcd_compat.h。
 */
void lcd_cs_pinstate(u8 state)
{
    if (lcd_pdata->cs_pin != (u32)-1) {
        gpio_direction_output(lcd_pdata->cs_pin, state);
    }
}

/**
 * @brief 设置面板命令/数据选择引脚电平。 完整合同见 d_lcd_compat.h。
 */
void lcd_rs_pinstate(u8 state)
{
    if (lcd_pdata->rs_pin != (u32)-1) {
        gpio_direction_output(lcd_pdata->rs_pin, state);
    }
}

/**
 * @brief 设置板级背光引脚的原始电平。 完整合同见 d_lcd_compat.h。
 */
void lcd_bl_pinstate(u8 state)
{
    if ((lcd_pdata != NULL) && (lcd_pdata->bl_pin != (u32)-1)) {
        gpio_direction_output(lcd_pdata->bl_pin, state);
    }
}

/**
 * @brief 向已打开的 LCD 接口提交一个字节。 完整合同见 d_lcd_compat.h。
 */
int lcd_send_byte(u8 data)
{
    if (lcd_hdl) {
        return dev_write(lcd_hdl, &data, 1);
    }
    return -1;
}

/**
 * @brief 向 LCD 接口提交一段像素或命令数据。 完整合同见 d_lcd_compat.h。
 */
int lcd_send_map(u8 *map, u32 len)
{
    if (lcd_hdl) {
#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
        if ((lcd_pdata != NULL) && (lcd_pdata->lcd_if == LCD_SPI) &&
            d_lcd_spi_async_is_ready()) {
            return lcd_spi_async_submit(map, len);
        }
#endif
        return dev_write(lcd_hdl, map, len);
    }
    return -1;
}

/**
 * @brief 在已绑定 SPI 接口上调用 SDK 非阻塞写刷新等待。
 * @note 兼容命令组合使用；无有效句柄或非 SPI 时不操作，void 不暴露结果。
 */
static void lcd_spi_write_wait(void)
{
    if ((lcd_hdl != NULL) && (lcd_pdata != NULL) && (lcd_pdata->lcd_if == LCD_SPI)) {
        dev_ioctl(lcd_hdl, IOCTL_SPI_WRITE_NON_BLOCK_FLUSH, 0);
    }
}

/**
 * @brief 组合片选及命令电平提交一个命令字节，并调用既有 SPI 等待。 完整合同见 d_lcd_compat.h。
 */
void WriteCOM(u8 cmd)
{
    lcd_cs_pinstate(0);
    lcd_rs_pinstate(0); // CMD
    lcd_send_byte(cmd);
    lcd_spi_write_wait();
    lcd_cs_pinstate(1);
}

/**
 * @brief 组合片选及数据电平提交一个数据字节，并调用既有 SPI 等待。 完整合同见 d_lcd_compat.h。
 */
void WriteDAT_8(u8 dat)
{
    lcd_cs_pinstate(0);
    lcd_rs_pinstate(1); // DAT
    lcd_send_byte(dat);
    lcd_spi_write_wait();
    lcd_cs_pinstate(1);
}

/**
 * @brief 组合片选及数据电平提交数据块，并沿原路径等待。 完整合同见 d_lcd_compat.h。
 */
void WriteDAT_DMA(u8 *dat, u32 len)
{
    lcd_cs_pinstate(0);
    lcd_rs_pinstate(1); // DAT
    lcd_send_map(dat, len);
    lcd_spi_write_wait();
    lcd_cs_pinstate(1);
}

#ifdef D_LCD_EMI_ILI948X_ENABLE
/**
 * @brief 在 EMI 配置中用静态小块缓冲重复提交纯色 RGB565 像素。 完整合同见 d_lcd_compat.h。
 */
void d_lcd_write_solid_pixels(u16 color, u32 pixel_count)
{
    u32 chunk_pixels;
    u32 i;

    if (pixel_count == 0U) {
        return;
    }

    for (i = 0U; i < D_LCD_SOLID_CHUNK_PIXELS; ++i) {
        s_lcd_solid_chunk[(i * 2U)] = (u8)((color >> 8) & 0xffU);
        s_lcd_solid_chunk[(i * 2U) + 1U] = (u8)(color & 0xffU);
    }

    lcd_cs_pinstate(0);
    lcd_rs_pinstate(1);
    while (pixel_count > 0U) {
        chunk_pixels = pixel_count;
        if (chunk_pixels > D_LCD_SOLID_CHUNK_PIXELS) {
            chunk_pixels = D_LCD_SOLID_CHUNK_PIXELS;
        }
        if (lcd_send_map(s_lcd_solid_chunk, chunk_pixels * 2U) < 0) {
            lcd_e("Solid color transfer failed\n");
            break;
        }
        lcd_interface_non_block_wait();
        pixel_count -= chunk_pixels;
    }
    lcd_cs_pinstate(1);
}
#endif

/**
 * @brief 按当前总线调用已有非阻塞写入的等待/刷新入口。 完整合同见 d_lcd_compat.h。
 */
void lcd_interface_non_block_wait(void)
{
    if (lcd_hdl && lcd_pdata) {
        if (lcd_pdata->lcd_if == LCD_SPI) {
#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
            if (d_lcd_spi_async_is_ready()) {
                d_lcd_spi_async_wait(LCD_SPI_ASYNC_COMMAND_TIMEOUT_TICKS);
                return;
            }
#endif
            dev_ioctl(lcd_hdl, IOCTL_SPI_WRITE_NON_BLOCK_FLUSH, 0);
        } else if (lcd_pdata->lcd_if == LCD_EMI) {
            dev_ioctl(lcd_hdl, IOCTL_EMI_WRITE_NON_BLOCK_FLUSH, 0);
        }
    }
}

// 屏幕识别函数（通过 D6/D7 引脚读取）
#ifdef D_LCD_EMI_ILI948X_ENABLE
/**
 * @brief 切换 EMI 型号识别引脚 D6/D7 的方向。
 * @param[in] dir 非零设为输入，零设为输出。
 * @note 平台描述符须有效；识别完成后恢复输出，避免占用 EMI 数据线。
 */
static void lcd_check_pin_io_dir(u8 dir)
{
    if ((lcd_pdata->check_pin_D6 != (u32)-1) && (lcd_pdata->check_pin_D7 != (u32)-1)) {
        if (dir) {
            gpio_set_direction(lcd_pdata->check_pin_D6, 1);
            gpio_set_direction(lcd_pdata->check_pin_D7, 1);
        } else {
            gpio_set_direction(lcd_pdata->check_pin_D6, 0);
            gpio_set_direction(lcd_pdata->check_pin_D7, 0);
        }
    }
}

/**
 * @brief 在打开 EMI 前用 D6/D7 电平选择已有面板型号。
 * @return 两个识别引脚均为高电平时为 1（ILI9488），其余及未配引脚为 0（ILI9481）。
 * @note 会临时切换引脚方向并在返回前恢复输出，不与正在使用的数据总线并发。
 */
static int get_check_pin_io_state(void)
{
    if ((lcd_pdata->check_pin_D6 != (u32)-1) && (lcd_pdata->check_pin_D7 != (u32)-1)) {
        // 先设置输出高，再切换输入读取，最后恢复为输出（避免占用 EMI 数据线 D6/D7）
        lcd_check_pin_io_dir(0);
        gpio_direction_output(lcd_pdata->check_pin_D6, 1);
        gpio_direction_output(lcd_pdata->check_pin_D7, 1);
        lcd_check_pin_io_dir(1);

        int type = 0;
        if (gpio_read(lcd_pdata->check_pin_D6) && gpio_read(lcd_pdata->check_pin_D7)) {
            lcd_d("Detected ILI9488\n");
            type = 1;
        } else {
            lcd_d("Detected ILI9481\n");
            type = 0;
        }

        lcd_check_pin_io_dir(0);
        return type;
    }
    lcd_d("No check pins, default to ILI9481\n");
    return 0; // 默认 ILI9481
}

// LCD 数据发送完成回调（用于通知 lcd_data_driver）
extern void lcd_send_ok(void);
/**
 * @brief 将 EMI 发送完成通知转交 SDK lcd_data_driver。
 * @note 由 SDK 中断回调入口执行，不打印高频日志或操作页面。
 */
static void lcd_send_ok_callback(void)
{
    /* 移除频繁的打印，减少日志噪音 */
    lcd_send_ok();
}
#endif /* D_LCD_EMI_ILI948X_ENABLE */

/**
 * @brief 按已选总线打开设备并登记所需回调及非阻塞模式。
 * @return 设备成功打开为 0，未支持的总线或打开失败为 -1。
 * @note GC 异步配置失败仍保留已打开句柄并降级；EMI 当前不把 ioctl 配置错误作为打开失败上报。
 */
static int lcd_interface_init(void)
{
#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
    if (lcd_pdata->lcd_if == LCD_SPI) {
        if (lcd_pdata->spi_id == NULL) {
            lcd_e("LCD SPI id is NULL\n");
            return -1;
        }
        lcd_hdl = dev_open(lcd_pdata->spi_id, NULL);
        if (lcd_hdl) {
            int ret = lcd_spi_async_setup();
            /* D++: 接口打开细节属于迁移诊断，普通启动仅保留简短日志。 */
#ifdef D_LCD_IF_DIAG_LOG_ENABLE
            printf("[D_CODE] SPI LCD opened id=%s nonblock=%d\n", lcd_pdata->spi_id, ret);
#endif
            if (ret != 0) {
                dev_ioctl(lcd_hdl, IOCTL_SPI_NON_BLOCK, 1);
                printf("[GC9307-SPI] degraded synchronous fallback\n");
            }
            return 0;
        }
    }
#endif

#ifdef D_LCD_EMI_ILI948X_ENABLE
    if (lcd_pdata->lcd_if == LCD_EMI) {
        lcd_hdl = dev_open("emi", NULL);
        if (lcd_hdl) {
            // 设置数据发送完成回调（摄像头显示需要）
            int ret1 = dev_ioctl(lcd_hdl, EMI_SET_ISR_CB, lcd_send_ok_callback);
            int ret2 = dev_ioctl(lcd_hdl, EMI_USE_SEND_SEM, 1);
            // 设置非阻塞模式（摄像头显示需要）
            int ret3 = dev_ioctl(lcd_hdl, IOCTL_EMI_WRITE_NON_BLOCK, 1);
            /* D++: EMI 回调装配细节仅在 EMI/ILI948x 诊断时需要。 */
#ifdef D_LCD_IF_DIAG_LOG_ENABLE
            printf("[D_CODE] EMI callbacks set: ISR_CB=%d, USE_SEND_SEM=%d, NON_BLOCK=%d\n", ret1, ret2, ret3);
#endif
            (void)ret1;
            (void)ret2;
            (void)ret3;
            lcd_d("EMI device opened with ISR callback\n");
            return 0;
        }
    }
#endif

    lcd_e("LCD interface init failed\n");
    return -1;
}

/**
 * @brief 绑定平台参数、选择面板驱动并初始化总线及面板。 完整合同见 d_lcd_compat.h。
 */
int d_lcd_init(const struct lcd_platform_data *pdata)
{
    if (!pdata) {
        lcd_e("LCD platform data is NULL\n");
        return -1;
    }

    lcd_pdata = pdata;

#ifdef D_LCD_BL_ACTIVE_LOW
    lcd_bk_on = 0;
    lcd_bk_off = 1;
#else
    lcd_bk_on = 1;
    lcd_bk_off = 0;
#endif

    lcd_d("LCD init start, interface type: %d\n", pdata->lcd_if);

    const struct lcd_device *found = NULL;

#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
    if (pdata->lcd_if == LCD_SPI) {
        found = d_find_lcd_driver("gc9307");
        if (found) {
            lcd_d("Found LCD driver: gc9307\n");
        }
    }
#endif

#ifdef D_LCD_EMI_ILI948X_ENABLE
    if ((found == NULL) && (pdata->lcd_if == LCD_EMI)) {
        // 识别屏幕型号（通过 D6/D7 引脚读取）
        // 注意：D6/D7 与 EMI 数据线复用，必须在打开 EMI 设备前完成识别并恢复 IO 方向
        int lcd_type = get_check_pin_io_state();

    if (lcd_type == 0) {
        found = d_find_lcd_driver("ili9481");
        if (found) {
            lcd_d("Found LCD driver: ili9481\n");
        }
    } else {
        found = d_find_lcd_driver("ili9488");
        if (found) {
            lcd_d("Found LCD driver: ili9488\n");
        }
    }
    }
#endif

    lcd_dev = (struct lcd_device *)found;
    if (!lcd_dev || !lcd_dev->LCD_Init) {
        lcd_e("No matching LCD driver found\n");
        return -1;
    }

    // 打开 LCD 接口设备
    if (lcd_interface_init() != 0) {
        return -1;
    }

    // 先获取屏幕尺寸（摄像头显示需要）
    lcd_w = lcd_dev->lcd_width;
    lcd_h = lcd_dev->lcd_height;
    lcd_d("LCD resolution: %dx%d\n", lcd_w, lcd_h);

#ifdef D_LCD_EMI_ILI948X_ENABLE
    if (pdata->lcd_if == LCD_EMI) {
    // 设置 lcd_data_driver 参数（摄像头显示需要）
    extern void set_lcd_data_driver_lcd_h_w(u16 lcd_w, u16 lcd_h);
    extern void set_lcd_data_size(u32 size);
    extern void set_lcd_yuv420_data_size(u32 size);
    extern void set_lcd_bk_on(u8 on);
    extern void set_lcd_te_pgio(u32 gpio);
    extern void malloc_lcd_buf(void);
    extern void picture_compose_task_init(void);

    u32 lcd_data_size = lcd_w * lcd_h * 2;  // RGB565
    u32 lcd_yuv420_data_size = lcd_w * lcd_h * 3 / 2;  // YUV420

    set_lcd_data_driver_lcd_h_w(lcd_w, lcd_h);
    set_lcd_data_size(lcd_data_size);
    set_lcd_yuv420_data_size(lcd_yuv420_data_size);
    set_lcd_bk_on(lcd_bk_on);
    set_lcd_te_pgio(pdata->te_pin);

    // 申请全局 LCD 显示 buf
    malloc_lcd_buf();
    }
#endif

    // 调用具体屏驱动的初始化
    if (lcd_dev->LCD_Init() != 0) {
        lcd_e("LCD driver init failed\n");
        return -1;
    }

#ifdef D_LCD_EMI_ILI948X_ENABLE
    if (pdata->lcd_if == LCD_EMI) {
    // 启动图像合成任务（摄像头显示需要）
    /* D++: EMI/ILI948x 沿原流程启动合成任务，详细日志保持显式开启。 */
#ifdef D_LCD_IF_DIAG_LOG_ENABLE
    printf("[LCD_DRIVE] Starting picture compose task...\n");
#endif
    picture_compose_task_init();
#ifdef D_LCD_IF_DIAG_LOG_ENABLE
    printf("[LCD_DRIVE] picture compose task initialized\n");
#endif
    }
#endif

    lcd_d("LCD init success\n");
    return 0;
}

/**
 * @brief 调用已选面板驱动的可选清屏入口。 完整合同见 d_lcd_compat.h。
 */
void d_lcd_clear_screen(u16 color)
{
    if (lcd_dev && lcd_dev->LCD_ClearScreen) {
        lcd_dev->LCD_ClearScreen(color);
    }
}

/**
 * @brief 读取已选驱动登记的宽度。 完整合同见 d_lcd_compat.h。
 */
u16 d_lcd_get_width(void)
{
    return lcd_w;
}

/**
 * @brief 保留 LVGL 所需的兼容初始化入口，当前不重复初始化面板。 完整合同见 d_lcd_compat.h。
 */
void user_lcd_init(void)
{
    // LCD 已在 d_lcd_init() 中初始化，此处无需操作
}

/**
 * @brief 读取已选驱动登记的高度。 完整合同见 d_lcd_compat.h。
 */
u16 d_lcd_get_height(void)
{
    return lcd_h;
}

/**
 * @brief 调用共用清屏入口填充纯色。 完整合同见 d_lcd_compat.h。
 */
void d_lcd_fill_color(u16 color)
{
    d_lcd_clear_screen(color);
}

/**
 * @brief 将一块 UI 像素转交所选面板或 EMI 图像合成链路。 完整合同见 d_lcd_compat.h。
 */
void lcd_lvgl_full(u16 xs, u16 xe, u16 ys, u16 ye, u8 *img)
{
#ifdef D_LCD_GC9307_SDK_LCD_ENABLE
    if (lcd_dev && lcd_dev->LCD_Lvgl_Full) {
        lcd_dev->LCD_Lvgl_Full(xs, xe, ys, ye, (char *)img);
    } else {
        printf("[LCD_LVGL_FULL] ERROR: GC9307 lcd_dev or LCD_Lvgl_Full NULL!\n");
    }
    return;
#endif

#ifdef D_LCD_EMI_ILI948X_ENABLE
    static u32 lcd_lvgl_full_count = 0;

    lcd_lvgl_full_count++;
    // 检查当前显示模式
    extern u8 get_lcd_show_deta_mode(void);
    extern u8 get_lcd_compose_status(void);  // 获取合成状态
    extern void set_lcd_ui_x_y(u16 xstart, u16 xend, u16 ystart, u16 yend);
    extern void ui_send_data_ready(u8 *data_buf, u32 data_size);

    u8 mode = get_lcd_show_deta_mode();

    /* D++: 普通界面不输出高频 EMI 刷新跟踪；下方错误仍无条件报告。 */
#ifdef D_LCD_EMI_FLUSH_LOG_ENABLE
    if (lcd_lvgl_full_count <= 10) {
        printf("[LCD_LVGL_FULL] #%d mode=%d img=%x area=%d,%d,%d,%d\n",
               lcd_lvgl_full_count, mode, (u32)img, xs, xe, ys, ye);
    }
#endif

    // data_mode == UI_CAMERA (值为2) 时，需要检查 compose_status
    // compose_status == 1: 需要UI合成
    // compose_status == 0: 摄像头直接显示，忽略LVGL刷屏
    if (mode == 2) {  // UI_CAMERA 模式 (包括 CAMERA 和 UI_CAMERA)
        u8 compose_status = get_lcd_compose_status();

        if (compose_status) {
            // 需要UI合成：发送UI数据到 lcd_data_driver
            set_lcd_ui_x_y(xs, xe, ys, ye);
            u32 data_size = (xe - xs + 1) * (ye - ys + 1) * 2;
            ui_send_data_ready(img, data_size);
        }
        // 如果 compose_status == 0，忽略LVGL刷屏（摄像头数据直接显示）
    } else {
        // UI 模式：直接刷屏
#ifdef D_LCD_EMI_FLUSH_LOG_ENABLE
        if (lcd_lvgl_full_count <= 10) {
            printf("[LCD_LVGL_FULL] UI mode, lcd_dev=%x\n", (u32)lcd_dev);
        }
#endif
        if (lcd_dev && lcd_dev->SetDrawArea && lcd_dev->LCD_Draw) {
            lcd_dev->SetDrawArea(xs, xe, ys, ye);
            u32 len = (xe - xs + 1) * (ye - ys + 1) * 2;
            lcd_dev->LCD_Draw(img, len);
        } else {
            printf("[LCD_LVGL_FULL] ERROR: lcd_dev or functions NULL!\n");
        }
    }
#else
    (void)xs;
    (void)xe;
    (void)ys;
    (void)ye;
    (void)img;
#endif
}
