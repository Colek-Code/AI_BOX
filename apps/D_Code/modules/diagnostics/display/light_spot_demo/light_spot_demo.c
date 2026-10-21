/**
 * @file light_spot_demo.c
 * @brief Two light spots main page demo.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_LIGHT_SPOT_DEMO)

#include "modules/diagnostics/display/light_spot_demo/light_spot_demo.h"
#include "framework/d_page_manager.h"
#include "lvgl.h"
#include <stdint.h>

#define LOG_TAG "[MAIN_PAGE] "

#define LIGHT_SPOT_DEMO_A_SIZE        160
#define LIGHT_SPOT_DEMO_B_SIZE        190
#define LIGHT_SPOT_DEMO_A_RADIUS      (LIGHT_SPOT_DEMO_A_SIZE / 2)
#define LIGHT_SPOT_DEMO_B_RADIUS      (LIGHT_SPOT_DEMO_B_SIZE / 2)
#define LIGHT_SPOT_DEMO_A_CENTER_X    (MY_DISP_HOR_RES * 35 / 100)
#define LIGHT_SPOT_DEMO_A_CENTER_Y    (MY_DISP_VER_RES * 42 / 100)
#define LIGHT_SPOT_DEMO_B_CENTER_X    (MY_DISP_HOR_RES * 65 / 100)
#define LIGHT_SPOT_DEMO_B_CENTER_Y    (MY_DISP_VER_RES * 58 / 100)
#define LIGHT_SPOT_DEMO_A_RANGE_X     (MY_DISP_HOR_RES * 18 / 100)
#define LIGHT_SPOT_DEMO_A_RANGE_Y     (MY_DISP_VER_RES * 15 / 100)
#define LIGHT_SPOT_DEMO_B_RANGE_X     (MY_DISP_HOR_RES * 14 / 100)
#define LIGHT_SPOT_DEMO_B_RANGE_Y     (MY_DISP_VER_RES * 15 / 100)

static lv_obj_t *s_light_spot_a = NULL;
static lv_obj_t *s_light_spot_b = NULL;
static int16_t s_light_spot_angle_ax = 0;
static int16_t s_light_spot_angle_ay = 47;
static int16_t s_light_spot_angle_bx = 200;
static int16_t s_light_spot_angle_by = 130;

static void light_spot_demo_clean_obj(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void light_spot_demo_set_fullscreen(lv_obj_t *obj, lv_color_t color)
{
    if (obj == NULL) {
        return;
    }

    light_spot_demo_clean_obj(obj);
    lv_obj_set_size(obj, MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

static void light_spot_demo_style_spot(lv_obj_t *obj, int size, uint32_t color)
{
    if (obj == NULL) {
        return;
    }

    light_spot_demo_clean_obj(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, size / 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
}

static void light_spot_demo_set_pos(lv_obj_t *obj, int center_x, int center_y, int radius)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_set_pos(obj, center_x - radius, center_y - radius);
}

static int light_spot_demo_create(lv_obj_t *parent)
{
    lv_obj_t *scr;

    if (parent == NULL) {
        return -1;
    }

    scr = lv_obj_get_parent(parent);
    if (scr != NULL) {
        light_spot_demo_clean_obj(scr);
    }
    light_spot_demo_set_fullscreen(parent, lv_color_hex(0x111827));

    s_light_spot_a = lv_obj_create(parent);
    if (s_light_spot_a == NULL) {
        return -1;
    }
    light_spot_demo_style_spot(s_light_spot_a, LIGHT_SPOT_DEMO_A_SIZE, 0xFF6B6B);

    s_light_spot_b = lv_obj_create(parent);
    if (s_light_spot_b == NULL) {
        return -1;
    }
    light_spot_demo_style_spot(s_light_spot_b, LIGHT_SPOT_DEMO_B_SIZE, 0x4ECDC4);

    s_light_spot_angle_ax = 0;
    s_light_spot_angle_ay = 47;
    s_light_spot_angle_bx = 200;
    s_light_spot_angle_by = 130;

    light_spot_demo_set_pos(s_light_spot_a,
                            LIGHT_SPOT_DEMO_A_CENTER_X,
                            LIGHT_SPOT_DEMO_A_CENTER_Y,
                            LIGHT_SPOT_DEMO_A_RADIUS);
    light_spot_demo_set_pos(s_light_spot_b,
                            LIGHT_SPOT_DEMO_B_CENTER_X,
                            LIGHT_SPOT_DEMO_B_CENTER_Y,
                            LIGHT_SPOT_DEMO_B_RADIUS);

    printf(LOG_TAG "Light spot demo created\n");
    return 0;
}

static void light_spot_demo_update(uint32_t elapsed_ms)
{
    int32_t ax;
    int32_t ay;
    int32_t bx;
    int32_t by;
    int step;

    if (s_light_spot_a == NULL || s_light_spot_b == NULL) {
        return;
    }

    step = elapsed_ms > 33 ? 2 : 1;

    s_light_spot_angle_ax = (int16_t)((s_light_spot_angle_ax + step) % 360);
    s_light_spot_angle_ay = (int16_t)((s_light_spot_angle_ay + step * 2) % 360);
    s_light_spot_angle_bx = (int16_t)((s_light_spot_angle_bx + step) % 360);
    s_light_spot_angle_by = (int16_t)((s_light_spot_angle_by + step * 3) % 360);

    ax = LIGHT_SPOT_DEMO_A_CENTER_X +
         (int32_t)lv_trigo_sin(s_light_spot_angle_ax) * LIGHT_SPOT_DEMO_A_RANGE_X / 32768;
    ay = LIGHT_SPOT_DEMO_A_CENTER_Y +
         (int32_t)lv_trigo_sin(s_light_spot_angle_ay) * LIGHT_SPOT_DEMO_A_RANGE_Y / 32768;
    bx = LIGHT_SPOT_DEMO_B_CENTER_X +
         (int32_t)lv_trigo_sin(s_light_spot_angle_bx) * LIGHT_SPOT_DEMO_B_RANGE_X / 32768;
    by = LIGHT_SPOT_DEMO_B_CENTER_Y +
         (int32_t)lv_trigo_sin(s_light_spot_angle_by) * LIGHT_SPOT_DEMO_B_RANGE_Y / 32768;

    light_spot_demo_set_pos(s_light_spot_a, (int)ax, (int)ay, LIGHT_SPOT_DEMO_A_RADIUS);
    light_spot_demo_set_pos(s_light_spot_b, (int)bx, (int)by, LIGHT_SPOT_DEMO_B_RADIUS);
}

static void light_spot_demo_enter(void)
{
    printf(LOG_TAG "Light spot demo enter\n");
}

static void light_spot_demo_exit(void)
{
    printf(LOG_TAG "Light spot demo exit\n");
}

static const page_ops_t s_light_spot_demo_ops = {
    .create  = light_spot_demo_create,
    .enter   = light_spot_demo_enter,
    .exit    = light_spot_demo_exit,
    .destroy = NULL,
    .update  = light_spot_demo_update,
    .gesture = NULL,
};

static const page_desc_t s_light_spot_demo_desc = {
    .page_type  = PAGE_TYPE_MAIN,
    .name       = "LightSpotDemo",
    .ops        = &s_light_spot_demo_ops,
    .container  = NULL,
    .is_created = 0,
    .is_active  = 0,
};

int light_spot_demo_register(void)
{
    return page_manager_register(&s_light_spot_demo_desc);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_LIGHT_SPOT_DEMO) */
