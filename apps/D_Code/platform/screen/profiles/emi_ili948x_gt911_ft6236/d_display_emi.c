/**
 * @file    d_display_emi.c
 * @brief   EMI LCD 显示适配器（平台层）
 *          不含 UI 框架逻辑，只负责 LCD 硬件初始化和背光控制
 */

#include "config/app_config.h"
#include "platform/screen/d_display.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "asm/gpio.h"
#include "asm/pwm.h"

#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)

#include "platform/screen/lcd/d_lcd_compat.h"
#include "platform/screen/profiles/emi_ili948x_gt911_ft6236/d_touch.h"
#include "platform/screen/power/d_screen_power.h"
#include "platform/pwm/d_pwm.h"
#include "framework/d_log.h"

#define TAG "D_DISPLAY_EMI"
#define BSP_LCD_BACKLIGHT_PWM_DEV       "pwm_lcd_bl"
#define BSP_LCD_BRIGHTNESS_DEFAULT      80
#define BSP_LCD_BRIGHTNESS_MIN_VISIBLE  5
#define BSP_LCD_PWM_FREQ_HZ             20000
/* #define BSP_LCD_PWM_VERIFY_LOG_ENABLE */ /* 调试 LCD 背光 PWM 时临时打开；背光低有效：80%亮度=duty20，100%亮度=duty0。 */
#define BSP_LCD_PWM_VERIFY_LOG_STEP     10

static u16 lcd_w = 0;
static u16 lcd_h = 0;
static u8 lcd_bk_on = 1;
static u8 lcd_bk_off = 0;
static u8 s_lcd_brightness = BSP_LCD_BRIGHTNESS_DEFAULT;
static u8 s_lcd_last_nonzero_brightness = BSP_LCD_BRIGHTNESS_DEFAULT;

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
static void *s_lcd_bl_pwm_handle = NULL;
static struct pwm_platform_data s_lcd_bl_pwm_cfg = {0};
#ifdef BSP_LCD_PWM_VERIFY_LOG_ENABLE
static u8 s_lcd_bl_pwm_log_count = 0;
static u8 s_lcd_bl_pwm_last_log_brightness = 0xff;
#endif
#endif

extern const struct lcd_platform_data *d_get_lcd_pdata(void);
extern int d_lcd_init(const struct lcd_platform_data *pdata);
extern void d_lcd_fill_color(u16 color);

/**
 * @brief 获取 LCD 宽度
 */
uint16_t d_display_get_width(void)
{
    return lcd_w;
}

/**
 * @brief 获取 LCD 高度
 */
uint16_t d_display_get_height(void)
{
    return lcd_h;
}

static u8 d_display_emi_clamp_brightness(u8 percent)
{
    if (percent > 100) {
        return 100;
    }

    if ((percent > 0) && (percent < BSP_LCD_BRIGHTNESS_MIN_VISIBLE)) {
        return BSP_LCD_BRIGHTNESS_MIN_VISIBLE;
    }

    return percent;
}

static u8 d_display_emi_brightness_to_pwm_duty(u8 percent)
{
#ifdef D_LCD_BL_ACTIVE_LOW
    return 100 - percent;
#else
    return percent;
#endif
}

static void d_display_emi_set_gpio_backlight(u8 on)
{
    const struct lcd_platform_data *pdata = d_get_lcd_pdata();

    if (pdata && pdata->bl_pin != (u32)-1) {
        gpio_direction_output(pdata->bl_pin, on ? lcd_bk_on : lcd_bk_off);
    }
}

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
static void d_display_emi_pwm_log_set_result(u8 brightness, u8 duty, d_err_t err)
{
#ifdef BSP_LCD_PWM_VERIFY_LOG_ENABLE
    u8 delta;

    if (err != D_OK) {
        LOGW(TAG, "BL PWM set fail: brightness=%u duty=%u err=%d",
             brightness, duty, err);
        return;
    }

    if (s_lcd_bl_pwm_last_log_brightness == 0xff) {
        LOGI(TAG, "BL PWM set: brightness=%u duty=%u", brightness, duty);
        s_lcd_bl_pwm_last_log_brightness = brightness;
        s_lcd_bl_pwm_log_count++;
        return;
    }

    if (brightness > s_lcd_bl_pwm_last_log_brightness) {
        delta = brightness - s_lcd_bl_pwm_last_log_brightness;
    } else {
        delta = s_lcd_bl_pwm_last_log_brightness - brightness;
    }

    if ((brightness == BSP_LCD_BRIGHTNESS_MIN_VISIBLE) ||
        (brightness == 100) ||
        (delta >= BSP_LCD_PWM_VERIFY_LOG_STEP)) {
        LOGI(TAG, "BL PWM set: brightness=%u duty=%u", brightness, duty);
        s_lcd_bl_pwm_last_log_brightness = brightness;
        s_lcd_bl_pwm_log_count++;
    }
#else
    (void)brightness;
    (void)duty;
    (void)err;
#endif
}

static d_err_t d_display_emi_pwm_ensure_open(void)
{
    d_err_t err;
    const struct lcd_platform_data *pdata;

    if (s_lcd_bl_pwm_handle != NULL) {
        return D_OK;
    }

    err = d_pwm_open(BSP_LCD_BACKLIGHT_PWM_DEV, &s_lcd_bl_pwm_cfg, &s_lcd_bl_pwm_handle);
    if (err != D_OK) {
        return err;
    }

    pdata = d_get_lcd_pdata();
    if ((s_lcd_bl_pwm_cfg.port == 0) && (pdata != NULL) && (pdata->bl_pin != (u32)-1)) {
        s_lcd_bl_pwm_cfg.port = pdata->bl_pin;
    }
    if (s_lcd_bl_pwm_cfg.pwm_ch == 0) {
        s_lcd_bl_pwm_cfg.pwm_ch = PWM_TIMER2_OPCH2;
    }
    if (s_lcd_bl_pwm_cfg.freq == 0) {
        s_lcd_bl_pwm_cfg.freq = BSP_LCD_PWM_FREQ_HZ;
    }
    if (s_lcd_bl_pwm_cfg.point_bit == 0) {
        s_lcd_bl_pwm_cfg.point_bit = 1;
    }

    err = d_pwm_run(s_lcd_bl_pwm_handle, &s_lcd_bl_pwm_cfg);
    if (err != D_OK) {
#ifdef BSP_LCD_PWM_VERIFY_LOG_ENABLE
        LOGW(TAG, "BL PWM run fail: dev=%s err=%d", BSP_LCD_BACKLIGHT_PWM_DEV, err);
#endif
        d_pwm_close(s_lcd_bl_pwm_handle);
        s_lcd_bl_pwm_handle = NULL;
    } else {
#ifdef BSP_LCD_PWM_VERIFY_LOG_ENABLE
        LOGI(TAG, "BL PWM open ok: dev=%s port=%u ch=0x%x freq=%u duty=%d point=%u",
             BSP_LCD_BACKLIGHT_PWM_DEV,
             s_lcd_bl_pwm_cfg.port,
             s_lcd_bl_pwm_cfg.pwm_ch,
             s_lcd_bl_pwm_cfg.freq,
             (int)s_lcd_bl_pwm_cfg.duty,
             s_lcd_bl_pwm_cfg.point_bit);
#endif
    }

    return err;
}

static d_err_t d_display_emi_pwm_set_brightness(u8 percent)
{
    d_err_t err;
    u8 duty;

    err = d_display_emi_pwm_ensure_open();
    if (err != D_OK) {
#ifdef BSP_LCD_PWM_VERIFY_LOG_ENABLE
        LOGW(TAG, "BL PWM open fail: dev=%s brightness=%u err=%d",
             BSP_LCD_BACKLIGHT_PWM_DEV, percent, err);
#endif
        return err;
    }

    duty = d_display_emi_brightness_to_pwm_duty(percent);
    err = d_pwm_set_duty(s_lcd_bl_pwm_handle, &s_lcd_bl_pwm_cfg, duty);
    d_display_emi_pwm_log_set_result(percent, duty, err);

    return err;
}
#endif

d_err_t d_display_set_brightness(uint8_t percent)
{
    u8 brightness = d_display_emi_clamp_brightness(percent);

    if ((brightness > 0U) && (d_screen_power_is_off() != 0U)) {
        return D_ERR_BUSY;
    }

    s_lcd_brightness = brightness;
    if (brightness > 0) {
        s_lcd_last_nonzero_brightness = brightness;
    }

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
    d_err_t err;

    err = d_display_emi_pwm_set_brightness(brightness);
    if (err == D_OK) {
        return D_OK;
    }

    d_display_emi_set_gpio_backlight(brightness > 0);
    return err;
#else
    d_display_emi_set_gpio_backlight(brightness > 0);
    return D_OK;
#endif
}

uint8_t d_display_get_brightness(void)
{
    return s_lcd_brightness;
}


/**
 * @brief 背光控制
 */
d_err_t d_display_set_backlight(uint8_t on)
{
    if (on) {
        if (s_lcd_last_nonzero_brightness == 0) {
            s_lcd_last_nonzero_brightness = BSP_LCD_BRIGHTNESS_DEFAULT;
        }
        return d_display_set_brightness(s_lcd_last_nonzero_brightness);
    }
    return d_display_set_brightness(0);
}


/**
 * @brief LCD + 触摸初始化任务
 */
static void d_display_emi_task(void *p)
{
    d_err_t touch_err;
    const struct lcd_platform_data *pdata = d_get_lcd_pdata();
    if (!pdata) {
        LOGE(TAG, "Platform data NULL!");
        return;
    }

    LOGI(TAG, "GPIO: rst=%d rs=%d bl=%d te=%d t_int=%d t_rst=%d",
           pdata->rst_pin, pdata->rs_pin, pdata->bl_pin, pdata->te_pin,
           pdata->touch_int_pin, pdata->touch_reset_pin);

    /* 先关闭背光 */
    if (pdata->bl_pin != (u32)-1) {
#ifdef D_LCD_BL_ACTIVE_LOW
        lcd_bk_on = 0;
        lcd_bk_off = 1;
#else
        lcd_bk_on = 1;
        lcd_bk_off = 0;
#endif
        gpio_direction_output(pdata->bl_pin, lcd_bk_off);
    }

    /* 复位脉冲 */
    if (pdata->rst_pin != (u32)-1) {
        gpio_direction_output(pdata->rst_pin, 0);
        os_time_dly(5);
        gpio_direction_output(pdata->rst_pin, 1);
        os_time_dly(15);
    }

    /* LCD 初始化 */
    LOGI(TAG, "Initializing LCD...");
    if (d_lcd_init(pdata) != 0) {
        LOGE(TAG, "LCD init failed!");
        return;
    }

    extern u16 d_lcd_get_width(void);
    extern u16 d_lcd_get_height(void);
    lcd_w = d_lcd_get_width();
    lcd_h = d_lcd_get_height();
    LOGI(TAG, "LCD %dx%d ready", lcd_w, lcd_h);

    /* 触摸初始化 */
    touch_err = d_touch_init(pdata);
    if (touch_err != D_OK) {
        LOGE(TAG, "Touch init failed: %d", touch_err);
    }

    /* 清屏 + 开背光 */
    d_lcd_fill_color(0x0000);
    d_display_set_backlight(1);
    LOGI(TAG, "Display ready");
}

/**
 * @brief BSP LCD 初始化入口
 */
d_err_t d_display_init(void)
{
    int ret;

    LOGI(TAG, "Creating display task...");
    ret = thread_fork("d_display_emi", 10, 2048, 0, 0, d_display_emi_task, NULL);
    if (ret != OS_NO_ERR) {
        LOGE(TAG, "display task create failed: %d", ret);
        return D_ERR_IO;
    }

    return D_OK;
}


#endif /* D_LCD_ENABLE && D_LCD_EMI_ILI948X_ENABLE */
