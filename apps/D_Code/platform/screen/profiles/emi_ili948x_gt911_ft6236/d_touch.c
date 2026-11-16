/**
 * @file d_touch.c
 * @brief 在 EMI 触摸任务中探测并读取 GT911/FT6236，UI 读取端另执行屏幕唤醒过滤。
 */

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)

#include "system/includes.h"
#include "device/device.h"
#include "device/iic.h"
#include "asm/iic.h"
#include "asm/port_waked_up.h"
#include "framework/d_error.h"
#include "platform/screen/profiles/emi_ili948x_gt911_ft6236/d_touch.h"
#include "platform/screen/power/d_screen_power.h"
#include "platform/screen/lcd/d_lcd_compat.h"

static void *s_iic = NULL;
static OS_SEM s_touch_sem;
static u8 s_touch_type = 0;
static u8 s_touch_down = 0;
static const struct lcd_platform_data *s_pdata = NULL;
static u16 s_last_x = 0;
static u16 s_last_y = 0;
static u8 s_gt911_wrcmd = 0x28;
static u8 s_gt911_rdcmd = 0x29;

enum {
    TOUCH_TYPE_NONE = 0,
    TOUCH_TYPE_GT911 = 1,
    TOUCH_TYPE_FT6236 = 2,
};

/**
 * @brief 在中断中只通知触摸读取任务，不读取总线或调用 UI。
 */
static void touch_irq_cb(void)
{
    os_sem_post(&s_touch_sem);
}

/**
 * @brief 向已打开 IIC 提交候选写命令并检查地址应答。
 * @param[in] wrcmd 候选写命令字节。
 * @return 地址被 SDK 接受为 1，否则为 0；结束时发送 STOP。
 */
static int iic_probe_wrcmd(u8 wrcmd)
{
    int ack = 0;
    dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, wrcmd) == 0) {
        ack = 1;
    }
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return ack;
}

/**
 * @brief 为有效触摸中断脚设置输入、上拉及数字输入。
 * @param[in] pin 板级 GPIO；未配置的 -1 值不处理。
 */
static void touch_int_prepare_input(u32 pin)
{
    if (pin == (u32)-1) {
        return;
    }
    gpio_set_direction(pin, 1);
    gpio_set_pull_up(pin, 1);
    gpio_set_pull_down(pin, 0);
    gpio_set_die(pin, 1);
}

/**
 * @brief 沿 SDK 例程的 INT 电平时序复位 GT911 并恢复中断输入。
 * @param[in] pdata 板级描述符；NULL 或缺少必需引脚时不执行。
 * @note 包含任务延时，只在启动阶段调用；当前值决定既有候选地址选择，不在此改地址策略。
 */
static void gt911_reset_sequence(const struct lcd_platform_data *pdata)
{
    if (!pdata) {
        return;
    }
    u32 rst_pin = pdata->touch_reset_pin != (u32)-1 ? pdata->touch_reset_pin : pdata->rst_pin;
    if (rst_pin == (u32)-1 || pdata->touch_int_pin == (u32)-1) {
        return;
    }
    // 对齐 SDK check_gt911(): 通过 INT 电平决定 I2C 地址
    gpio_direction_output(rst_pin, 0);
    os_time_dly(1);
    gpio_direction_output(pdata->touch_int_pin, 1);
    os_time_dly(1);
    gpio_direction_output(rst_pin, 1);
    os_time_dly(1);
    gpio_direction_output(pdata->touch_int_pin, 0);
    os_time_dly(10);

    touch_int_prepare_input(pdata->touch_int_pin);
}

/**
 * @brief 按既有延时复位 FT6236，专用复位脚缺失时借用面板复位脚。
 * @param[in] pdata 板级描述符；NULL 或无可用复位脚时不处理。
 * @note 上层负责在共享复位的情形决定是否允许调用。
 */
static void ft6236_reset_sequence(const struct lcd_platform_data *pdata)
{
    if (!pdata) {
        return;
    }
    u32 rst_pin = pdata->touch_reset_pin != (u32)-1 ? pdata->touch_reset_pin : pdata->rst_pin;
    if (rst_pin == (u32)-1) {
        return;
    }
    gpio_direction_output(rst_pin, 0);
    os_time_dly(6);
    gpio_direction_output(rst_pin, 1);
    os_time_dly(10);
}

/**
 * @brief 以两字节地址提交一个寄存器字节并发送 STOP。
 * @param[in] wrcmd 写命令字节。
 * @param[in] reg 16 位寄存器地址，高字节在前。
 * @param[in] val 待写字节。
 * @return 各发送步骤未报告错误为 1，任一步报告错误为 0。
 * @note 仅触摸任务/初始化路径串行使用已打开的 IIC，不在此处理业务状态。
 */
static int iic_write_u16addr_u8(u8 wrcmd, u16 reg, u8 val)
{
    int ok = 1;
    dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, wrcmd)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX, (reg >> 8) & 0xff)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX, reg & 0xff)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_STOP_BIT, val)) {
        ok = 0;
        goto exit;
    }
    delay(100);
exit:
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return ok;
}

/**
 * @brief 提交两字节寄存器地址及读命令，再读取一个字节。
 * @param[in] wrcmd 写命令字节。
 * @param[in] rdcmd 读命令字节。
 * @param[in] reg 16 位寄存器地址。
 * @param[out] val 有效的一字节输出；接收失败时不可据返回 1 推断该值已更新。
 * @return 地址和读命令未报告发送错误为 1；发送失败为 0。接收 ioctl 返回值当前未纳入此结果。
 */
static int iic_read_u16addr_u8(u8 wrcmd, u8 rdcmd, u16 reg, u8 *val)
{
    int ok = 1;
    dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, wrcmd)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX, (reg >> 8) & 0xff)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX, reg & 0xff)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, rdcmd)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    dev_ioctl(s_iic, IIC_IOCTL_RX_WITH_STOP_BIT, (u32)val);
exit:
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return ok;
}

/**
 * @brief 以单字节地址提交一个寄存器字节并发送 STOP。
 * @param[in] wrcmd 写命令字节。
 * @param[in] reg 8 位寄存器地址。
 * @param[in] val 待写字节。
 * @return 发送步骤未报告错误为 1，否则为 0。
 */
static int iic_write_u8addr_u8(u8 wrcmd, u8 reg, u8 val)
{
    int ok = 1;
    dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, wrcmd)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX, reg)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_STOP_BIT, val)) {
        ok = 0;
        goto exit;
    }
    delay(100);
exit:
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return ok;
}

/**
 * @brief 提交单字节寄存器地址及读命令，再读取一个字节。
 * @param[in] wrcmd 写命令字节。
 * @param[in] rdcmd 读命令字节。
 * @param[in] reg 8 位寄存器地址。
 * @param[out] val 有效的一字节输出，失败时可能保持旧值。
 * @return 地址和读命令未报告发送错误为 1，否则为 0；接收 ioctl 错误未纳入现有返回。
 */
static int iic_read_u8addr_u8(u8 wrcmd, u8 rdcmd, u8 reg, u8 *val)
{
    int ok = 1;
    dev_ioctl(s_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, wrcmd)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX, reg)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    if (dev_ioctl(s_iic, IIC_IOCTL_TX_WITH_START_BIT, rdcmd)) {
        ok = 0;
        goto exit;
    }
    delay(100);
    dev_ioctl(s_iic, IIC_IOCTL_RX_WITH_STOP_BIT, (u32)val);
exit:
    dev_ioctl(s_iic, IIC_IOCTL_STOP, 0);
    return ok;
}

/**
 * @brief 使用一组读写命令读取三个 PID 字节并匹配 911。
 * @param[in] wrcmd 候选写命令。
 * @param[in] rdcmd 候选读命令。
 * @return 三个读取路径返回真且字节匹配时为 1，否则为 0。
 */
static int gt911_detect_with_addr(u8 wrcmd, u8 rdcmd)
{
    u8 b0 = 0, b1 = 0, b2 = 0;
    if (!iic_read_u16addr_u8(wrcmd, rdcmd, 0x8140, &b0)) {
        return 0;
    }
    if (!iic_read_u16addr_u8(wrcmd, rdcmd, 0x8141, &b1)) {
        return 0;
    }
    if (!iic_read_u16addr_u8(wrcmd, rdcmd, 0x8142, &b2)) {
        return 0;
    }
    printf("[TOUCH] GT911 pid bytes: %u %u %u\n", b0, b1, b2);
    if (b0 == '9' && b1 == '1' && b2 == '1') {
        return 1;
    }
    return 0;
}

/**
 * @brief 依次试验既有 GT911 命令组，并保存首个匹配组。
 * @return 识别成功为 1；两组均未匹配为 0。
 */
static int gt911_detect(void)
{
    // SDK 例程常用 0x28/0x29；部分板子可能是 0xBA/0xBB
    if (gt911_detect_with_addr(0x28, 0x29)) {
        s_gt911_wrcmd = 0x28;
        s_gt911_rdcmd = 0x29;
        return 1;
    }
    if (gt911_detect_with_addr(0xBA, 0xBB)) {
        s_gt911_wrcmd = 0xBA;
        s_gt911_rdcmd = 0xBB;
        return 1;
    }
    return 0;
}

/**
 * @brief 按既有命令读取 FT 芯片 ID 并匹配当前支持值。
 * @return ID 为 0x64 或 0x36 且读取路径未报告发送错误时为 1，否则为 0。
 */
static int ft6236_detect(void)
{
    const u8 FT_WRCMD = 0x70;
    const u8 FT_RDCMD = 0x71;
    u8 id = 0;
    if (!iic_read_u8addr_u8(FT_WRCMD, FT_RDCMD, 0xA3, &id)) {
        return 0;
    }
    printf("[TOUCH] FT6236 chip id: 0x%02x\n", id);
    if (id == 0x64 || id == 0x36) {
        return 1;
    }
    return 0;
}

/**
 * @brief 执行原 FT6236 阈值、周期和中断模式寄存器序列并读回诊断。
 * @note 保留所有配置值；当前忽略各写入/读回错误，不作为独立配置成功确认。
 */
static void ft6236_init_regs(void)
{
    // 对齐 apps/common/ui/touch_panel/ft6236.c 的初始化写寄存器流程
    const u8 FT_WRCMD = 0x70;
    const u8 FT_RDCMD = 0x71;
    u8 read_data = 0;

    // FT_DEVIDE_MODE = 0x00
    iic_write_u8addr_u8(FT_WRCMD, 0x00, 0x00);
    iic_write_u8addr_u8(FT_WRCMD, 0x80, 22);
    iic_read_u8addr_u8(FT_WRCMD, FT_RDCMD, 0x80, &read_data);
    printf("[TOUCH] FT THGROUP=%u\n", read_data);

    iic_write_u8addr_u8(FT_WRCMD, 0x88, 13);
    iic_read_u8addr_u8(FT_WRCMD, FT_RDCMD, 0x88, &read_data);
    printf("[TOUCH] FT PERIODACTIVE=%u\n", read_data);

    // FT_ID_G_MODE = 0xA4
    iic_write_u8addr_u8(FT_WRCMD, 0xA4, 1);
    iic_read_u8addr_u8(FT_WRCMD, FT_RDCMD, 0xA4, &read_data);
    printf("[TOUCH] FT MODE=%u\n", read_data);
}

/**
 * @brief 读取 GT911 状态及首点，裁剪到已登记面板尺寸并更新缓存。
 * @note 沿现有 bit7 判定接触路径，不额外检查低位点数；读取路径提前失败时保留旧缓存，不提供 HYN 式失效取消。
 */
static void gt911_read_and_print(void)
{
    u8 status = 0;
    u8 buf[4] = {0};
    u16 x = 0;
    u16 y = 0;

    if (!iic_read_u16addr_u8(s_gt911_wrcmd, s_gt911_rdcmd, 0x814E, &status)) {
        return;
    }

    if (status & 0x80) {
        for (u8 i = 0; i < 4; i++) {
            if (!iic_read_u16addr_u8(s_gt911_wrcmd, s_gt911_rdcmd, 0x8150 + i, &buf[i])) {
                return;
            }
        }
        x = (u16)buf[0] | ((u16)buf[1] << 8);
        y = (u16)buf[2] | ((u16)buf[3] << 8);

        if (x >= lcd_w) {
            x = lcd_w ? (lcd_w - 1) : 0;
        }
        if (y >= lcd_h) {
            y = lcd_h ? (lcd_h - 1) : 0;
        }
        s_last_x = x;
        s_last_y = y;
        if (!s_touch_down) {
            s_touch_down = 1;
            printf("[TOUCH] x=%u y=%u\n", x, y);
        }

        iic_write_u16addr_u8(s_gt911_wrcmd, 0x814E, 0x00);
    } else {
        s_touch_down = 0;
    }
}

/**
 * @brief 按 FT 状态字节读取首点并更新裁剪后的原生缓存。
 * @note 状态字节为零时清接触；读取提前失败保留旧状态，首次按下时才打印坐标。
 */
static void ft6236_read_and_print(void)
{
    const u8 FT_WRCMD = 0x70;
    const u8 FT_RDCMD = 0x71;
    u8 n = 0;
    u8 buf[4] = {0};
    u16 x = 0;
    u16 y = 0;

    if (!iic_read_u8addr_u8(FT_WRCMD, FT_RDCMD, 0x02, &n)) {
        return;
    }

    if (n) {
        for (u8 i = 0; i < 4; i++) {
            if (!iic_read_u8addr_u8(FT_WRCMD, FT_RDCMD, 0x03 + i, &buf[i])) {
                return;
            }
        }
        x = (((u16)(buf[0] & 0x0F)) << 8) | buf[1];
        y = (((u16)(buf[2] & 0x0F)) << 8) | buf[3];

        if (x >= lcd_w) {
            x = lcd_w ? (lcd_w - 1) : 0;
        }
        if (y >= lcd_h) {
            y = lcd_h ? (lcd_h - 1) : 0;
        }
        s_last_x = x;
        s_last_y = y;
        if (!s_touch_down) {
            s_touch_down = 1;
            printf("[TOUCH] x=%u y=%u\n", x, y);
        }
    } else {
        s_touch_down = 0;
    }
}

/**
 * @brief 读取最近原生坐标并执行唤醒过滤；完整合同见 d_touch.h。
 */
void get_touch_x_y_status(s16 *x, s16 *y, unsigned char *status)
{
    /* 平台级锁屏触摸门控：息屏后的触摸先交给背光唤醒逻辑，避免误点 UI。 */
    u8 filtered_status = d_screen_power_filter_touch(s_touch_down);

    if (x) *x = s_last_x;
    if (y) *y = s_last_y;
    if (status) *status = filtered_status;
}

/**
 * @brief 在中断或既有超时唤醒后，按已识别型号读取触摸。
 * @param[in] priv SDK 任务参数，当前未使用。
 * @note 常驻任务；型号未识别时不读取，不在此调用 LVGL。
 */
static void d_touch_task(void *priv)
{
    (void)priv;

    while (1) {
        os_sem_pend(&s_touch_sem, 5);
        if (s_touch_type == TOUCH_TYPE_GT911) {
            gt911_read_and_print();
        } else if (s_touch_type == TOUCH_TYPE_FT6236) {
            ft6236_read_and_print();
        }
    }
}

/**
 * @brief 在初始化失败且读取任务尚未建立时释放本次中断、信号量和 IIC。
 * @param[in] err 要传播的初始化错误。
 * @param[in] wakeup_hdl 本次取得的中断注册句柄，可为 NULL。
 * @return 原样返回 err。
 * @note 只用于初始化补偿，不提供运行期任务结束或回调在途排空确认。
 */
static d_err_t d_touch_cleanup(d_err_t err, void *wakeup_hdl)
{
    if (wakeup_hdl != NULL) {
        port_wakeup_unreg(wakeup_hdl);
    }
    if (os_sem_valid(&s_touch_sem) != 0) {
        os_sem_del(&s_touch_sem, OS_DEL_ALWAYS);
    }
    if (s_iic != NULL) {
        dev_close(s_iic);
        s_iic = NULL;
    }
    s_pdata = NULL;
    s_touch_type = TOUCH_TYPE_NONE;
    s_touch_down = 0;
    s_last_x = 0;
    s_last_y = 0;
    return err;
}

/**
 * @brief 准备 EMI 触摸设备与常驻任务；完整合同见 d_touch.h。
 */
d_err_t d_touch_init(const struct lcd_platform_data *pdata)
{
    void *touch_wakeup_hdl;
    int ret;

    if (!pdata) {
        return D_ERR_INVALID_ARG;
    }

    if (pdata->touch_int_pin == (u32)-1) {
        printf("[TOUCH] touch_int_pin invalid\n");
        return D_ERR_NOT_FOUND;
    }

    s_pdata = pdata;
    touch_int_prepare_input(pdata->touch_int_pin);
    s_iic = dev_open("iic0", NULL);
    if (!s_iic) {
        printf("[TOUCH] open iic0 failed\n");
        s_pdata = NULL;
        return D_ERR_IO;
    }

    int ack_28 = iic_probe_wrcmd(0x28);
    int ack_ba = iic_probe_wrcmd(0xBA);
    int ack_70 = iic_probe_wrcmd(0x70);
    int ack_a8 = iic_probe_wrcmd(0xA8);
    printf("[TOUCH] probe 0x28=%d 0xBA=%d 0x70=%d 0xA8=%d\n",
           ack_28, ack_ba, ack_70, ack_a8);

    int rst_shared = (pdata->touch_reset_pin != (u32)-1) && (pdata->touch_reset_pin == pdata->rst_pin);
    if (rst_shared) {
        printf("[TOUCH] shared reset pin with LCD, skip touch reset\n");
    }

    // 若 0x70 ACK，优先按 FT6236 流程初始化（DevKitBoard 320x480 默认 FT6236）
    if (ack_70) {
        touch_int_prepare_input(pdata->touch_int_pin);
        if (!rst_shared) {
            ft6236_reset_sequence(pdata);
        } else {
            os_time_dly(10);
        }
        ft6236_init_regs();
        if (ft6236_detect()) {
            s_touch_type = TOUCH_TYPE_FT6236;
            printf("[TOUCH] detected FT6236\n");
        } else if (ack_28 || ack_ba) {
            if (!rst_shared) {
                gt911_reset_sequence(pdata);
            }
            if (gt911_detect()) {
                s_touch_type = TOUCH_TYPE_GT911;
                printf("[TOUCH] detected GT911\n");
            } else {
                s_touch_type = TOUCH_TYPE_NONE;
            }
        } else {
            s_touch_type = TOUCH_TYPE_NONE;
        }
    } else {
        // 未发现 FT6236 地址，尝试 GT911
        if (!rst_shared) {
            gt911_reset_sequence(pdata);
        }
        if (gt911_detect()) {
            s_touch_type = TOUCH_TYPE_GT911;
            printf("[TOUCH] detected GT911\n");
        } else if (ack_70) {
            if (!rst_shared) {
                ft6236_reset_sequence(pdata);
            }
            if (ft6236_detect()) {
                s_touch_type = TOUCH_TYPE_FT6236;
                printf("[TOUCH] detected FT6236\n");
            } else {
                s_touch_type = TOUCH_TYPE_NONE;
            }
        } else {
            s_touch_type = TOUCH_TYPE_NONE;
        }
    }

    if (s_touch_type == TOUCH_TYPE_NONE) {
        printf("[TOUCH] no supported touch IC\n");
        dev_close(s_iic);
        s_iic = NULL;
        s_pdata = NULL;
        return D_ERR_NOT_FOUND;
    }

    if (os_sem_create(&s_touch_sem, 0) != OS_NO_ERR) {
        printf("[TOUCH] sem create failed\n");
        return d_touch_cleanup(D_ERR_IO, NULL);
    }

    touch_wakeup_hdl = port_wakeup_reg(EVENT_IO_0, pdata->touch_int_pin, EDGE_NEGATIVE, touch_irq_cb);
    if (touch_wakeup_hdl == NULL) {
        printf("[TOUCH] irq register failed\n");
        return d_touch_cleanup(D_ERR_IO, NULL);
    }

    ret = thread_fork("d_touch", 29, 1024, 0, NULL, d_touch_task, NULL);
    if (ret != OS_NO_ERR) {
        printf("[TOUCH] d_touch task create failed: %d\n", ret);
        return d_touch_cleanup(D_ERR_IO, touch_wakeup_hdl);
    }

    return D_OK;
}

#endif /* D_LCD_ENABLE && D_LCD_EMI_ILI948X_ENABLE */
