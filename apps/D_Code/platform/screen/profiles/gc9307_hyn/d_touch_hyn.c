/**
 * @file d_touch_hyn.c
 * @brief 在专属任务读取 HYN 触摸，发布原生坐标及取消代次，不直接操作 LVGL。
 */

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_TOUCH_HYN_DRIVER_ENABLE)

#include "system/includes.h"
#include "device/device.h"
#include "device/iic.h"
#include "asm/gpio.h"
#include "asm/port_waked_up.h"
#include "platform/screen/profiles/gc9307_hyn/d_touch_hyn.h"
#include "platform/screen/profiles/gc9307_hyn/d_touch_gesture.h"

#define HYN_TAG                         "[HYN] "
#define HYN_CST8XXT_MAX_POINTS          2
#define HYN_CST8XXT_FRAME_LEN           (3 + 6 * HYN_CST8XXT_MAX_POINTS)
#define HYN_CST8XXT_VALIDATE_LEN        8
#define HYN_TOUCH_POLL_TICKS            2
#define HYN_IDLE_LOG_DIV                50
/* 保留宏旧名；达到此连续失败阈值执行取消，绝不合成正常松手。 */
#define HYN_FRAME_FAIL_RELEASE_LIMIT    3
#define HYN_SCREEN_WIDTH                240
#define HYN_SCREEN_HEIGHT               320

#ifndef D_TOUCH_HYN_SWAP_XY
#define D_TOUCH_HYN_SWAP_XY             0
#endif
#ifndef D_TOUCH_HYN_REVERSE_X
#define D_TOUCH_HYN_REVERSE_X           0
#endif
#ifndef D_TOUCH_HYN_REVERSE_Y
#define D_TOUCH_HYN_REVERSE_Y           0
#endif

static void *s_hyn_iic = NULL;
static void *s_hyn_wakeup_hdl = NULL;
static OS_SEM s_hyn_sem;
static u8 s_hyn_sem_ready = 0;
static u8 s_hyn_wrcmd = 0x2a;
static u8 s_hyn_rdcmd = 0x2b;
static u8 s_hyn_ready = 0;
static volatile u8 s_hyn_down = 0;
static volatile u8 s_hyn_points = 0;
static u32 s_hyn_int_pin = (u32)-1;
static volatile u16 s_hyn_x = 0;
static volatile u16 s_hyn_y = 0;
static u32 s_hyn_poll_count = 0;
static u8 s_hyn_frame_fail_streak = 0;
static volatile u8 s_hyn_needs_release;
static volatile u32 s_hyn_cancel_epoch;

static int hyn_write_reg(u32 reg, u8 reg_len);
static int hyn_read_reg(u32 reg, u8 reg_len, u8 *buf, u16 len);

/**
 * @brief 在短临界区内更新最近坐标、点数及有效接触电平。
 * @param[in] x 原生横坐标。
 * @param[in] y 原生纵坐标。
 * @param[in] points 本次报告的接触点数。
 * @param[in] down 有效按下电平。
 */
static void hyn_state_store(u16 x, u16 y, u8 points, u8 down)
{
    OS_ENTER_CRITICAL();
    s_hyn_x = x;
    s_hyn_y = y;
    s_hyn_points = points;
    s_hyn_down = down;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 在短临界区内复制基本触摸状态，NULL 输出单项跳过。
 * @param[out] x 可选横坐标输出。
 * @param[out] y 可选纵坐标输出。
 * @param[out] points 可选点数输出。
 * @param[out] down 可选电平输出。
 */
static void hyn_state_snapshot(u16 *x, u16 *y, u8 *points, u8 *down)
{
    OS_ENTER_CRITICAL();
    if (x != NULL) {
        *x = s_hyn_x;
    }
    if (y != NULL) {
        *y = s_hyn_y;
    }
    if (points != NULL) {
        *points = s_hyn_points;
    }
    if (down != NULL) {
        *down = s_hyn_down;
    }
    OS_EXIT_CRITICAL();
}

/**
 * @brief 在信号量已准备时通知读取任务，不在中断中读取 IIC。
 */
static void hyn_irq_cb(void)
{
    if (s_hyn_sem_ready) {
        os_sem_post(&s_hyn_sem);
    }
}

/**
 * @brief 按既有 HYN 硬件要求设置中断脚输入、上拉及数字输入。
 * @param[in] pin 板级提供的有效 GPIO 编号。
 */
static void hyn_prepare_int_pin(u32 pin)
{
    gpio_set_direction(pin, 1);
    gpio_set_pull_up(pin, 1);
    gpio_set_pull_down(pin, 0);
    gpio_set_die(pin, 1);
}

/**
 * @brief 执行既有触摸复位电平和延时，再恢复中断输入。
 * @param[in] pdata 已校验触摸引脚的板级描述符。
 * @note 仅初始化阶段调用；包含任务延时，不能从中断调用。
 */
static void hyn_reset_panel(const struct lcd_platform_data *pdata)
{
    gpio_set_pull_up(pdata->touch_reset_pin, 0);
    gpio_set_pull_down(pdata->touch_reset_pin, 0);
    gpio_set_die(pdata->touch_reset_pin, 1);
    gpio_direction_output(pdata->touch_reset_pin, 0);
    os_time_dly(1);
    gpio_direction_output(pdata->touch_reset_pin, 1);
    os_time_dly(5);
    hyn_prepare_int_pin(pdata->touch_int_pin);
}

/**
 * @brief 发送候选写命令值并检查 IIC 应答，结束时发送 STOP。
 * @param[in] wrcmd 候选表中的写命令字节，沿用既有地址表示。
 * @return 收到当前 SDK 接受的应答为 1，否则为 0。
 * @note 内部调用方须先打开本驱动 IIC 句柄。
 */
static int hyn_probe_addr(u8 wrcmd)
{
    int ack = 0;

    dev_ioctl(s_hyn_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_hyn_iic, IIC_IOCTL_TX_WITH_START_BIT, wrcmd) == 0) {
        ack = 1;
    }
    dev_ioctl(s_hyn_iic, IIC_IOCTL_STOP, 0);

    return ack;
}

/**
 * @brief 按既有探测规则排除全零及全 FF 数据块。
 * @param[in] buf 至少 len 字节的有效缓冲区。
 * @param[in] len 待检查长度。
 * @return 既非全零也非全 FF 为非零；空长度结果为零。
 * @note 这是可读数据的启发式检查，不单独证明芯片型号或每个字段合法。
 */
static int hyn_buf_has_signal(const u8 *buf, u16 len)
{
    u16 i;
    u8 all_zero = 1;
    u8 all_ff = 1;

    for (i = 0; i < len; i++) {
        if (buf[i] != 0x00) {
            all_zero = 0;
        }
        if (buf[i] != 0xff) {
            all_ff = 0;
        }
    }

    return (!all_zero && !all_ff);
}

/**
 * @brief 将命令值低位按所需长度排成高字节在前的字节序列。
 * @param[in] reg 命令或寄存器值。
 * @param[in] reg_len 输出字节数，由内部调用方保证容量。
 * @param[out] buf 至少 reg_len 字节的缓冲区。
 */
static void hyn_make_reg_buf(u32 reg, u8 reg_len, u8 *buf)
{
    u8 i = reg_len;

    while (i > 0) {
        i--;
        buf[i] = (u8)reg;
        reg >>= 8;
    }
}

/**
 * @brief 在当前 IIC 事务中提交最多三字节的命令序列。
 * @param[in] reg 待编码的命令值。
 * @param[in] reg_len 编码长度，不得超过局部三字节缓冲。
 * @param[in] stop 非零时最后一字节带 STOP。
 * @return 全部提交成功为 0；长度超限或任一 SDK 发送失败为 -1。
 * @note 调用方组织 START 及失败后的 STOP，不在此打开或关闭 IIC 设备。
 */
static int hyn_send_reg_bytes(u32 reg, u8 reg_len, u8 stop)
{
    u8 reg_buf[3] = {0};
    u8 i;

    if (reg_len > sizeof(reg_buf)) {
        return -1;
    }

    hyn_make_reg_buf(reg, reg_len, reg_buf);
    for (i = 0; i < reg_len; i++) {
        u32 cmd = (stop && (i == (reg_len - 1))) ? IIC_IOCTL_TX_WITH_STOP_BIT : IIC_IOCTL_TX;
        if (dev_ioctl(s_hyn_iic, cmd, reg_buf[i])) {
            return -1;
        }
        delay(100);
    }

    return 0;
}

/**
 * @brief 以已选写命令启动一次写事务，提交命令序列并结束总线事务。
 * @param[in] reg 待提交的命令值。
 * @param[in] reg_len 命令编码字节数。
 * @return 成功为 0；地址或命令发送失败为 -1。
 */
static int hyn_write_reg(u32 reg, u8 reg_len)
{
    int ret = 0;

    dev_ioctl(s_hyn_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_hyn_iic, IIC_IOCTL_TX_WITH_START_BIT, s_hyn_wrcmd)) {
        ret = -1;
        goto exit;
    }
    ret = hyn_send_reg_bytes(reg, reg_len, 1);

exit:
    dev_ioctl(s_hyn_iic, IIC_IOCTL_STOP, 0);
    return ret;
}

/**
 * @brief 先写寄存器/命令位置，再以已选读命令读取字节。
 * @param[in] reg 起始寄存器或命令值。
 * @param[in] reg_len 地址编码字节数。
 * @param[out] buf 至少 len 字节的输出，失败时可能已写部分。
 * @param[in] len 非零读取字节数。
 * @return 完整读取为 0；输出无效、零长度或任一总线操作失败为 -1。
 * @note 失败路径也发送 STOP；内部任务串行使用同一 IIC 句柄。
 */
static int hyn_read_reg(u32 reg, u8 reg_len, u8 *buf, u16 len)
{
    u16 i;
    int ret = 0;

    if ((buf == NULL) || (len == 0)) {
        return -1;
    }

    dev_ioctl(s_hyn_iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(s_hyn_iic, IIC_IOCTL_TX_WITH_START_BIT, s_hyn_wrcmd)) {
        ret = -1;
        goto exit;
    }
    if (hyn_send_reg_bytes(reg, reg_len, 0) != 0) {
        ret = -1;
        goto exit;
    }
    if (dev_ioctl(s_hyn_iic, IIC_IOCTL_TX_WITH_START_BIT, s_hyn_rdcmd)) {
        ret = -1;
        goto exit;
    }
    for (i = 0; i < len; i++) {
        u32 cmd = (i == (len - 1)) ? IIC_IOCTL_RX_WITH_STOP_BIT : IIC_IOCTL_RX_WITH_ACK;
        if (dev_ioctl(s_hyn_iic, cmd, (u32)&buf[i])) {
            ret = -1;
            goto exit;
        }
    }

exit:
    dev_ioctl(s_hyn_iic, IIC_IOCTL_STOP, 0);
    return ret;
}

/**
 * @brief 尝试读取已有信息及帧位置，按信号启发式核对候选设备。
 * @param[in] name 候选诊断名称，当前未使用。
 * @return 任一读取成功且不是全零/FF 时为 0，否则为 -1。
 */
static int hyn_validate_cst8xxt(const char *name)
{
    u8 info[4] = {0};
    u8 frame[HYN_CST8XXT_VALIDATE_LEN] = {0};
    int ret_info = hyn_read_reg(0xa7, 1, info, sizeof(info));
    int ret_frame = hyn_read_reg(0x00, 1, frame, sizeof(frame));
    int info_ok = (ret_info == 0) && hyn_buf_has_signal(info, sizeof(info));
    int frame_ok = (ret_frame == 0) && hyn_buf_has_signal(frame, sizeof(frame));

    (void)name;
    return (info_ok || frame_ok) ? 0 : -1;
}

/**
 * @brief 依次试探既有候选命令组，保存首个可读设备的读写命令。
 * @return 命中候选为 0；均未应答或未验证可读时为 -1。
 */
static int hyn_select_cst8xxt_addr(void)
{
    static const struct {
        u8 wrcmd;
        u8 rdcmd;
        const char *name;
    } candidates[] = {
        {0x2a, 0x2b, "cst8xxT"},
        {0x15, 0x16, "cst8xxT-raw7"},
    };
    u8 i;

    for (i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); i++) {
        int ack;

        s_hyn_wrcmd = candidates[i].wrcmd;
        s_hyn_rdcmd = candidates[i].rdcmd;
        ack = hyn_probe_addr(s_hyn_wrcmd);
        if (ack && (hyn_validate_cst8xxt(candidates[i].name) == 0)) {
            return 0;
        }
    }

    return -1;
}

/**
 * @brief 提交已有的 0x00 单字节命令进入正常采集流程。
 * @return 底层命令写入结果，0 成功、-1 失败。
 */
static int hyn_set_normal_mode(void)
{
    return hyn_write_reg(0x00, 1);
}

/**
 * @brief 读取固定长度触摸帧并把报告点数限制到本驱动上限。
 * @param[out] buf 至少 HYN_CST8XXT_FRAME_LEN 字节的缓冲区。
 * @param[out] rep_num 有效点数上限，成功时写入，失败不保证内容。
 * @return 读取成功为 0；空输出或总线失败为 -1。
 */
static int hyn_read_cst8xxt_frame(u8 *buf, u8 *rep_num)
{
    u8 raw_rep_num;

    if ((buf == NULL) || (rep_num == NULL)) {
        return -1;
    }
    if (hyn_read_reg(0x00, 1, buf, HYN_CST8XXT_FRAME_LEN) != 0) {
        return -1;
    }

    raw_rep_num = buf[2];
    if (raw_rep_num > HYN_CST8XXT_MAX_POINTS) {
        raw_rep_num = HYN_CST8XXT_MAX_POINTS;
    }
    *rep_num = raw_rep_num;
    return 0;
}

/**
 * @brief 计算原生坐标差并转为现有有符号 16 位表示。
 * @param[in] end 当前坐标。
 * @param[in] start 上一坐标。
 * @return end-start 的 s16 转换值。
 */
static s16 hyn_delta_u16(u16 end, u16 start)
{
    return (s16)((s32)end - (s32)start);
}

/**
 * @brief 将坐标限制为给定尺寸内的最后一个像素。
 * @param[in] value 输入坐标。
 * @param[in] limit 该方向的像素数。
 * @return limit 为零时为 0；否则为 min(value,limit-1)。
 */
static u16 hyn_clamp_coord(u16 value, u16 limit)
{
    if (limit == 0) {
        return 0;
    }
    return (value >= limit) ? (u16)(limit - 1) : value;
}

/**
 * @brief 按安装配置交换/反向并裁剪原生点，不应用产品运行方向。
 * @param[in] raw_x 控制器原始横坐标。
 * @param[in] raw_y 控制器原始纵坐标。
 * @param[out] out_x 有效的安装后横坐标输出指针。
 * @param[out] out_y 有效的安装后纵坐标输出指针。
 * @note 显示尺寸未登记时使用既有 240×320 默认域；方向翻转由 LVGL 和逻辑手势适配处理，不能在此重复旋转。
 */
static void hyn_transform_point(u16 raw_x, u16 raw_y, u16 *out_x, u16 *out_y)
{
    u16 x = raw_x;
    u16 y = raw_y;
    u16 width = lcd_w ? lcd_w : HYN_SCREEN_WIDTH;
    u16 height = lcd_h ? lcd_h : HYN_SCREEN_HEIGHT;

#if D_TOUCH_HYN_SWAP_XY
    u16 swap_tmp = x;
    x = y;
    y = swap_tmp;
#endif

    x = hyn_clamp_coord(x, width);
    y = hyn_clamp_coord(y, height);

#if D_TOUCH_HYN_REVERSE_X
    x = (u16)(width - 1 - x);
#endif
#if D_TOUCH_HYN_REVERSE_Y
    y = (u16)(height - 1 - y);
#endif

    *out_x = x;
    *out_y = y;
}

/**
 * @brief 从本次报告中选择首个驱动认可的有效接触，解析安装后主点。
 * @param[in] buf 完整帧缓冲区。
 * @param[in] rep_num 已限制到两点以内的报告数量。
 * @param[out] point 成功时写入主点，失败时不可据其内容更新输入。
 * @return 成功写入主点为 0；参数无效或无认可接触为 -1。
 */
static int hyn_parse_primary_point(const u8 *buf, u8 rep_num, d_touch_hyn_point_t *point)
{
    u8 i;

    if ((buf == NULL) || (point == NULL)) {
        return -1;
    }

    for (i = 0; i < rep_num; i++) {
        u8 index = 3 + i * 6;
        u8 id = (buf[index + 2] & 0xf0) >> 4;
        u8 event = (buf[index] & 0x40) ? 0 : 1;
        u16 raw_x;
        u16 raw_y;

        if ((id > 1) || !event) {
            continue;
        }

        raw_x = ((u16)(buf[index] & 0x0f) << 8) | buf[index + 1];
        raw_y = ((u16)(buf[index + 2] & 0x0f) << 8) | buf[index + 3];
        point->id = id;
        point->z = (u8)(3 + (raw_x & 0x03));
        hyn_transform_point(raw_x, raw_y, &point->x, &point->y);
        return 0;
    }

    return -1;
}

/**
 * @brief 在有效中性帧到来时，按上一坐标报告正常松手。
 * @param[in] now_ms 本帧读取时的毫秒计数。
 * @note 只有此前按下时才生成 UP；读取失败不调用本函数，不能把未知接触丢失转换成点击或返回手势。
 */
static void hyn_report_release(u32 now_ms)
{
    d_touch_hyn_point_t point;
    d_touch_gesture_sample_t sample;
    u16 state_x;
    u16 state_y;
    u8 state_down;

    hyn_state_snapshot(&state_x, &state_y, NULL, &state_down);
    if (!state_down) {
        hyn_state_store(state_x, state_y, 0, 0);
        return;
    }

    point.id = 0;
    point.z = 0;
    point.x = state_x;
    point.y = state_y;
    sample.point = point;
    sample.dx = 0;
    sample.dy = 0;
    sample.timestamp_ms = now_ms;
    sample.points = 0;
    d_touch_gesture_report_up(&sample);
    hyn_state_store(state_x, state_y, 0, 0);
}

/**
 * @brief 把有效主点转成新的按下或有变化的移动样本。
 * @param[in] point 解析成功的原生点。
 * @param[in] rep_num 本帧报告点数。
 * @param[in] now_ms 本帧毫秒计数。
 * @note 先登记基本状态，再通知手势层；未改变的坐标和点数不反复生成移动。
 */
static void hyn_report_point(const d_touch_hyn_point_t *point, u8 rep_num, u32 now_ms)
{
    d_touch_gesture_sample_t sample;
    u16 state_x;
    u16 state_y;
    u8 state_points;
    u8 state_down;

    hyn_state_snapshot(&state_x, &state_y, &state_points, &state_down);
    sample.point = *point;
    sample.dx = 0;
    sample.dy = 0;
    sample.timestamp_ms = now_ms;
    sample.points = rep_num;
    if (!state_down) {
        hyn_state_store(point->x, point->y, rep_num, 1);
        d_touch_gesture_report_down(&sample);
        return;
    }

    if ((point->x != state_x) || (point->y != state_y) || (rep_num != state_points)) {
        s16 dx = hyn_delta_u16(point->x, state_x);
        s16 dy = hyn_delta_u16(point->y, state_y);

        hyn_state_store(point->x, point->y, rep_num, 1);
        sample.dx = dx;
        sample.dy = dy;
        d_touch_gesture_report_move(&sample);
    }
}

/**
 * @brief 连续读取失败进入取消期时复位手势并推进一次取消代次。
 * @note 同一取消期只推进一次；清有效电平但保留坐标，必须等合法中性帧解除 needs_release，不产生正常 UP。
 */
static void hyn_cancel_input(void)
{
    if (s_hyn_needs_release) {
        return;
    }
    /* 无法确认的接触丢失属于取消，绝不合成为用户正常松手手势。 */
    d_touch_gesture_reset();
    OS_ENTER_CRITICAL();
    s_hyn_needs_release = 1U;
    s_hyn_cancel_epoch++;
    s_hyn_down = 0U;
    s_hyn_points = 0U;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 读取一帧并区分正常接触、正常松手和连续读取失败取消。
 * @note 失败计数达到现有阈值才取消；恢复后仍按住时持续抑制，合法中性帧只解除恢复门禁，本帧不补发旧输入。
 */
static void hyn_read_and_report_frame(void)
{
    u8 buf[HYN_CST8XXT_FRAME_LEN] = {0};
    u8 rep_num = 0;
    d_touch_hyn_point_t point;
    u32 now_ms = timer_get_ms();
    int has_point;

    s_hyn_poll_count++;
    if (hyn_read_cst8xxt_frame(buf, &rep_num) != 0) {
        if (s_hyn_frame_fail_streak < HYN_FRAME_FAIL_RELEASE_LIMIT) {
            s_hyn_frame_fail_streak++;
        }
        if ((s_hyn_poll_count == 1) || ((s_hyn_poll_count % HYN_IDLE_LOG_DIV) == 0)) {
            printf(HYN_TAG "frame read failed int=%d streak=%u\n",
                   gpio_read(s_hyn_int_pin),
                   (unsigned int)s_hyn_frame_fail_streak);
        }
        if (s_hyn_frame_fail_streak >= HYN_FRAME_FAIL_RELEASE_LIMIT) {
            hyn_cancel_input();
        }
        return;
    }
    s_hyn_frame_fail_streak = 0;

    has_point = rep_num != 0U && hyn_parse_primary_point(buf, rep_num, &point) == 0;
    if (s_hyn_needs_release) {
        if (!has_point) {
            OS_ENTER_CRITICAL();
            s_hyn_needs_release = 0U;
            OS_EXIT_CRITICAL();
        }
        return;
    }
    if (!has_point) {
        hyn_report_release(now_ms);
        return;
    }

    hyn_report_point(&point, rep_num, now_ms);
}

/**
 * @brief 由专属任务在中断通知或既有轮询周期到来时读取触摸。
 * @param[in] priv SDK 任务参数，当前未使用。
 * @note 常驻任务，不在中断执行总线读取或 LVGL 调用。
 */
static void hyn_touch_task(void *priv)
{
    (void)priv;

    while (1) {
        os_sem_pend(&s_hyn_sem, HYN_TOUCH_POLL_TICKS);
        hyn_read_and_report_frame();
    }
}

/**
 * @brief 在初始化失败、工作任务尚未建立时清理已取得的中断、信号量及 IIC。
 * @param[in] err 需要传播的初始化错误。
 * @return 原样返回传入错误。
 * @note 这是初始化补偿入口，不提供运行期任务停止或 IRQ 在途排空合同；不从另一线程销毁已运行任务的资源。
 */
static d_err_t hyn_cleanup(d_err_t err)
{
    if (s_hyn_wakeup_hdl != NULL) {
        port_wakeup_unreg(s_hyn_wakeup_hdl);
        s_hyn_wakeup_hdl = NULL;
    }
    if (s_hyn_sem_ready && (os_sem_valid(&s_hyn_sem) != 0)) {
        os_sem_del(&s_hyn_sem, OS_DEL_ALWAYS);
    }
    s_hyn_sem_ready = 0;
    if (s_hyn_iic != NULL) {
        dev_close(s_hyn_iic);
        s_hyn_iic = NULL;
    }
    d_touch_gesture_reset();
    s_hyn_ready = 0;
    s_hyn_poll_count = 0;
    s_hyn_frame_fail_streak = 0;
    hyn_state_store(0, 0, 0, 0);
    return err;
}

/**
 * @brief 启动 HYN 读取能力；完整合同见 d_touch_hyn.h。
 */
d_err_t d_touch_hyn_init(const struct lcd_platform_data *pdata)
{
    int ret;

    if (s_hyn_ready) {
        return D_OK;
    }
    if (pdata == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if ((pdata->touch_int_pin == (u32)-1) || (pdata->touch_reset_pin == (u32)-1)) {
        printf(HYN_TAG "invalid touch pins\n");
        return D_ERR_INVALID_ARG;
    }

    s_hyn_int_pin = pdata->touch_int_pin;
    s_hyn_poll_count = 0;
    s_hyn_frame_fail_streak = 0;
    printf(HYN_TAG "start driver iic=%s scl=PC1 sda=PC2 int=%u rst=%u reset=PC3\n",
           D_HYN_IIC_DEVICE_NAME, pdata->touch_int_pin, pdata->touch_reset_pin);
    hyn_prepare_int_pin(pdata->touch_int_pin);
    hyn_reset_panel(pdata);

    s_hyn_iic = dev_open(D_HYN_IIC_DEVICE_NAME, NULL);
    if (s_hyn_iic == NULL) {
        printf(HYN_TAG "open %s failed\n", D_HYN_IIC_DEVICE_NAME);
        return D_ERR_IO;
    }

    if (hyn_select_cst8xxt_addr() != 0) {
        printf(HYN_TAG "no CST8xxT ack\n");
        return hyn_cleanup(D_ERR_NOT_FOUND);
    }

    if (hyn_set_normal_mode() != 0) {
        return hyn_cleanup(D_ERR_IO);
    }

    if (os_sem_create(&s_hyn_sem, 0) != OS_NO_ERR) {
        printf(HYN_TAG "sem create failed\n");
        return hyn_cleanup(D_ERR_IO);
    }
    s_hyn_sem_ready = 1;

    s_hyn_wakeup_hdl = port_wakeup_reg(EVENT_IO_0, pdata->touch_int_pin, EDGE_NEGATIVE, hyn_irq_cb);
    if (s_hyn_wakeup_hdl == NULL) {
        printf(HYN_TAG "irq register failed, polling only\n");
    }

    ret = thread_fork("hyn_touch", 28, 1024, 0, NULL, hyn_touch_task, NULL);
    if (ret != OS_NO_ERR) {
        printf(HYN_TAG "task create failed: %d\n", ret);
        return hyn_cleanup(D_ERR_IO);
    }

    s_hyn_ready = 1;
    printf(HYN_TAG "driver ready cst8xxT max_points=%u gesture=%u swap=%u rx=%u ry=%u\n",
           HYN_CST8XXT_MAX_POINTS,
#ifdef D_TOUCH_GESTURE_ENABLE
           1,
#else
           0,
#endif
           (unsigned int)D_TOUCH_HYN_SWAP_XY,
           (unsigned int)D_TOUCH_HYN_REVERSE_X,
           (unsigned int)D_TOUCH_HYN_REVERSE_Y
          );
    return D_OK;
}

/**
 * @brief 复制完整质量快照；完整合同见 d_touch_hyn.h。
 */
void d_touch_hyn_get_snapshot(d_touch_hyn_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    OS_ENTER_CRITICAL();
    snapshot->x = s_hyn_x;
    snapshot->y = s_hyn_y;
    snapshot->pressed = s_hyn_down;
    snapshot->needs_release = s_hyn_needs_release;
    snapshot->cancel_epoch = s_hyn_cancel_epoch;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 提供兼容原生状态视图；完整合同见 d_touch_hyn.h。
 */
void d_touch_hyn_get_state(s16 *x, s16 *y, unsigned char *status)
{
    d_touch_hyn_snapshot_t snapshot;
    d_touch_hyn_get_snapshot(&snapshot);
    if (x != NULL) {
        *x = (s16)snapshot.x;
    }
    if (y != NULL) {
        *y = (s16)snapshot.y;
    }
    if (status != NULL) {
        *status = snapshot.pressed ? 1 : 0;
    }
}

#endif /* D_LCD_ENABLE && D_TOUCH_HYN_DRIVER_ENABLE */
