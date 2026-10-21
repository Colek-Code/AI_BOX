/**
 * @file product_ui_camera_page.c
 * @brief 208x156 preview copied only from the existing LVGL update path.
 * @author D_Code
 * @date 2026-09-11
 */
#include "config/app_config.h"
#if D_CAMERA_PREVIEW_ENABLE
#include "product_ui_camera_controller.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include "platform/screen/profiles/gc9307_hyn/lvgl/lv_port_disp_gc9307.h"
#include <string.h>

#define PRODUCT_UI_CAMERA_REFRESH_MS 100U

typedef struct {
    product_ui_page_view_t frame;
    product_ui_camera_controller_t controller;
    lv_obj_t *image;
    lv_obj_t *message;
    const char *shown_status;
    const char *shown_button;
    uint32_t sequence;
    uint32_t displayed_session;
    uint32_t refresh_ms;
    unsigned int region_request_id;
    uint8_t is_created;
    uint8_t is_image_visible;
    uint8_t is_button_visible;
} product_ui_camera_page_t;

static product_ui_camera_page_t s_camera_page;
static uint8_t s_camera_ui_rgb[CAMERA_PREVIEW_RGB_BYTES] __attribute__((aligned(32)));
static const lv_image_dsc_t s_camera_image = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = CAMERA_PREVIEW_WIDTH,
    .header.h = CAMERA_PREVIEW_HEIGHT,
    .header.stride = CAMERA_PREVIEW_WIDTH * 2U,
    .data_size = CAMERA_PREVIEW_RGB_BYTES,
    .data = s_camera_ui_rgb,
};

static void product_ui_camera_click(lv_event_t *event)
{
    (void)event;
    product_ui_camera_retry(&s_camera_page.controller);
    s_camera_page.refresh_ms = PRODUCT_UI_CAMERA_REFRESH_MS;
}

static int product_ui_camera_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "相机", .back_title = "桌面", .show_back = 1U,
        .nav_action_text = "", .nav_action_cb = product_ui_camera_click,
        .nav_action_disabled = 1U,
    };

    if (parent == NULL) {
        return -1;
    }
    memset(&s_camera_page, 0, sizeof(s_camera_page));
    if (product_ui_page_frame_create_ex(parent, &spec, &s_camera_page.frame) != 0) {
        return -1;
    }
    lv_obj_remove_flag(s_camera_page.frame.content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_camera_page.image = lv_image_create(s_camera_page.frame.content);
    s_camera_page.message = lv_label_create(s_camera_page.frame.content);
    if (s_camera_page.image == NULL || s_camera_page.message == NULL) {
        return -1;
    }
    lv_image_set_src(s_camera_page.image, &s_camera_image);
    lv_obj_center(s_camera_page.image);
    lv_obj_add_flag(s_camera_page.image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_camera_page.image, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_width(s_camera_page.message, 296);
    lv_obj_set_style_text_font(s_camera_page.message, product_ui_font(PRODUCT_UI_FONT_BODY), 0);
    lv_obj_set_style_text_color(s_camera_page.message, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(s_camera_page.message, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_camera_page.message, LV_LABEL_LONG_WRAP);
    lv_label_set_text_static(s_camera_page.message, "正在打开相机");
    lv_obj_center(s_camera_page.message);
    s_camera_page.is_created = 1U;
    return 0;
}

static void product_ui_camera_page_enter(void)
{
    if (!s_camera_page.is_created) {
        return;
    }
    product_ui_camera_enter(&s_camera_page.controller);
    s_camera_page.refresh_ms = PRODUCT_UI_CAMERA_REFRESH_MS;
}

static void product_ui_camera_page_exit(void)
{
    product_ui_camera_leave(&s_camera_page.controller);
    lv_port_disp_gc9307_cancel_region_update(s_camera_page.region_request_id);
    s_camera_page.region_request_id = 0U;
}

static void product_ui_camera_page_destroy(void)
{
    product_ui_camera_page_exit();
    if (s_camera_page.image != NULL) {
        lv_image_cache_drop(&s_camera_image);
    }
    product_ui_page_view_reset(&s_camera_page.frame);
    /* The page manager owns object deletion, including partially created views. */
    memset(&s_camera_page, 0, sizeof(s_camera_page));
}

static void product_ui_camera_page_update(uint32_t elapsed_ms)
{
    product_ui_camera_controller_t *controller = &s_camera_page.controller;
    const char *nav_text;
    uint8_t is_visible;
    uint8_t has_new_frame = 0U;
    if (!s_camera_page.is_created) {
        return;
    }
    product_ui_process(elapsed_ms);
    product_ui_camera_update(controller);
    if (elapsed_ms < PRODUCT_UI_CAMERA_REFRESH_MS &&
        s_camera_page.refresh_ms < PRODUCT_UI_CAMERA_REFRESH_MS - elapsed_ms) {
        s_camera_page.refresh_ms += elapsed_ms;
        return;
    }
    s_camera_page.refresh_ms = 0U;
    /* Previous LVGL draw tasks have completed before this update. DMA reads its draw buffer. */
    if (camera_preview_copy_latest(controller->session, s_camera_ui_rgb, sizeof(s_camera_ui_rgb),
                                    &s_camera_page.sequence) == D_OK) {
        s_camera_page.displayed_session = controller->session;
        has_new_frame = 1U;
    }
    /* Cancellation can arrive during the copy; hide its buffer before LVGL consumes it. */
    product_ui_camera_update(controller);
    is_visible = controller->snapshot.phase == CAMERA_PREVIEW_RUNNING &&
                 !controller->was_screen_off && s_camera_page.displayed_session == controller->session &&
                 s_camera_page.sequence != 0U;
    if (is_visible != s_camera_page.is_image_visible) {
        lv_obj_update_flag(s_camera_page.image, LV_OBJ_FLAG_HIDDEN, !is_visible);
        lv_obj_update_flag(s_camera_page.message, LV_OBJ_FLAG_HIDDEN, is_visible);
        s_camera_page.is_image_visible = is_visible;
    }
    if (is_visible && has_new_frame) {
        lv_area_t area;
        lv_obj_update_layout(s_camera_page.image);
        lv_obj_get_coords(s_camera_page.image, &area);
        s_camera_page.region_request_id = lv_port_disp_gc9307_begin_region_update(&area);
        lv_obj_invalidate(s_camera_page.image);
    } else if (!is_visible) {
        lv_port_disp_gc9307_cancel_region_update(s_camera_page.region_request_id);
        s_camera_page.region_request_id = 0U;
    }
    if (controller->status != s_camera_page.shown_status) {
        lv_label_set_text_static(s_camera_page.message, controller->status);
        lv_obj_center(s_camera_page.message);
        s_camera_page.shown_status = controller->status;
    }
    nav_text = is_visible ? "实时预览" : (controller->can_retry ? controller->button : NULL);
    if (nav_text != s_camera_page.shown_button ||
        controller->can_retry != s_camera_page.is_button_visible) {
        product_ui_page_action_update(&s_camera_page.frame, nav_text,
                                        controller->can_retry, is_visible);
        s_camera_page.shown_button = nav_text;
        s_camera_page.is_button_visible = controller->can_retry;
    }
}

static const page_ops_t s_camera_page_ops = {
    .create = product_ui_camera_page_create, .enter = product_ui_camera_page_enter,
    .exit = product_ui_camera_page_exit, .destroy = product_ui_camera_page_destroy,
    .update = product_ui_camera_page_update, .flags = PAGE_FLAG_NO_GESTURE,
};
static page_desc_t s_camera_page_desc = { .name = "ProductCamera", .ops = &s_camera_page_ops };
static int product_ui_camera_register(int page_type)
{
    s_camera_page_desc.page_type = page_type;
    return page_manager_register(&s_camera_page_desc);
}
D_UI_MODULE_DEFINE(product_camera, "相机", "Camera", product_ui_camera_register, NULL);
#endif
