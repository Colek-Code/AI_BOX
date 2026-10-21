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

static int hyn_write_reg(u32 reg, u8 reg_len);
static int hyn_read_reg(u32 reg, u8 reg_len, u8 *buf, u16 len);

static void hyn_state_store(u16 x, u16 y, u8 points, u8 down)
{
    OS_ENTER_CRITICAL();
    s_hyn_x = x;
    s_hyn_y = y;
    s_hyn_points = points;
    s_hyn_down = down;
    OS_EXIT_CRITICAL();
}

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

static void hyn_irq_cb(void)
{
    if (s_hyn_sem_ready) {
        os_sem_post(&s_hyn_sem);
    }
}

static void hyn_prepare_int_pin(u32 pin)
{
    gpio_set_direction(pin, 1);
    gpio_set_pull_up(pin, 1);
    gpio_set_pull_down(pin, 0);
    gpio_set_die(pin, 1);
}

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

static void hyn_make_reg_buf(u32 reg, u8 reg_len, u8 *buf)
{
    u8 i = reg_len;

    while (i > 0) {
        i--;
        buf[i] = (u8)reg;
        reg >>= 8;
    }
}

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

static int hyn_set_normal_mode(void)
{
    return hyn_write_reg(0x00, 1);
}

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

static s16 hyn_delta_u16(u16 end, u16 start)
{
    return (s16)((s32)end - (s32)start);
}

static u16 hyn_clamp_coord(u16 value, u16 limit)
{
    if (limit == 0) {
        return 0;
    }
    return (value >= limit) ? (u16)(limit - 1) : value;
}

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

static void hyn_read_and_report_frame(void)
{
    u8 buf[HYN_CST8XXT_FRAME_LEN] = {0};
    u8 rep_num = 0;
    d_touch_hyn_point_t point;
    u32 now_ms = timer_get_ms();

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
            hyn_report_release(now_ms);
        }
        return;
    }
    s_hyn_frame_fail_streak = 0;

    if ((rep_num == 0) || (hyn_parse_primary_point(buf, rep_num, &point) != 0)) {
        hyn_report_release(now_ms);
        return;
    }

    hyn_report_point(&point, rep_num, now_ms);
}

static void hyn_touch_task(void *priv)
{
    (void)priv;

    while (1) {
        os_sem_pend(&s_hyn_sem, HYN_TOUCH_POLL_TICKS);
        hyn_read_and_report_frame();
    }
}

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

void d_touch_hyn_get_state(s16 *x, s16 *y, unsigned char *status)
{
    u16 state_x;
    u16 state_y;
    u8 state_down;

    hyn_state_snapshot(&state_x, &state_y, NULL, &state_down);
    if (x != NULL) {
        *x = (s16)state_x;
    }
    if (y != NULL) {
        *y = (s16)state_y;
    }
    if (status != NULL) {
        *status = state_down ? 1 : 0;
    }
}

#endif /* D_LCD_ENABLE && D_TOUCH_HYN_DRIVER_ENABLE */
