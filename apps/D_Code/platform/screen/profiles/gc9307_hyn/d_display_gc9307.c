/**
 * @file d_display_gc9307.c
 * @brief GC9307 display adapter: init task and PWM backlight brightness.
 */

#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

#include "platform/screen/d_display.h"
#include "platform/screen/profiles/gc9307_hyn/d_screen_gc9307_hyn.h"
#include "platform/pwm/d_pwm.h"
#include "platform/screen/power/d_screen_power.h"
#include "framework/d_log.h"
#include "platform/screen/lcd/d_lcd_compat.h"
#include "os/os_api.h"
#include "asm/gpio.h"
#include "asm/pwm.h"

#define TAG "BSP_LCD_GC9307"
#define BSP_LCD_GC9307_INIT_TIMEOUT_TICKS 500
#define BSP_LCD_BACKLIGHT_PWM_DEV         "pwm_lcd_bl"
#define BSP_LCD_BRIGHTNESS_DEFAULT        80
/* Keep in sync with PRODUCT_UI_BRIGHTNESS_MIN_PERCENT (settings service). */
#define BSP_LCD_BRIGHTNESS_MIN_VISIBLE    5
#define BSP_LCD_PWM_FREQ_HZ               20000
/* #define BSP_LCD_GC9307_PWM_VERIFY_LOG_ENABLE */

static u16 s_lcd_width = 0;
static u16 s_lcd_height = 0;
static u8 s_lcd_ready = 0;
static u8 s_lcd_init_done = 0;
static OS_SEM s_lcd_init_sem;
static d_err_t s_lcd_init_result = D_ERR_NOT_READY;

static u8 s_lcd_brightness = BSP_LCD_BRIGHTNESS_DEFAULT;
static u8 s_lcd_last_nonzero_brightness = BSP_LCD_BRIGHTNESS_DEFAULT;
static OS_MUTEX s_bl_mutex;
static u8 s_bl_mutex_ready;

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
static void *s_lcd_bl_pwm_handle = NULL;
static struct pwm_platform_data s_lcd_bl_pwm_cfg = {0};
#endif

extern const struct lcd_platform_data *d_get_lcd_pdata(void);

static void d_display_gc9307_bl_mutex_ensure(void)
{
    if (s_bl_mutex_ready) {
        return;
    }
    if (os_mutex_create(&s_bl_mutex) == OS_NO_ERR) {
        s_bl_mutex_ready = 1U;
    }
}

static void d_display_gc9307_bl_lock(void)
{
    d_display_gc9307_bl_mutex_ensure();
    if (s_bl_mutex_ready) {
        (void)os_mutex_pend(&s_bl_mutex, 0);
    }
}

static void d_display_gc9307_bl_unlock(void)
{
    if (s_bl_mutex_ready) {
        (void)os_mutex_post(&s_bl_mutex);
    }
}

uint16_t d_display_get_width(void)
{
    return s_lcd_width;
}

uint16_t d_display_get_height(void)
{
    return s_lcd_height;
}

static u8 d_display_gc9307_clamp_brightness(u8 percent)
{
    if (percent > 100U) {
        return 100U;
    }
    if ((percent > 0U) && (percent < BSP_LCD_BRIGHTNESS_MIN_VISIBLE)) {
        return BSP_LCD_BRIGHTNESS_MIN_VISIBLE;
    }
    return percent;
}

static u8 d_display_gc9307_brightness_to_pwm_duty(u8 percent)
{
#ifdef D_LCD_BL_ACTIVE_LOW
    return (u8)(100U - percent);
#else
    /* GC9307 PC4 active-high: duty tracks brightness percent. */
    return percent;
#endif
}

static void d_display_gc9307_set_gpio_backlight(u8 on)
{
    const struct lcd_platform_data *pdata = d_get_lcd_pdata();

    if ((pdata != NULL) && (pdata->bl_pin != (u32)-1)) {
        gpio_direction_output(pdata->bl_pin, on ? 1 : 0);
    }
}

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
static void d_display_gc9307_pwm_release(void)
{
    if (s_lcd_bl_pwm_handle != NULL) {
        d_pwm_close(s_lcd_bl_pwm_handle);
        s_lcd_bl_pwm_handle = NULL;
    }
}

static d_err_t d_display_gc9307_pwm_ensure_open(void)
{
    d_err_t err;
    const struct lcd_platform_data *pdata;

    if (s_lcd_bl_pwm_handle != NULL) {
        return D_OK;
    }

    err = d_pwm_open(BSP_LCD_BACKLIGHT_PWM_DEV, &s_lcd_bl_pwm_cfg,
                     &s_lcd_bl_pwm_handle);
    if (err != D_OK) {
        return err;
    }

    pdata = d_get_lcd_pdata();
    if ((s_lcd_bl_pwm_cfg.port == 0U) && (pdata != NULL) &&
        (pdata->bl_pin != (u32)-1)) {
        s_lcd_bl_pwm_cfg.port = pdata->bl_pin;
    }
    if (s_lcd_bl_pwm_cfg.pwm_ch == 0U) {
        s_lcd_bl_pwm_cfg.pwm_ch = PWM_TIMER2_OPCH2;
    }
    if (s_lcd_bl_pwm_cfg.freq == 0U) {
        s_lcd_bl_pwm_cfg.freq = BSP_LCD_PWM_FREQ_HZ;
    }
    if (s_lcd_bl_pwm_cfg.point_bit == 0U) {
        s_lcd_bl_pwm_cfg.point_bit = 1;
    }

    err = d_pwm_run(s_lcd_bl_pwm_handle, &s_lcd_bl_pwm_cfg);
    if (err != D_OK) {
#ifdef BSP_LCD_GC9307_PWM_VERIFY_LOG_ENABLE
        LOGW(TAG, "BL PWM run fail: err=%d", err);
#endif
        d_display_gc9307_pwm_release();
        return err;
    }

#ifdef BSP_LCD_GC9307_PWM_VERIFY_LOG_ENABLE
    LOGI(TAG, "BL PWM open ok: port=%u ch=0x%x freq=%u",
         s_lcd_bl_pwm_cfg.port, s_lcd_bl_pwm_cfg.pwm_ch,
         s_lcd_bl_pwm_cfg.freq);
#endif
    return D_OK;
}

static d_err_t d_display_gc9307_pwm_set_brightness(u8 percent)
{
    d_err_t err;
    u8 duty;

    err = d_display_gc9307_pwm_ensure_open();
    if (err != D_OK) {
#ifdef BSP_LCD_GC9307_PWM_VERIFY_LOG_ENABLE
        LOGW(TAG, "BL PWM open fail: brightness=%u err=%d", percent, err);
#endif
        return err;
    }

    duty = d_display_gc9307_brightness_to_pwm_duty(percent);
    err = d_pwm_set_duty(s_lcd_bl_pwm_handle, &s_lcd_bl_pwm_cfg, duty);
#ifdef BSP_LCD_GC9307_PWM_VERIFY_LOG_ENABLE
    if (err != D_OK) {
        LOGW(TAG, "BL PWM set fail: brightness=%u duty=%u err=%d",
             percent, duty, err);
    } else {
        LOGI(TAG, "BL PWM set: brightness=%u duty=%u", percent, duty);
    }
#endif
    return err;
}
#endif /* D_LCD_BACKLIGHT_PWM_ENABLE */

/*
 * Apply brightness with lock held. Caller must hold bl mutex.
 * Continuous % is committed only after PWM success. Off may degrade to GPIO.
 */
static d_err_t d_display_gc9307_apply_brightness_locked(u8 brightness)
{
    d_err_t err = D_OK;

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
    err = d_display_gc9307_pwm_set_brightness(brightness);
    if (err != D_OK) {
        if (brightness == 0U) {
            /*
             * Off: release PWM then digital GPIO low so pin is not left in
             * PWM mode while claiming off. SW 0 is product-correct.
             */
            d_display_gc9307_pwm_release();
            d_display_gc9307_set_gpio_backlight(0U);
            s_lcd_brightness = 0U;
            return D_OK;
        }
        /*
         * Non-zero fail: keep handle if open so last successful duty stays on
         * HW. open/run fail already leaves handle NULL (release in ensure).
         * Do not GPIO-steal or claim a new continuous %.
         */
        return err;
    }
#else
    /* Digital-only path: software % is on/off truth (0 or last request). */
    d_display_gc9307_set_gpio_backlight(brightness > 0U ? 1U : 0U);
    err = D_OK;
#endif

    s_lcd_brightness = brightness;
    if (brightness > 0U) {
        s_lcd_last_nonzero_brightness = brightness;
    }
    return err;
}

d_err_t d_display_set_brightness(uint8_t percent)
{
    u8 brightness = d_display_gc9307_clamp_brightness(percent);
    d_err_t err;

    /*
     * While screen power is off, reject non-zero apply so UI cannot light HW
     * without clearing the off state. Wake/clear_off clears screen_off first.
     */
    if ((brightness > 0U) && (d_screen_power_is_off() != 0U)) {
        return D_ERR_BUSY;
    }

    d_display_gc9307_bl_lock();
    err = d_display_gc9307_apply_brightness_locked(brightness);
    d_display_gc9307_bl_unlock();
    return err;
}

uint8_t d_display_get_brightness(void)
{
    u8 value;

    d_display_gc9307_bl_lock();
    value = s_lcd_brightness;
    d_display_gc9307_bl_unlock();
    return value;
}

d_err_t d_display_set_backlight(uint8_t on)
{
    u8 target;
    d_err_t err;

    /*
     * While screen power is off, reject non-zero apply.
     * Backlight-on is only legal after clear_off/wake.
     */
    if (on && (d_screen_power_is_off() != 0U)) {
        return D_ERR_BUSY;
    }

    d_display_gc9307_bl_lock();
    if (on) {
        target = s_lcd_last_nonzero_brightness;
        if (target == 0U) {
            target = BSP_LCD_BRIGHTNESS_DEFAULT;
            s_lcd_last_nonzero_brightness = target;
        }
        err = d_display_gc9307_apply_brightness_locked(target);
    } else {
        err = d_display_gc9307_apply_brightness_locked(0U);
    }
    d_display_gc9307_bl_unlock();
    return err;
}

static void d_display_gc9307_signal_init(d_err_t result)
{
    s_lcd_init_result = result;
    s_lcd_init_done = 1;
    os_sem_post(&s_lcd_init_sem);
}

static void d_display_gc9307_task(void *priv)
{
    const struct lcd_platform_data *pdata;
    d_err_t result;

    (void)priv;

    pdata = d_get_lcd_pdata();
    if (pdata == NULL) {
        LOGE(TAG, "platform data missing");
        d_display_gc9307_signal_init(D_ERR_INVALID_ARG);
        return;
    }

    result = d_screen_gc9307_hyn_init(pdata, &s_lcd_width, &s_lcd_height);
    if (result == D_OK) {
        LOGI(TAG, "ready %ux%u", s_lcd_width, s_lcd_height);
        /* Apply default PWM brightness after panel init. */
        result = d_display_set_brightness(BSP_LCD_BRIGHTNESS_DEFAULT);
        if (result != D_OK) {
            LOGE(TAG, "backlight init failed: %d", result);
            d_display_gc9307_signal_init(result);
            d_screen_gc9307_hyn_idle();
            return;
        }
        s_lcd_ready = 1;
        d_display_gc9307_signal_init(D_OK);
        d_screen_gc9307_hyn_idle();
        return;
    }

    LOGE(TAG, "init failed: %d", result);
    d_display_gc9307_signal_init(result);
}

d_err_t d_display_init(void)
{
    int ret;
    int wait_ret;

    if (s_lcd_ready) {
        return D_OK;
    }
    if (s_lcd_init_done) {
        return s_lcd_init_result;
    }

    /* Create BL mutex before worker/task races set_brightness. */
    d_display_gc9307_bl_mutex_ensure();

    if (os_sem_create(&s_lcd_init_sem, 0) != OS_NO_ERR) {
        LOGE(TAG, "init sem create failed");
        return D_ERR_IO;
    }

    ret = thread_fork("d_display_gc9307", 10, 2048, 0, NULL, d_display_gc9307_task,
                      NULL);
    if (ret != OS_NO_ERR) {
        LOGE(TAG, "task create failed: %d", ret);
        os_sem_del(&s_lcd_init_sem, OS_DEL_ALWAYS);
        return D_ERR_IO;
    }

    wait_ret = os_sem_pend(&s_lcd_init_sem, BSP_LCD_GC9307_INIT_TIMEOUT_TICKS);
    if (wait_ret == OS_TIMEOUT) {
        LOGE(TAG, "init timeout");
        s_lcd_init_result = D_ERR_TIMEOUT;
        s_lcd_init_done = 1;
        return D_ERR_TIMEOUT;
    }

    return s_lcd_init_result;
}

#endif /* D_LCD_ENABLE && D_BOARD_IS_GC9307 */
