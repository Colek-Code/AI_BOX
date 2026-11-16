/**
 * @file lv_port_disp_emi.c
 * @brief 适配 EMI/ILI948x 的既有 LVGL 全帧显示及图像合成路径。
 * D++: 归属 D_Code EMI/ILI948x/GT911/FT6236 显示配置。
 */
/*********************
 *      头文件
 *********************/
#define BOOL_DEFINE_CONFLICT
#include "framework/d_lvgl_port.h"
#include "lvgl.h"
#include "src/display/lv_display_private.h"
#include <stdbool.h>
#include "config/app_config.h"

#if defined(D_LCD_ENABLE) && defined(D_LCD_EMI_ILI948X_ENABLE)

#include "platform/screen/lcd/d_lcd_compat.h"
#include "lcd_data_driver.h"

/*********************
 *      配置定义
 *********************/
#ifndef MY_DISP_HOR_RES
#warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen width, default value 320 is used for now.
#define MY_DISP_HOR_RES    320
#endif

#ifndef MY_DISP_VER_RES
#warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen height, default value 240 is used for now.
#define MY_DISP_VER_RES    240
#endif

/**********************
 *      类型定义
 **********************/
#if LV_COLOR_DEPTH==16
#define LV_PIXEL_COLOR_T lv_color16_t
#endif

#define DISP_PHYS_HOR_RES        320
#define DISP_PHYS_VER_RES        480
#define DISP_FLUSH_WAIT_TICKS    10

#ifndef D_LVGL_LOGICAL_LANDSCAPE
#define D_LVGL_LOGICAL_LANDSCAPE 0
#endif


static void disp_init(void);

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void disp_flush_wait(lv_display_t *disp);
static u8 *disp_rotate_landscape_to_lcd(const lv_area_t *area, uint8_t *px_map);

/**********************
 *  私有状态
 **********************/
static volatile int s_disp_flush_started = 0;

/**********************
 *      MACROS
 **********************/
/* #define THREE_FB_ACCELERATION  //利用双核双线程+3FB加速 ,某些场景能够提升1-2帧  */

#ifdef THREE_FB_ACCELERATION
#include "os/os_api.h"
#include "spinlock.h"
static OS_SEM mcu_lcd_physics_flush_sem, mcu_lcd_physics_flush_end_sem;
static LV_PIXEL_COLOR_T buf_3_3[MY_DISP_HOR_RES * MY_DISP_VER_RES] __attribute__((aligned(32)));            /* 第三份整屏缓冲。 */

struct lv_fb_t {
    LV_PIXEL_COLOR_T *fb;
    lv_area_t area;
};
static struct lv_fb_t cur_disp;/*当前 LCD 的显示显存*/
volatile static struct lv_fb_t next_disp;/* next_fb地址 */
static struct lv_fb_t next_offline_disp;/* 上一帧online_fb，也是下一帧offline_fb */
static spinlock_t fb_lock; //双核互斥锁
#endif

/**********************
 *   公开入口
 **********************/
#if LV_COLOR_DEPTH==16
/**
 * @brief 将 CPU 原生 RGB565 数据复制为 EMI 所需的高字节在前线序。
 * @param[in] area 局部模式的有效逻辑区域；FULL 模式按整屏像素数处理。
 * @param[in] px_map 连续有效 RGB565 像素，由内部调用方保证容量及对齐。
 * @return 静态整屏交换缓冲的借用指针，不转移所有权。
 * @note 不改 LVGL 原帧，不处理额外行填充；本配置默认 FULL。调用方沿已有刷新完成边界串行复用静态缓冲。
 */
static void *lv_color_16_swap(const lv_area_t *area, uint8_t *px_map)
{
    /*printf("lv_color_16_swap -> %d, %d, %d, %d, 0x%x",area->x1,area->x2,area->y1,area->y2,px_map);*/
    static LV_PIXEL_COLOR_T buf_swap[MY_DISP_HOR_RES * MY_DISP_VER_RES] __attribute__((aligned(32)));
    uint8_t *data = buf_swap;

    if (LV_GLOBAL_DEFAULT()->disp_refresh->render_mode != LV_DISPLAY_RENDER_MODE_FULL) {
        for (int i = 0; i < (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1); i++) { //芯片EMI硬件不支持小端输出， 只能够软件转换, 正常是不允许修改fb，否则把绘制内容给改掉
            uint16_t color = ((uint16_t *)px_map)[i];
            data[2 * i] = (color >> 8) & 0xff;
            data[2 * i + 1] = color & 0xff;
        }
    } else {
        for (int i = 0; i < MY_DISP_HOR_RES * MY_DISP_VER_RES; i++) { //芯片EMI硬件不支持小端输出， 只能够软件转换, 正常是不允许修改fb，否则把绘制内容给改掉
            uint16_t color = ((uint16_t *)px_map)[i];
            data[2 * i] = (color >> 8) & 0xff;
            data[2 * i + 1] = color & 0xff;
        }
    }

    return data;
}
#endif

/**
 * @brief 注册 EMI 全帧双缓冲及既有刷新回调；通用合同见 framework/d_lvgl_port.h。
 * @note 可选三缓冲路径保持原编译选择；本入口不报告独立的分配或物理显示完成结果。
 */
void lv_port_disp_init(void)
{
    /* 初始化当前显示适配。 */
    disp_init();

    /* 创建显示并装配刷新及等待回调。 */
    lv_display_t *disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_flush_wait_cb(disp, disp_flush_wait);
#if 0
    /* 关闭的示例一：单缓冲局部渲染。 */
    static LV_PIXEL_COLOR_T buf_1_1[MY_DISP_HOR_RES * 10];                          /* 容纳 10 行的缓冲。 */
    lv_display_set_buffers(disp, buf_1_1, NULL, sizeof(buf_1_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
#if 0
    /* 关闭的示例二：双缓冲局部渲染，后台刷新须遵循回调完成边界。 */
    static LV_PIXEL_COLOR_T buf_2_1[MY_DISP_HOR_RES * 10];
    static LV_PIXEL_COLOR_T buf_2_2[MY_DISP_HOR_RES * 10];
    lv_display_set_buffers(disp, buf_2_1, buf_2_2, sizeof(buf_2_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    /* 现有整屏双缓冲方案；当前选择 FULL 以满足摄像头与 UI 合成。 */
    static LV_PIXEL_COLOR_T buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES] __attribute__((aligned(32)));            /* 第一份整屏缓冲。 */
    static LV_PIXEL_COLOR_T buf_3_2[MY_DISP_HOR_RES * MY_DISP_VER_RES] __attribute__((aligned(32)));            /* 第二份整屏缓冲。 */

#ifdef THREE_FB_ACCELERATION
    lv_display_set_buffers(disp, buf_3_1, buf_3_2, buf_3_3, sizeof(buf_3_1), LV_DISPLAY_RENDER_MODE_FULL);
    /*lv_display_set_buffers(disp, buf_3_1, buf_3_2, buf_3_3, sizeof(buf_3_1), LV_DISPLAY_RENDER_MODE_PARTIAL);*/
#else
    /* 使用 FULL 模式确保整帧UI数据一起发送，这对于摄像头+UI合成是必需的 */
    /* PARTIAL 模式会分块发送，导致UI数据不完整 */
    lv_display_set_buffers(disp, buf_3_1, buf_3_2, NULL, sizeof(buf_3_1), LV_DISPLAY_RENDER_MODE_FULL);
#endif
#ifdef THREE_FB_ACCELERATION
    /* next_offline_fb 变量保存下一帧的 offline_fb 地址  */
    next_offline_disp.fb = buf_3_3;
    /* 调用系统API把 online_fb 的地址设置为当前 LCD 的显示显存 */
    cur_disp.fb = buf_3_2;

    lcd_interface_set_non_block(0); //因为有专用线程, 因此使用阻塞方式释放cpu
    os_sem_create(&mcu_lcd_physics_flush_sem, 0);
    os_sem_create(&mcu_lcd_physics_flush_end_sem, 1);
    void lvgl_lcd_draw_task(void *p);
    thread_fork("lvgl_lcd_draw_task", 29, 200, 0, NULL, lvgl_lcd_draw_task, NULL);
#endif
}

/**********************
 *   私有实现
 **********************/

#ifdef THREE_FB_ACCELERATION
/**
 * @brief 在可选三缓冲路径中等候交换位置，再转交已绘制缓冲。
 * @param[in] area 非 FULL 模式时随帧复制的有效区域。
 * @note UI 线程调用；帧指针交换在原 fb_lock 内执行，硬件发送由专属任务处理。
 */
static void lv_lcd_draw_trig(const lv_area_t *area)
{
    /* 等到s_next_fb空闲同步已完成 */
    os_sem_pend(&mcu_lcd_physics_flush_end_sem, 0);
    /*while (next_disp.fb != NULL) {};*/
    spin_lock(&fb_lock);
    /* 将绘制好的 offline_fb 设置为 next_fb */
    next_disp.fb = LV_GLOBAL_DEFAULT()->disp_refresh->buf_act->data;
    if (LV_GLOBAL_DEFAULT()->disp_refresh->render_mode != LV_DISPLAY_RENDER_MODE_FULL) {
        memcpy((void *)&next_disp.area, area, sizeof(lv_area_t));
    }
    /* 把上一帧的 online_fb 作为下一帧的 offline_fb */
    LV_GLOBAL_DEFAULT()->disp_refresh->buf_act->data = next_offline_disp.fb;
    spin_unlock(&fb_lock);

    /*os_sem_set(&mcu_lcd_physics_flush_sem,0);*/
    os_sem_post(&mcu_lcd_physics_flush_sem);
}

/**
 * @brief 消费可选三缓冲帧，交换前后台引用并转交 EMI 显示。
 * @param[in] p SDK 任务参数，当前未使用。
 * @note 沿原缓冲及等待机制运行，不提供 GC9307 完成票据、方向代次或新的资源仲裁。
 */
void lvgl_lcd_draw_task(void *p)
{
    LV_PIXEL_COLOR_T *pixel;
    while (1) {
        os_sem_pend(&mcu_lcd_physics_flush_sem, 0);

        while (next_disp.fb != NULL) {
            spin_lock(&fb_lock);
            /* next_offline_fb 变量保存上一帧的 online_fb 地址 */
            next_offline_disp.fb = cur_disp.fb;
            /* 调用系统API把 online_fb 的地址设置为当前 LCD 的显示显存 */
            cur_disp.fb = next_disp.fb;
            if (LV_GLOBAL_DEFAULT()->disp_refresh->render_mode != LV_DISPLAY_RENDER_MODE_FULL) {
                memcpy(&cur_disp.area, (const void *)&next_disp.area, sizeof(lv_area_t));
            }
            next_disp.fb = NULL;
            spin_unlock(&fb_lock);
            os_sem_post(&mcu_lcd_physics_flush_end_sem);
#if LV_COLOR_DEPTH==16
            pixel = lv_color_16_swap(&cur_disp.area, cur_disp.fb);
#else
            pixel = cur_disp.fb;
#endif
            if (LV_GLOBAL_DEFAULT()->disp_refresh->render_mode != LV_DISPLAY_RENDER_MODE_FULL) {
                lcd_lvgl_full(cur_disp.area.x1, cur_disp.area.x2, cur_disp.area.y1, cur_disp.area.y2, pixel);
            } else {
                lcd_lvgl_full(0, MY_DISP_HOR_RES, 0, MY_DISP_VER_RES, pixel);
            }
        }
    }
}
#endif

/**
 * @brief 调用兼容 LCD 用户初始化钩子，真实面板初始化已由启动层执行。
 */
static void disp_init(void)
{
    /* 本钩子不重复初始化面板。 */
    user_lcd_init();
}
bool disp_flush_enabled = true;

/**
 * @brief 允许后续 EMI 刷新提交；通用合同见 framework/d_lvgl_port.h。
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/**
 * @brief 禁止后续 EMI 刷新提交；不停止已经提交的硬件操作。
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/**
 * @brief 在既有横屏配置下将完整逻辑画面转为 EMI 物理扫描线序。
 * @param[in] area 必须覆盖完整逻辑画面的区域。
 * @param[in] px_map 足量的有效 RGB565 画面。
 * @return 满足横屏、尺寸、整帧范围和缓冲条件时为 SDK 显示缓冲借用指针；否则为 NULL。
 * @note 不是运行期 0°/180° 事务，不更改面板参数；只在原显示节奏中串行使用借用目标缓冲。
 */
static u8 *disp_rotate_landscape_to_lcd(const lv_area_t *area, uint8_t *px_map)
{
#if LV_COLOR_DEPTH==16 && D_LVGL_LOGICAL_LANDSCAPE
    u8 *lcd_buf = get_show_buf();
    u16 *src = (u16 *)px_map;

    if (lcd_buf == NULL || px_map == NULL || area == NULL) {
        return NULL;
    }

    if (area->x1 != 0 || area->y1 != 0 ||
        area->x2 != (MY_DISP_HOR_RES - 1) ||
        area->y2 != (MY_DISP_VER_RES - 1)) {
        return NULL;
    }

    if (MY_DISP_HOR_RES != DISP_PHYS_VER_RES ||
        MY_DISP_VER_RES != DISP_PHYS_HOR_RES) {
        return NULL;
    }

    if (lcd_w != DISP_PHYS_HOR_RES || lcd_h != DISP_PHYS_VER_RES) {
        return NULL;
    }

    for (u16 x = 0; x < MY_DISP_HOR_RES; x++) {
        u16 *src_col = src + x;
        u8 *dst = lcd_buf + ((u32)x * DISP_PHYS_HOR_RES + (DISP_PHYS_HOR_RES - 1)) * 2;

        for (u16 y = 0; y < MY_DISP_VER_RES; y++) {
            u16 color = *src_col;

            dst[0] = (color >> 8) & 0xff;
            dst[1] = color & 0xff;
            src_col += MY_DISP_HOR_RES;
            dst -= 2;
        }
    }

    return lcd_buf;
#else
    (void)area;
    (void)px_map;
    return NULL;
#endif
}

/**
 * @brief 按原 SDK 等待链收取已提交 EMI 刷新，再向 LVGL 归还缓冲。
 * @param[in] disp 当前 LVGL 显示对象。
 * @note 沿既有等待 tick 策略；兼容等待结果不形成 GC9307 的独立 DMA 确认合同。
 */
static void disp_flush_wait(lv_display_t *disp)
{
    if (s_disp_flush_started) {
        lcd_lvgl_flush_wait(DISP_FLUSH_WAIT_TICKS);
        s_disp_flush_started = 0;
    }
    lv_display_flush_ready(disp);
}

/**
 * @brief 按当前 SDK 图像模式选择 UI 合成、直接传屏或忽略本次 LVGL 数据。
 * @param[in] disp_drv 当前 LVGL 显示描述符，当前仅保留回调兼容参数。
 * @param[in] area 本次有效逻辑区域。
 * @param[in] px_map 本次 LVGL 像素数据，沿原刷新完成边界保持。
 * @note UI_CAMERA 合成只在 compose_status 为 1 时提交 UI；纯相机模式沿原策略忽略 UI。直接 UI 路径只接受匹配面板的整帧，不新增区域能力。
 */

static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
#ifdef D_LCD_EMI_FLUSH_LOG_ENABLE
    static u32 flush_count = 0;
    flush_count++;
#endif

    if (disp_flush_enabled) {
#ifdef THREE_FB_ACCELERATION
        lv_lcd_draw_trig(area);
#else
        /* 获取当前显示模式 */
        u8 current_mode = get_lcd_show_deta_mode();
        u8 compose_status = get_lcd_compose_status();

#ifdef D_LCD_EMI_FLUSH_LOG_ENABLE
        if (flush_count <= 10) {
            printf("[DISP_FLUSH] #%d mode=%d compose=%d area=%d,%d,%d,%d px_map=%x\n",
                   flush_count, current_mode, compose_status,
                   area->x1, area->x2, area->y1, area->y2, (u32)px_map);
        }
#endif

        /* 只有在UI_CAMERA模式且compose_status=1时才进行UI叠加处理 */
        /* CAMERA模式(compose_status=0): 摄像头直接显示，忽略LVGL刷屏 */
        /* UI_CAMERA模式(compose_status=1): UI和摄像头合成 */
        if (current_mode == UI_CAMERA && compose_status == 1) {
            /* FULL模式下，LVGL发送整屏数据，area是全屏范围 */
            /* 将 CPU 原生 RGB565 转换为 EMI 所需的高字节在前线序。 */
#if LV_COLOR_DEPTH==16
            px_map = lv_color_16_swap(area, px_map);
#endif
            /* 设置UI数据的坐标范围 */
            set_lcd_ui_x_y(area->x1, area->x2, area->y1, area->y2);

            /* 将LVGL数据发送到叠加系统 */
            ui_send_data_ready(px_map,
                              (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) * 2);
        } else if (current_mode == UI) {
            /* UI模式，直接推屏 */
#if LV_COLOR_DEPTH==16
            /* 保留提交与等待分离；硬件等待由既有 disp_flush_wait 路径执行。 */
            /* 此处保留 LVGL 原生 RGB565，由横屏转换写出面板线序。 */
#endif
#ifdef D_LCD_EMI_FLUSH_LOG_ENABLE
            if (flush_count <= 10) {
                printf("[DISP_FLUSH] UI mode, starting TE flush\n");
            }
#endif
#if D_LVGL_LOGICAL_LANDSCAPE
            u8 *lcd_buf = disp_rotate_landscape_to_lcd(area, px_map);
            if (lcd_buf != NULL) {
                lcd_lvgl_flush_start(lcd_buf, lcd_w * lcd_h * 2);
                s_disp_flush_started = 1;
            } else {
                s_disp_flush_started = 0;
                printf("[DISP_FLUSH] rotate failed\n");
            }
#else
#if LV_COLOR_DEPTH==16
            px_map = lv_color_16_swap(area, px_map);
#endif
            if (area->x1 == 0 && area->y1 == 0 &&
                area->x2 == (MY_DISP_HOR_RES - 1) &&
                area->y2 == (MY_DISP_VER_RES - 1) &&
                lcd_w == MY_DISP_HOR_RES &&
                lcd_h == MY_DISP_VER_RES) {
                lcd_lvgl_flush_start(px_map, lcd_w * lcd_h * 2);
                s_disp_flush_started = 1;
            } else {
                s_disp_flush_started = 0;
                printf("[DISP_FLUSH] portrait flush range mismatch\n");
            }
#endif
        }
        /* CAMERA模式(current_mode==UI_CAMERA && compose_status==0): 忽略LVGL刷屏 */
#endif
    } else {
        printf("[DISP_FLUSH] flush disabled!\n");
    }
    (void)disp_drv;
}
#endif /* D_LCD_ENABLE && D_LCD_EMI_ILI948X_ENABLE */
