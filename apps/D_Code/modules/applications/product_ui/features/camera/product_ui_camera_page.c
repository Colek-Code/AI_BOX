/**
 * @file product_ui_camera_page.c
 * @brief 相机页面只在现有 LVGL 更新路径拷贝 208×156 帧并管理局部刷新。
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
    lv_display_t *display;
    const char *shown_status;
    const char *shown_button;
    uint32_t sequence;
    uint32_t displayed_session;
    uint32_t refresh_ms;
    unsigned int region_request_id;
    uint8_t is_created;
    uint8_t is_image_visible;
    uint8_t is_button_visible;
    uint8_t is_content_flipped;
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

/**
 * @brief 按当前渲染方向原地翻转已拷贝的 RGB565 帧。
 * @return 本次执行翻转返回 1；无显示、无帧或方向未变返回 0。
 * @note 传感器方向跟随物理板，使用渲染方向让首次翻转帧及回滚帧与 UI 保持一致。
 */
static uint8_t product_ui_camera_orient_frame(void)
{
    uint32_t front, back;
    uint8_t flipped;
    if (s_camera_page.display == NULL || s_camera_page.sequence == 0U) return 0U;
    /* 传感器帧跟随物理板而独立于 UI 几何；使用渲染方向，让首个翻转／回滚帧一致。 */
    flipped = lv_display_get_rotation(s_camera_page.display) == LV_DISPLAY_ROTATION_270;
    if (flipped == s_camera_page.is_content_flipped) return 0U;
    for (front = 0U, back = sizeof(s_camera_ui_rgb) - 2U; front < back; front += 2U, back -= 2U) {
        uint8_t low = s_camera_ui_rgb[front];
        uint8_t high = s_camera_ui_rgb[front + 1U];
        s_camera_ui_rgb[front] = s_camera_ui_rgb[back];
        s_camera_ui_rgb[front + 1U] = s_camera_ui_rgb[back + 1U];
        s_camera_ui_rgb[back] = low;
        s_camera_ui_rgb[back + 1U] = high;
    }
    s_camera_page.is_content_flipped = flipped;
    return 1U;
}

/**
 * @brief 方向变化后取消旧局部刷新票据，并使可见的新方向图像失效重绘。
 * @param[in] event 显示分辨率变化事件，当前不读取载荷。
 */
static void product_ui_camera_orientation_cb(lv_event_t *event)
{
    (void)event;
    if (!s_camera_page.is_created || !product_ui_camera_orient_frame()) return;
    lv_port_disp_gc9307_cancel_region_update(s_camera_page.region_request_id);
    s_camera_page.region_request_id = 0U;
    if (s_camera_page.is_image_visible) lv_obj_invalidate(s_camera_page.image);
}

/**
 * @brief 把导航操作转交控制器显式重试，并请求下一轮立即刷新。
 * @param[in] event 借用的 LVGL 点击事件，当前不读取载荷。
 */
static void product_ui_camera_click(lv_event_t *event)
{
    (void)event;
    product_ui_camera_retry(&s_camera_page.controller);
    s_camera_page.refresh_ms = PRODUCT_UI_CAMERA_REFRESH_MS;
}

/**
 * @brief 创建相机页面框架、图像和状态标签并注册显示方向监听。
 * @param[in,out] parent 页面管理器拥有的父对象。
 * @return 创建和监听验证成功返回 0；空父对象或任一步失败返回 -1。
 * @note 失败时可能保留部分页面对象；destroy 负责撤销监听与状态，页面管理器最终删除对象。
 */
static int product_ui_camera_page_create(lv_obj_t *parent)
{
    uint32_t event_count;
    static const product_ui_page_spec_t spec = {
        .title = "相机", .back_title = "桌面", .show_back = 1U,
        .role = PRODUCT_UI_PAGE_ROLE_CONTENT,
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
    s_camera_page.display = lv_obj_get_display(s_camera_page.image);
    if (s_camera_page.display == NULL) return -1;
    event_count = lv_display_get_event_count(s_camera_page.display);
    lv_display_add_event_cb(s_camera_page.display, product_ui_camera_orientation_cb,
                            LV_EVENT_RESOLUTION_CHANGED, &s_camera_page);
    if (lv_display_get_event_count(s_camera_page.display) != event_count + 1U) return -1;
    s_camera_page.is_created = 1U;
    return 0;
}

/**
 * @brief 附着相机控制器并让下次周期立即检查预览。
 */
static void product_ui_camera_page_enter(void)
{
    if (!s_camera_page.is_created) {
        return;
    }
    product_ui_camera_enter(&s_camera_page.controller);
    s_camera_page.refresh_ms = PRODUCT_UI_CAMERA_REFRESH_MS;
}

/**
 * @brief 解除控制器附着并取消本页局部刷新请求。
 * @note 停止为异步请求，页面退出不宣称硬件已经停止。
 */
static void product_ui_camera_page_exit(void)
{
    product_ui_camera_leave(&s_camera_page.controller);
    lv_port_disp_gc9307_cancel_region_update(s_camera_page.region_request_id);
    s_camera_page.region_request_id = 0U;
}

/**
 * @brief 退出页面、撤销显示监听和图像缓存，再清空页面借用状态。
 * @note 页面管理器拥有对象删除，包括创建失败时留下的部分对象。
 */
static void product_ui_camera_page_destroy(void)
{
    product_ui_camera_page_exit();
    if (s_camera_page.display != NULL) {
        lv_display_remove_event_cb_with_user_data(s_camera_page.display,
            product_ui_camera_orientation_cb, &s_camera_page);
    }
    if (s_camera_page.image != NULL) {
        lv_image_cache_drop(&s_camera_image);
    }
    product_ui_page_view_reset(&s_camera_page.frame);
    /* 对象及部分创建视图均由页面管理器删除。 */
    memset(&s_camera_page, 0, sizeof(s_camera_page));
}

/**
 * @brief 推进公共 UI 和相机控制器，按节奏拷贝最新帧并更新可见状态。
 * @param[in] elapsed_ms 本轮页面周期经过的毫秒数。
 * @note 控制中心拉出至关闭期间冻结背景，但继续观察停止、灭屏与维护状态；拷贝后再次检查取消，避免已取消帧交给 LVGL。
 */
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
    if (product_ui_control_center_is_busy()) {
        /* 从初次下拉到关闭（包括回弹）冻结背景；
         * 控制器仍继续观察停止、屏幕及维护状态变化。 */
        lv_port_disp_gc9307_cancel_region_update(s_camera_page.region_request_id);
        s_camera_page.region_request_id = 0U;
        s_camera_page.refresh_ms = PRODUCT_UI_CAMERA_REFRESH_MS;
    } else {
        if (elapsed_ms < PRODUCT_UI_CAMERA_REFRESH_MS &&
            s_camera_page.refresh_ms < PRODUCT_UI_CAMERA_REFRESH_MS - elapsed_ms) {
            s_camera_page.refresh_ms += elapsed_ms;
            return;
        }
        s_camera_page.refresh_ms = 0U;
        /* 前轮 LVGL 绘制任务已结束；DMA 读取的是其自己的绘制缓冲区。 */
        if (camera_preview_copy_latest(controller->session, s_camera_ui_rgb, sizeof(s_camera_ui_rgb),
                                        &s_camera_page.sequence) == D_OK) {
            s_camera_page.is_content_flipped = 0U;
            (void)product_ui_camera_orient_frame();
            s_camera_page.displayed_session = controller->session;
            has_new_frame = 1U;
        }
    }
    /* 拷贝中可能收到取消；LVGL 使用缓冲区前再次检查并隐藏已取消图像。 */
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
/**
 * @brief 将静态相机页面描述注册到页面管理器。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int product_ui_camera_register(int page_type)
{
    s_camera_page_desc.page_type = page_type;
    return page_manager_register(&s_camera_page_desc);
}
D_UI_MODULE_DEFINE(product_camera, "相机", "Camera", product_ui_camera_register, NULL);
#endif
