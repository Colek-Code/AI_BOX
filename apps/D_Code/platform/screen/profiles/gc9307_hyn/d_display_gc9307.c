/**
 * @file d_display_gc9307.c
 * @brief 实现 GC9307 显示启动任务及 PWM 背光适配，不承载产品页面或偏好策略。
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
/* 与设置服务 PRODUCT_UI_BRIGHTNESS_MIN_PERCENT 的可见下限保持一致。 */
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

/**
 * @brief 按需创建背光互斥锁，创建失败时保留未就绪标志。
 * @note 启动层先调用以减少初始化竞争；本函数本身不提供并发首次创建保护，也不向调用方报告创建失败。
 */
static void d_display_gc9307_bl_mutex_ensure(void)
{
    if (s_bl_mutex_ready) {
        return;
    }
    if (os_mutex_create(&s_bl_mutex) == OS_NO_ERR) {
        s_bl_mutex_ready = 1U;
    }
}

/**
 * @brief 尝试准备背光锁，存在时无限期等待取得。
 * @note 锁创建失败时继续原路径，不可据此宣称所有配置下均有互斥保证。
 */
static void d_display_gc9307_bl_lock(void)
{
    d_display_gc9307_bl_mutex_ensure();
    if (s_bl_mutex_ready) {
        (void)os_mutex_pend(&s_bl_mutex, 0);
    }
}

/**
 * @brief 仅在背光锁创建成功时归还锁。
 */
static void d_display_gc9307_bl_unlock(void)
{
    if (s_bl_mutex_ready) {
        (void)os_mutex_post(&s_bl_mutex);
    }
}

/**
 * @brief 读取面板宽度；完整合同见 d_display.h。
 */
uint16_t d_display_get_width(void)
{
    return s_lcd_width;
}

/**
 * @brief 读取面板高度；完整合同见 d_display.h。
 */
uint16_t d_display_get_height(void)
{
    return s_lcd_height;
}

/**
 * @brief 将非零背光请求限制在可见下限与百分比上限之间。
 * @param[in] percent 请求的无符号百分比。
 * @return 零仍为关闭；1至4变为5；超过100变为100，其余保留。
 */
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

/**
 * @brief 按板级有效电平把亮度换算为 PWM 占空比。
 * @param[in] percent 已经限制为 0 至 100 的亮度。
 * @return 低有效时为 100-percent，高有效时等于 percent。
 */
static u8 d_display_gc9307_brightness_to_pwm_duty(u8 percent)
{
#ifdef D_LCD_BL_ACTIVE_LOW
    return (u8)(100U - percent);
#else
    /* GC9307 PC4 高电平有效，占空比随亮度百分比变化。 */
    return percent;
#endif
}

/**
 * @brief 用板级背光引脚执行数字开关，缺少有效引脚时不处理。
 * @param[in] on 非零输出高电平，零输出低电平。
 */
static void d_display_gc9307_set_gpio_backlight(u8 on)
{
    const struct lcd_platform_data *pdata = d_get_lcd_pdata();

    if ((pdata != NULL) && (pdata->bl_pin != (u32)-1)) {
        gpio_direction_output(pdata->bl_pin, on ? 1 : 0);
    }
}

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
/**
 * @brief 关闭并清空本适配持有的 PWM 句柄。
 * @note 调用方负责背光串行化；本函数不作为底层关闭完成可查询票据。
 */
static void d_display_gc9307_pwm_release(void)
{
    if (s_lcd_bl_pwm_handle != NULL) {
        d_pwm_close(s_lcd_bl_pwm_handle);
        s_lcd_bl_pwm_handle = NULL;
    }
}

/**
 * @brief 按需打开 PWM，补齐板级默认参数并启动通道。
 * @return D_OK 为已有或新启动成功；否则为底层打开/启动错误。
 * @note 启动失败关闭并清空本适配句柄，不占用别人的 PWM 句柄。
 */
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

/**
 * @brief 准备 PWM 后写入换算后的占空比。
 * @param[in] percent 已限制为 0 至 100 的亮度。
 * @return 准备或写入结果，不在此提交软件亮度缓存。
 */
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

/**
 * @brief 在背光串行区应用亮度，成功后才提交软件连续亮度。
 * @param[in] brightness 已经限制范围的目标亮度。
 * @return 成功为 D_OK；非零 PWM 应用失败时保留原亮度并返回错误；关闭可降级为 GPIO 低电平并返回 D_OK。
 * @note 调用方应持有背光锁；非零失败不抢占已打开 PWM 引脚，以保留最后成功的硬件占空比。
 */
static d_err_t d_display_gc9307_apply_brightness_locked(u8 brightness)
{
    d_err_t err = D_OK;

#ifdef D_LCD_BACKLIGHT_PWM_ENABLE
    err = d_display_gc9307_pwm_set_brightness(brightness);
    if (err != D_OK) {
        if (brightness == 0U) {
            /* 关闭失败时先释放 PWM，再将 GPIO 拉低，不能仍占 PWM 却宣称已关闭。 */
            d_display_gc9307_pwm_release();
            d_display_gc9307_set_gpio_backlight(0U);
            s_lcd_brightness = 0U;
            return D_OK;
        }
        /* 非零写入失败时保留已打开句柄及最后成功占空比；打开/启动失败已清句柄。
         * 不抢占 GPIO，也不公布未成功应用的连续亮度。 */
        return err;
    }
#else
    /* 纯数字背光仅表示开/关，软件百分比为零或最近请求值。 */
    d_display_gc9307_set_gpio_backlight(brightness > 0U ? 1U : 0U);
    err = D_OK;
#endif

    s_lcd_brightness = brightness;
    if (brightness > 0U) {
        s_lcd_last_nonzero_brightness = brightness;
    }
    return err;
}

/**
 * @brief 串行应用亮度；完整合同见 d_display.h。
 */
d_err_t d_display_set_brightness(uint8_t percent)
{
    u8 brightness = d_display_gc9307_clamp_brightness(percent);
    d_err_t err;

    /* 息屏状态下拒绝非零应用，不能绕过屏幕电源事实直接点亮；
     * wake/clear_off 路径先合法清除息屏状态。 */
    if ((brightness > 0U) && (d_screen_power_is_off() != 0U)) {
        return D_ERR_BUSY;
    }

    d_display_gc9307_bl_lock();
    err = d_display_gc9307_apply_brightness_locked(brightness);
    d_display_gc9307_bl_unlock();
    return err;
}

/**
 * @brief 读取已接纳的亮度缓存；完整合同见 d_display.h。
 */
uint8_t d_display_get_brightness(void)
{
    u8 value;

    d_display_gc9307_bl_lock();
    value = s_lcd_brightness;
    d_display_gc9307_bl_unlock();
    return value;
}

/**
 * @brief 关闭或恢复背光；完整合同见 d_display.h。
 */
d_err_t d_display_set_backlight(uint8_t on)
{
    u8 target;
    d_err_t err;

    /* 息屏时拒绝非零应用，只有 clear_off/wake 后才允许开启背光。 */
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

/**
 * @brief 保存显示启动结果并通知同步等待者。
 * @param[in] result 本次启动阶段结果。
 * @note 由初始化工作任务调用，静态信号量及结果仍可能在同步等待超时后使用。
 */
static void d_display_gc9307_signal_init(d_err_t result)
{
    s_lcd_init_result = result;
    s_lcd_init_done = 1;
    os_sem_post(&s_lcd_init_sem);
}

/**
 * @brief 初始化 GC9307/HYN 配置与默认背光，并发布启动结果。
 * @param[in] priv SDK 任务参数，当前未使用。
 * @note 面板成功后默认背光失败也报告失败；面板已初始化的路径保留原屏幕空闲任务，不在此进入 LVGL。
 */
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
        /* 面板初始化完成后再应用默认 PWM 亮度。 */
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

/**
 * @brief 创建 GC9307 初始化任务并有界等待；完整合同见 d_display.h。
 */
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

    /* 在工作任务可能调用 set_brightness 前先准备背光锁。 */
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
