/** @file product_ui_overlay_content.c @brief 320×240 控制中心内容投影及服务命令，完全打开后才授予交互。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shell/control_center/product_ui_overlay_internal.h"
#include "modules/applications/product_ui/shell/control_center/product_ui_control_widgets.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "modules/applications/product_ui/features/media/product_ui_media_control.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"
#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "modules/applications/product_ui/debug/product_ui_visual_capture.h"
#include <string.h>

/* 几何统一来自 product_ui_theme.h 的 PRODUCT_UI_CC_*，不在内容层另造尺寸。 */

static const product_ui_quick_card_spec_t s_quick_specs[] = {
    {PRODUCT_UI_CC_QUICK_W, PRODUCT_UI_CC_QUICK_H, PRODUCT_UI_QUICK_TILE_ALARM, "闹钟"},
    {PRODUCT_UI_CC_QUICK_W, PRODUCT_UI_CC_QUICK_H, PRODUCT_UI_QUICK_TILE_ROTATION, "翻转"},
    {PRODUCT_UI_CC_QUICK_W, PRODUCT_UI_CC_QUICK_H, PRODUCT_UI_QUICK_TILE_SETTINGS, "设置"},
};

typedef struct {
    product_ui_service_snapshot_t settings;
    product_ui_service_snapshot_t media;
    product_ui_service_snapshot_t alarm;
    product_ui_service_snapshot_t system;
} product_ui_control_services_t;

static void product_ui_control_set_enabled(
    product_ui_control_center_content_t *content, lv_obj_t *control,
    uint8_t is_enabled);
static void product_ui_media_action_cb(lv_event_t *event);

/**
 * @brief 清除默认样式、滚动及冒泡，并重置为显式绝对布局。
 * @param[in,out] obj 有效控件。
 */
static void product_ui_control_clean(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_layout(obj, LV_LAYOUT_NONE, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

/**
 * @brief 在容量内登记主动命中控件并关闭手势／事件冒泡。
 * @param[in,out] content 持久内容状态。
 * @param[in,out] obj 需纳入完全打开交互门禁的对象。
 * @return 登记成功为 0，空对象或命中表满为 -1。
 */
static int product_ui_control_record_interactive(
    product_ui_control_center_content_t *content, lv_obj_t *obj)
{
    if ((obj == NULL) ||
        (content->interactive_count >= PRODUCT_UI_CONTROL_INTERACTIVE_MAX)) {
        return -1;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    content->interactive[content->interactive_count++] = obj;
    return 0;
}

/**
 * @brief 撤销服务与 WLAN 已见缓存，立即重新投影权威值。
 * @param[in,out] content 持久内容状态，NULL 时忽略。
 */
static void product_ui_control_center_content_refresh_force(
    product_ui_control_center_content_t *content)
{
    if (content == NULL) {
        return;
    }
    /* 跳过已见代次缓存，重新投影各所有者的真实值。 */
    content->refresh_initialized = 0U;
    content->wifi_cache_initialized = 0U;
    product_ui_control_center_content_refresh(content);
}

/**
 * @brief 在完全打开且有效拖动松手时受理亮度／音量，取消或失败恢复真实投影。
 * @param[in] event 两滑条的释放、丢失或 RESET 事件，用户数据借用内容状态。
 * @note RESET 即使面板已关闭也先完成控件收尾；使用对应输入等待真实松手，不能复活旧拖动。成功只表示对应服务受理。
 */
static void product_ui_slider_release_cb(lv_event_t *event)
{
    product_ui_control_center_content_t *content;
    lv_obj_t *slider;
    product_ui_settings_command_e command;
    uint8_t index;
    int result;

    slider = lv_event_get_target(event);
    content = lv_event_get_user_data(event);
    if ((slider == NULL) || (content == NULL)) {
        return;
    }
    for (index = 0; index < 2U; index++) {
        if (content->sliders[index].root == slider) {
            break;
        }
    }
    if (index >= 2U) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_INDEV_RESET) {
        product_ui_control_wait_release(event);
        /* 面板刚关闭或能力刚撤销时，也须完成 RESET 控件收尾。 */
        content->sliders[index].is_dragging = 0U;
        lv_obj_send_event(slider, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
        return;
    }
    if (!content->is_fully_open || lv_obj_has_state(slider, LV_STATE_DISABLED)) {
        return;
    }
    if (!content->sliders[index].is_dragging) {
        product_ui_control_center_content_refresh_force(content);
        return;
    }
    content->sliders[index].is_dragging = 0U;
    if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST ||
        lv_event_get_code(event) == LV_EVENT_INDEV_RESET) {
        product_ui_control_center_content_refresh_force(content);
        return;
    }
    if (index == 0U) {
        command = PRODUCT_UI_SETTINGS_SET_BRIGHTNESS;
    } else {
        command = PRODUCT_UI_SETTINGS_SET_VOLUME;
    }
    result = product_ui_settings_service_command(
        command, (uint8_t)lv_slider_get_value(slider));
    if (result != 0) {
        /* LVGL 已经移动滑块，命令失败时从真实快照恢复。 */
        product_ui_control_center_content_refresh_force(content);
        PRODUCT_UI_TRACE("slider_command name=%s result=%d\n", index == 0U ? "brightness" : "volume", result);
        product_ui_toast_show("设置失败", 1200U);
        return;
    }
    PRODUCT_UI_TRACE("slider_change name=%s value=%d\n", index == 0U ? "brightness" : "volume",
                     lv_slider_get_value(slider));
    product_ui_control_center_content_refresh_force(content);
}

/**
 * @brief 创建初始位于屏幕上方且隐藏的面板布局 host。
 * @param[in,out] panel 父面板。
 * @param[in,out] content 接收布局引用的持久状态。
 * @return 成功为 0，创建失败为 -1。
 */
static int product_ui_layout_create(
    lv_obj_t *panel, product_ui_control_center_content_t *content)
{
    content->layout = lv_obj_create(panel);
    if (content->layout == NULL) {
        return -1;
    }
    product_ui_control_clean(content->layout);
    lv_obj_clear_flag(content->layout, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(content->layout, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(content->layout, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(content->layout, 0, -PRODUCT_UI_SCREEN_H);
    lv_obj_set_style_bg_opa(content->layout, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(content->layout, LV_OBJ_FLAG_HIDDEN);
    return 0;
}

/**
 * @brief 创建不透明全屏背景及底部握柄，背景不抢占空白点击。
 * @param[in,out] content 已有布局的持久状态。
 * @return 完整创建为 0，任一步失败为 -1。
 */
static int product_ui_frame_create(product_ui_control_center_content_t *content)
{
    lv_obj_t *grip;
    content->frame = lv_obj_create(content->layout);
    if (content->frame == NULL) {
        return -1;
    }
    product_ui_control_clean(content->frame);
    lv_obj_set_size(content->frame, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(content->frame, 0, 0);
    lv_obj_set_style_bg_color(
        content->frame, product_ui_color(PRODUCT_UI_COLOR_CONTROL_SCRIM), 0);
    lv_obj_set_style_bg_opa(content->frame, LV_OPA_COVER, 0);
    lv_obj_clear_flag(content->frame, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE);
    grip = lv_obj_create(content->frame);
    if (grip == NULL) return -1;
    product_ui_control_clean(grip);
    lv_obj_set_size(grip, 36, 3);
    lv_obj_set_pos(grip, 142, 230);
    lv_obj_set_style_radius(grip, 2, 0);
    lv_obj_set_style_bg_color(grip, product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
    lv_obj_set_style_bg_opa(grip, LV_OPA_50, 0);
    lv_obj_clear_flag(grip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    return 0;
}

/**
 * @brief 创建媒体卡内的三项传输控制并登记独立命中区。
 * @param[in,out] content 已有布局的持久状态。
 * @return 完整创建／登记为 0，失败为 -1。
 */
static int product_ui_media_create(product_ui_control_center_content_t *content)
{
    lv_obj_t *transport;

    memset(&content->media, 0, sizeof(content->media));
    content->media_card = product_ui_media_card_create(
        content->layout, PRODUCT_UI_CC_MEDIA_W, PRODUCT_UI_CC_MEDIA_H,
        &content->media);
    if (content->media_card == NULL) {
        return -1;
    }
    lv_obj_set_pos(content->media_card, PRODUCT_UI_CC_MEDIA_X,
                   PRODUCT_UI_CC_MEDIA_Y);

    /* 传输条以媒体卡为父对象，局部坐标不能脱出媒体壳体。 */
    transport = product_ui_media_transport_create(content->media_card,
                                                  &content->media);
    if (transport == NULL) {
        return -1;
    }
    product_ui_media_transport_bind(&content->media, product_ui_media_action_cb,
                                    content);
    if ((product_ui_control_record_interactive(
             content, content->media.previous) != 0) ||
        (product_ui_control_record_interactive(
             content, content->media.play) != 0) ||
        (product_ui_control_record_interactive(
             content, content->media.next) != 0)) {
        return -1;
    }
    return 0;
}

/**
 * @brief 完全打开且真实服务允许时分发具名媒体命令，再按快照刷新。
 * @param[in] event 媒体按钮点击事件，用户数据借用内容状态。
 * @note 禁用／不可用时吞掉动作，不伪造播放态、不关闭面板；服务返回成功只按其受理合同解释。
 */
static void product_ui_media_action_cb(lv_event_t *event)
{
    product_ui_control_center_content_t *content;
    lv_obj_t *target;
    uint16_t command;

    content = lv_event_get_user_data(event);
    target = lv_event_get_target(event);
    if ((content == NULL) || (target == NULL) || !content->is_fully_open) {
        return;
    }
    /* 不可用时只吞掉动作，不提示、不关闭、不伪造播放。 */
    if (lv_obj_has_state(target, LV_STATE_DISABLED) ||
        !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    command = (uint16_t)(uintptr_t)lv_obj_get_user_data(target);
    if (!product_ui_service_command_available(PRODUCT_UI_SERVICE_MEDIA) ||
        !product_ui_service_is_ready(PRODUCT_UI_SERVICE_MEDIA)) {
        PRODUCT_UI_TRACE("media_action command=%u result=unavailable\n",
                         command);
        return;
    }
    if (product_ui_service_command(PRODUCT_UI_SERVICE_MEDIA,
                                   command, NULL, 0U) != 0) {
        product_ui_control_center_content_refresh_force(content);
        return;
    }
    product_ui_control_center_content_refresh_force(content);
    PRODUCT_UI_TRACE("media_action command=%u result=ok\n", command);
}

/**
 * @brief 先清零输出，仅在设置服务 READY 且有命令处理器时读取快照。
 * @param[out] snapshot 接收值快照，失败时不得视为有效能力。
 * @return 成功为 0，其余为 -1。
 */
static int product_ui_control_settings_snapshot(
    product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return -1;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!product_ui_service_is_ready(PRODUCT_UI_SERVICE_SETTINGS) ||
        !product_ui_service_command_available(PRODUCT_UI_SERVICE_SETTINGS)) {
        return -1;
    }
    return product_ui_settings_service_snapshot(snapshot);
}

/**
 * @brief 检查快照读取成功且指定能力与有效位同时存在。
 * @param[in] snapshot 借用设置快照。
 * @param[in] snapshot_result 快照读取结果。
 * @param[in] cap 待检查单个能力位。
 * @return 满足条件返回 1，否则为 0。
 */
static uint8_t product_ui_slider_cap_ok(
    const product_ui_settings_snapshot_t *snapshot, int snapshot_result,
    uint32_t cap)
{
    if ((snapshot_result != 0) || (snapshot == NULL)) {
        return 0U;
    }
    if (((snapshot->capability_mask & cap) == 0U) ||
        ((snapshot->valid_mask & cap) == 0U)) {
        return 0U;
    }
    return 1U;
}

/**
 * @brief 撤销拖动，保留深色壳体并禁用点击，只弱化或隐藏数值、填充及图标。
 * @param[in,out] view 持久滑条视图。
 */
static void product_ui_slider_show_unavailable(product_ui_slider_view_t *view)
{
    if ((view == NULL) || (view->root == NULL)) {
        return;
    }
    view->is_dragging = 0U;
    /* 保留不透明深色壳体，只弱化填充及图标。 */
    lv_obj_clear_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(view->root, LV_STATE_DISABLED);
    lv_obj_clear_flag(view->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(view->root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(view->root, LV_OPA_COVER, LV_PART_MAIN);
    product_ui_slider_apply_value(view, 0);
    if (view->fill != NULL) {
        lv_obj_add_flag(view->fill, LV_OBJ_FLAG_HIDDEN);
    }
    if (view->icon != NULL) {
        lv_obj_set_style_image_recolor(
            view->icon,
            product_ui_color(PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED), 0);
        lv_obj_set_style_image_recolor_opa(view->icon, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(view->icon, LV_OPA_50, 0);
    }
    if (view->value_label != NULL) {
        lv_label_set_text_fmt(view->value_label, "%s\n--", view->name);
        lv_obj_set_style_opa(view->value_label, LV_OPA_50, 0);
        lv_obj_add_flag(view->value_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 依据设置快照创建亮度／音量竖滑条，绑定松手及重置处理。
 * @param[in,out] content 持久内容状态。
 * @return 完整创建／登记为 0，失败为 -1。
 */
static int product_ui_sliders_create(
    product_ui_control_center_content_t *content)
{
    product_ui_settings_snapshot_t snapshot;
    lv_obj_t *brightness;
    lv_obj_t *volume;
    int snapshot_result;
    uint8_t bri_ok;
    uint8_t vol_ok;

    snapshot_result = product_ui_control_settings_snapshot(&snapshot);
    bri_ok = product_ui_slider_cap_ok(&snapshot, snapshot_result,
                                      PRODUCT_UI_SETTING_CAP_BRIGHTNESS);
    vol_ok = product_ui_slider_cap_ok(&snapshot, snapshot_result,
                                      PRODUCT_UI_SETTING_CAP_VOLUME);

    brightness = product_ui_vertical_slider_create(
        content->layout, PRODUCT_UI_CC_SLIDER_W, PRODUCT_UI_CC_SLIDER_H,
        PRODUCT_UI_ICON_CC_BRIGHTNESS,
        bri_ok ? snapshot.brightness_percent : 0,
        &content->sliders[0]);
    volume = product_ui_vertical_slider_create(
        content->layout, PRODUCT_UI_CC_SLIDER_W, PRODUCT_UI_CC_SLIDER_H,
        PRODUCT_UI_ICON_CC_VOLUME,
        vol_ok ? product_ui_settings_volume_display(&snapshot) : 0,
        &content->sliders[1]);
    if ((brightness == NULL) || (volume == NULL)) {
        return -1;
    }
    lv_obj_set_pos(brightness, PRODUCT_UI_CC_SLIDER_0_X, PRODUCT_UI_CC_SLIDER_Y);
    lv_obj_set_pos(volume, PRODUCT_UI_CC_SLIDER_1_X, PRODUCT_UI_CC_SLIDER_Y);
#if defined(PRODUCT_UI_BRIGHTNESS_MIN_PERCENT)
    lv_slider_set_range(brightness,
                        (int32_t)PRODUCT_UI_BRIGHTNESS_MIN_PERCENT, 100);
#endif
    if ((product_ui_control_record_interactive(content, brightness) != 0) ||
        (product_ui_control_record_interactive(content, volume) != 0)) {
        return -1;
    }
    lv_obj_add_event_cb(brightness, product_ui_slider_release_cb,
                        LV_EVENT_RELEASED, content);
    lv_obj_add_event_cb(volume, product_ui_slider_release_cb,
                        LV_EVENT_RELEASED, content);
    lv_obj_add_event_cb(brightness, product_ui_slider_release_cb,
                        LV_EVENT_PRESS_LOST, content);
    lv_obj_add_event_cb(volume, product_ui_slider_release_cb,
                        LV_EVENT_PRESS_LOST, content);
    lv_obj_add_event_cb(brightness, product_ui_slider_release_cb,
                        LV_EVENT_INDEV_RESET, content);
    lv_obj_add_event_cb(volume, product_ui_slider_release_cb,
                        LV_EVENT_INDEV_RESET, content);

    if (bri_ok) {
        product_ui_slider_apply_value(
            &content->sliders[0], lv_slider_get_value(brightness));
    } else {
        product_ui_slider_show_unavailable(&content->sliders[0]);
    }
    if (vol_ok) {
        product_ui_slider_apply_value(
            &content->sliders[1], lv_slider_get_value(volume));
    } else {
        product_ui_slider_show_unavailable(&content->sliders[1]);
    }
    return 0;
}

/**
 * @brief WLAN 卡完全打开且可点击时导航到屏幕配网页。
 * @param[in] event WLAN 卡点击事件。
 * @note 离线仍可进入配网设置，本内容层不执行扫描或连接。
 */
static void product_ui_control_wifi_open_cb(lv_event_t *event)
{
    product_ui_control_center_content_t *content = lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);

    if ((content == NULL) || !content->is_fully_open ||
        (target != content->wifi_card.root) ||
        lv_obj_has_state(target, LV_STATE_DISABLED) ||
        !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    /* 离线时配网导航仍有用，连接交互由详情页拥有。 */
    if (product_ui_open_module("product_settings_wifi") != 0) {
        product_ui_toast_show("Wi-Fi 设置打开失败", 1200U);
    }
}

/**
 * @brief 创建 WLAN 信息卡并登记为可导航的主动命中对象。
 * @param[in,out] content 持久内容状态。
 * @return 完整创建／登记为 0，失败为 -1。
 */
static int product_ui_info_cards_create(
    product_ui_control_center_content_t *content)
{
    if (product_ui_info_card_create(
            content->layout, PRODUCT_UI_CC_WIFI_W, PRODUCT_UI_CC_WIFI_H,
            PRODUCT_UI_ICON_CC_WIFI, "WLAN", &content->wifi_card) == NULL) {
        return -1;
    }
    lv_obj_set_pos(content->wifi_card.root, PRODUCT_UI_CC_WIFI_X,
                   PRODUCT_UI_CC_WIFI_Y);
    if (product_ui_control_record_interactive(content, content->wifi_card.root) !=
        0) {
        return -1;
    }
    lv_obj_add_style(content->wifi_card.root,
                     product_ui_style(PRODUCT_UI_STYLE_CONTROL_PRESSED),
                     LV_STATE_PRESSED);
    lv_obj_add_event_cb(content->wifi_card.root, product_ui_control_wifi_open_cb,
                        LV_EVENT_CLICKED, content);

    return 0;
}

/**
 * @brief 读取设置、媒体、闹钟与系统值，比较并记录四个已见服务代次。
 * @param[in,out] content 持有已见代次缓存的内容状态。
 * @param[out] services 接收本轮四域快照，调用方先清零。
 * @return 首次或任一代次变化返回 1，无变化或空参数返回 0。
 */
static uint8_t product_ui_control_services_update(
    product_ui_control_center_content_t *content,
    product_ui_control_services_t *services)
{
    if ((content == NULL) || (services == NULL)) {
        return 0U;
    }
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_SETTINGS,
                                      &services->settings);
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_MEDIA,
                                      &services->media);
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_ALARM,
                                      &services->alarm);
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_SYSTEM,
                                      &services->system);
    if (content->refresh_initialized &&
        (content->settings_revision == services->settings.revision) &&
        (content->media_revision == services->media.revision) &&
        (content->alarm_revision == services->alarm.revision) &&
        (content->system_revision == services->system.revision)) {
        return 0U;
    }
    content->settings_revision = services->settings.revision;
    content->media_revision = services->media.revision;
    content->alarm_revision = services->alarm.revision;
    content->system_revision = services->system.revision;
    content->refresh_initialized = 1U;
    return 1U;
}

/**
 * @brief 按请求编号及错误阶段去重显示音量超时或失败提示。
 * @param[in,out] content 持有最近提示身份的内容状态。
 * @param[in] snapshot 借用的设置快照，可为空。
 */
static void product_ui_control_volume_feedback(
    product_ui_control_center_content_t *content,
    const product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->volume_request_id == 0U) return;
    if (snapshot->volume_state != PRODUCT_UI_VOLUME_TIMEOUT &&
        !(snapshot->volume_state == PRODUCT_UI_VOLUME_ERROR &&
          snapshot->volume_completed_id == snapshot->volume_request_id)) return;
    if (content->volume_feedback_request == snapshot->volume_request_id &&
        content->volume_feedback_state == snapshot->volume_state) return;
    content->volume_feedback_request = snapshot->volume_request_id;
    content->volume_feedback_state = (uint8_t)snapshot->volume_state;
    product_ui_toast_show(product_ui_settings_volume_status(snapshot), 1800U);
}

/**
 * @brief 按真实能力更新滑条确认值、使能及在途／错误文字，音量准入关闭时撤销拖动。
 * @param[in,out] content 持久内容状态。
 * @param[in] snapshot 借用设置快照。
 * @param[in] settings_enabled 设置快照是否可用。
 */
static void product_ui_control_sliders_refresh(
    product_ui_control_center_content_t *content,
    const product_ui_settings_snapshot_t *snapshot,
    uint8_t settings_enabled)
{
    static const uint32_t slider_caps[] = {
        PRODUCT_UI_SETTING_CAP_BRIGHTNESS,
        PRODUCT_UI_SETTING_CAP_VOLUME,
    };
    uint8_t slider_values[2];
    uint8_t index;
    uint8_t cap_ok;

    if (content == NULL) {
        return;
    }
    slider_values[0] = 0U;
    slider_values[1] = 0U;
    for (index = 0; index < 2U; index++) {
        if (content->sliders[index].root == NULL) {
            continue;
        }
        cap_ok = 0U;
        if ((settings_enabled != 0U) && (snapshot != NULL) &&
            ((snapshot->capability_mask & slider_caps[index]) != 0U) &&
            ((snapshot->valid_mask & slider_caps[index]) != 0U)) {
            cap_ok = 1U;
            if (index == 0U) {
                slider_values[0] = snapshot->brightness_percent;
#if defined(PRODUCT_UI_BRIGHTNESS_MIN_PERCENT)
                if (slider_values[0] < PRODUCT_UI_BRIGHTNESS_MIN_PERCENT) {
                    slider_values[0] = PRODUCT_UI_BRIGHTNESS_MIN_PERCENT;
                }
#endif
            } else {
                slider_values[1] = product_ui_settings_volume_display(snapshot);
            }
        }
        if (cap_ok) {
            if (index == 1U && !product_ui_settings_volume_can_adjust(snapshot))
                content->sliders[index].is_dragging = 0U;
            lv_obj_clear_flag(content->sliders[index].root, LV_OBJ_FLAG_HIDDEN);
            product_ui_control_set_enabled(
                content, content->sliders[index].root,
                index == 0U || product_ui_settings_volume_can_adjust(snapshot));
            lv_obj_set_style_opa(content->sliders[index].root, LV_OPA_COVER,
                                 0);
            if (content->sliders[index].icon != NULL) {
                lv_obj_set_style_opa(content->sliders[index].icon,
                                     LV_OPA_COVER, 0);
                lv_obj_set_style_image_recolor_opa(
                    content->sliders[index].icon, LV_OPA_COVER, 0);
            }
            if (content->sliders[index].fill != NULL) {
                lv_obj_clear_flag(content->sliders[index].fill,
                                  LV_OBJ_FLAG_HIDDEN);
            }
            product_ui_slider_apply_value(&content->sliders[index],
                                          (int32_t)slider_values[index]);
            if (index == 1U && !content->sliders[index].is_dragging) {
                lv_obj_t *value_label = content->sliders[index].value_label;
                if (value_label != NULL && snapshot->volume_state == PRODUCT_UI_VOLUME_APPLYING)
                    lv_label_set_text_static(value_label, "音量\n...");
                else if (value_label != NULL &&
                         (snapshot->volume_state == PRODUCT_UI_VOLUME_TIMEOUT ||
                          snapshot->volume_state == PRODUCT_UI_VOLUME_ERROR))
                    lv_label_set_text_static(value_label, "音量\n!");
            }
        } else {
            product_ui_slider_show_unavailable(&content->sliders[index]);
        }
    }
    product_ui_control_volume_feedback(content, snapshot);
}

/**
 * @brief 把服务 READY、命令绑定及传输／播放位分别投影为文字、图标与控件能力。
 * @param[in,out] content 持久内容状态。
 * @param[in] media_service 借用媒体服务值。
 */
static void product_ui_control_media_refresh(
    product_ui_control_center_content_t *content,
    const product_ui_service_snapshot_t *media_service)
{
    product_ui_media_service_view_t service_view;
    product_ui_media_cc_view_t view;

    if ((content == NULL) || (media_service == NULL)) {
        return;
    }
    /* 注册表快照使用 state 与 valid_mask，没有独立 capability_mask。 */
    service_view.service_ready =
        (uint8_t)(media_service->state == PRODUCT_UI_SERVICE_READY);
    service_view.command_available =
        (uint8_t)product_ui_service_command_available(PRODUCT_UI_SERVICE_MEDIA);
    service_view.valid_mask = media_service->valid_mask;
    product_ui_media_cc_project(&service_view, &view);

    if (view.is_unavailable == 0U) {
        if (view.show_pause_icon != 0U) {
            product_ui_media_card_set_text(&content->media, "本地音乐", "播放中");
        } else {
            product_ui_media_card_set_text(&content->media, "本地音乐", "未播放");
        }
        product_ui_media_card_set_playing(&content->media, view.show_pause_icon);
    } else {
        /* 不可用时显示未接入，不伪造播放图标状态。 */
        product_ui_media_card_set_text(&content->media, "音乐", "未接入");
        product_ui_media_card_set_playing(&content->media, 0U);
    }
    /* 控件几何仍占命中区，禁用动作被吞掉，不误关遮罩。 */
    product_ui_control_set_enabled(content, content->media.previous,
                                   view.transport_enabled);
    product_ui_control_set_enabled(content, content->media.play,
                                   view.transport_enabled);
    product_ui_control_set_enabled(content, content->media.next,
                                   view.transport_enabled);
    /* 不可用传输条整体弱化，READY 时恢复不透明。 */
    if (content->media.transport != NULL) {
        lv_obj_set_style_opa(content->media.transport,
                             (view.transport_enabled != 0U) ? LV_OPA_COVER :
                                                              LV_OPA_50,
                             0);
    }
}

/**
 * @brief 投影闹钟导航与翻转能力，激活外观只由状态位决定。
 * @param[in,out] content 持久内容状态。
 * @param[in] services 借用四域服务值。
 * @note 计时或保存失败时仍允许打开已注册闹钟页；设置为瞬时导航，不保留激活选中态。
 */
static void product_ui_control_runtime_quick_refresh(
    product_ui_control_center_content_t *content,
    const product_ui_control_services_t *services)
{
    uint8_t alarm_ready;
    uint8_t system_ready;
    product_ui_control_state_e alarm_state;
    product_ui_control_state_e system_state;

    if ((content == NULL) || (services == NULL)) {
        return;
    }
    /* 能力位控制可操作性，状态位只控制激活外观；
     * 有能力但未激活的控件仍可点击。 */
    /* 计时或保存不可用时，打开闹钟列表仍有用。 */
    alarm_ready = product_ui_module_available("alarm");
    system_ready =
        (uint8_t)(product_ui_service_command_available(
                      PRODUCT_UI_SERVICE_SYSTEM) &&
                  ((services->system.valid_mask &
                    PRODUCT_UI_SYSTEM_CAP_ROTATION) != 0U));
    if (alarm_ready == 0U) {
        alarm_state = PRODUCT_UI_CONTROL_UNAVAILABLE;
    } else if ((services->alarm.valid_mask &
                PRODUCT_UI_ALARM_STATE_ACTIVE) != 0U) {
        alarm_state = PRODUCT_UI_CONTROL_ACTIVE;
    } else {
        alarm_state = PRODUCT_UI_CONTROL_INACTIVE;
    }
    if (system_ready == 0U) {
        system_state = PRODUCT_UI_CONTROL_UNAVAILABLE;
    } else if ((services->system.valid_mask &
                PRODUCT_UI_SYSTEM_STATE_ROTATION_ON) != 0U) {
        system_state = PRODUCT_UI_CONTROL_ACTIVE;
    } else {
        system_state = PRODUCT_UI_CONTROL_INACTIVE;
    }
    product_ui_quick_card_set_state(&content->quick_cards[PRODUCT_UI_QUICK_TILE_ALARM], alarm_state);
    product_ui_control_set_enabled(content, content->quick_cards[PRODUCT_UI_QUICK_TILE_ALARM].root,
                                   alarm_ready);
    product_ui_quick_card_set_state(&content->quick_cards[PRODUCT_UI_QUICK_TILE_ROTATION], system_state);
    product_ui_control_set_enabled(content, content->quick_cards[PRODUCT_UI_QUICK_TILE_ROTATION].root,
                                   system_ready);
    /* 设置是瞬时导航，不保留激活蓝色选中态。 */
    product_ui_quick_card_set_state(&content->quick_cards[PRODUCT_UI_QUICK_TILE_SETTINGS],
                                    PRODUCT_UI_CONTROL_INACTIVE);
    product_ui_control_set_enabled(content, content->quick_cards[PRODUCT_UI_QUICK_TILE_SETTINGS].root, 1U);
}

/**
 * @brief 只消费连接域快照并比较 WLAN 显示字段，不调用生产者或 WiFi SDK。
 * @param[in,out] content 持有 WLAN 值缓存的内容状态。
 * @return 首次或内容变化返回 1，无变化／空内容返回 0。
 */
static uint8_t product_ui_control_wifi_changed(
    product_ui_control_center_content_t *content)
{
    product_ui_connection_snapshot_t conn = {0};
    product_ui_cc_wifi_cache_t next;

    if (content == NULL) {
        return 0U;
    }
    /* 生产者在公共 UI 周期同步，本视图只消费值快照。 */
    memset(&next, 0, sizeof(next));
    if (product_ui_connection_service_snapshot(&conn) == 0) {
        next.service_version = conn.service_version;
        next.capability_mask = conn.wifi_capable;
        next.valid_mask = conn.wifi_capable;
        next.has_saved_credentials = conn.has_saved_credentials;
        next.conn_state = conn.conn_state;
        next.conn_error = conn.conn_error;
        next.ready = conn.ready;
    }
    if ((content->wifi_cache_initialized != 0U) &&
        (content->wifi_cache.ready == next.ready) &&
        (content->wifi_cache.service_version == next.service_version) &&
        (content->wifi_cache.capability_mask == next.capability_mask) &&
        (content->wifi_cache.valid_mask == next.valid_mask) &&
        (content->wifi_cache.has_saved_credentials ==
         next.has_saved_credentials) &&
        (content->wifi_cache.conn_state == next.conn_state) &&
        (content->wifi_cache.conn_error == next.conn_error)) {
        return 0U;
    }
    content->wifi_cache = next;
    content->wifi_cache_initialized = 1U;
    return 1U;
}

/**
 * @brief 从独立连接域状态区分不可用、已连接、连接中、待配网、未连接及失败。
 * @param[in,out] content 已有 WLAN 缓存的内容状态。
 * @note 不复用状态栏 LINK_OFF 折叠，首次无凭据 IDLE 应显示等待配网。
 */
static void product_ui_control_wifi_refresh(
    product_ui_control_center_content_t *content)
{
    product_ui_control_state_e state;
    const char *sub;
    const product_ui_cc_wifi_cache_t *wifi;

    if (content == NULL) {
        return;
    }
    /* 只从 Product UI 连接快照投影，不复用状态栏 LINK_OFF 折叠；
     * IDLE 且无凭据须保留等待配网语义。 */
    wifi = &content->wifi_cache;
    if ((wifi->ready == 0U) || (wifi->capability_mask == 0U)) {
        state = PRODUCT_UI_CONTROL_UNAVAILABLE;
        sub = "不可用";
    } else if (wifi->conn_state == (uint8_t)PRODUCT_UI_CONN_STATE_CONNECTED) {
        state = PRODUCT_UI_CONTROL_ACTIVE;
        sub = "已连接";
    } else if ((wifi->conn_state ==
                (uint8_t)PRODUCT_UI_CONN_STATE_CONNECTING) ||
               (wifi->conn_state ==
                (uint8_t)PRODUCT_UI_CONN_STATE_ASSOCIATED)) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "连接中";
    } else if ((wifi->conn_state == (uint8_t)PRODUCT_UI_CONN_STATE_IDLE) &&
               (wifi->has_saved_credentials == 0U)) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "等待配网";
    } else if ((wifi->conn_state ==
                (uint8_t)PRODUCT_UI_CONN_STATE_DISCONNECTED) &&
               (wifi->conn_error == (uint8_t)PRODUCT_UI_CONN_ERR_NONE)) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "未连接";
    } else if ((wifi->conn_state == (uint8_t)PRODUCT_UI_CONN_STATE_ERROR) ||
               ((wifi->conn_state ==
                 (uint8_t)PRODUCT_UI_CONN_STATE_DISCONNECTED) &&
                (wifi->conn_error != (uint8_t)PRODUCT_UI_CONN_ERR_NONE))) {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "连接失败";
    } else {
        state = PRODUCT_UI_CONTROL_INACTIVE;
        sub = "未连接";
    }
    product_ui_info_card_set_state(&content->wifi_card, state, sub);
}

/**
 * @brief 在三项快捷卡中查找目标对象。
 * @param[in] content 有效内容状态。
 * @param[in] target 待匹配对象。
 * @return 匹配返回逻辑下标，未找到为 -1。
 */
static int product_ui_quick_find_index(
    const product_ui_control_center_content_t *content, const lv_obj_t *target)
{
    uint8_t index;

    for (index = 0; index < PRODUCT_UI_CONTROL_QUICK_COUNT; index++) {
        if (content->quick_cards[index].root == target) {
            return index;
        }
    }
    return -1;
}

/**
 * @brief 设置业务禁用状态，并把可点击性继续限制在完全打开终态。
 * @param[in] content 提供完全打开状态的内容。
 * @param[in,out] control 借用控件，NULL 时忽略。
 * @param[in] is_enabled 非零业务允许，零禁用。
 */
static void product_ui_control_set_enabled(
    product_ui_control_center_content_t *content, lv_obj_t *control,
    uint8_t is_enabled)
{
    if ((content == NULL) || (control == NULL)) {
        return;
    }
    if (is_enabled) {
        lv_obj_remove_state(control, LV_STATE_DISABLED);
        if (content->is_fully_open) {
            lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(control, LV_OBJ_FLAG_CLICKABLE);
        }
    } else {
        lv_obj_add_state(control, LV_STATE_DISABLED);
        lv_obj_clear_flag(control, LV_OBJ_FLAG_CLICKABLE);
    }
}

/**
 * @brief 校验当前快捷卡能力，执行设置／闹钟导航或受理方向切换。
 * @param[in] event 快捷卡点击事件。
 */
static void product_ui_quick_action_cb(lv_event_t *event)
{
    product_ui_control_center_content_t *content = lv_event_get_user_data(event);
    product_ui_quick_card_view_t *view;
    int index;

    if (content == NULL || !content->is_fully_open) {
        return;
    }
    index = product_ui_quick_find_index(content, lv_event_get_target(event));
    if (index < 0) {
        return;
    }
    view = &content->quick_cards[index];
    if (lv_obj_has_state(view->root, LV_STATE_DISABLED) ||
        !lv_obj_has_flag(view->root, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    if (view->tile_id == PRODUCT_UI_QUICK_TILE_SETTINGS) {
        PRODUCT_UI_TRACE("quick_action=%s state=momentary\n",
                         view->semantic_name);
        if (product_ui_open_module("product_settings") != 0) {
            product_ui_toast_show("设置打开失败", 1200U);
        }
        return;
    }
    if (view->tile_id == PRODUCT_UI_QUICK_TILE_ALARM) {
        if (product_ui_open_module("alarm") != 0) product_ui_toast_show("闹钟打开失败", 1200U);
        return;
    } else if (view->tile_id == PRODUCT_UI_QUICK_TILE_ROTATION) {
        if (!product_ui_service_is_ready(PRODUCT_UI_SERVICE_SYSTEM) ||
            !product_ui_service_command_available(PRODUCT_UI_SERVICE_SYSTEM)) {
            return;
        }
        if (product_ui_service_command(
                PRODUCT_UI_SERVICE_SYSTEM,
                (uint16_t)PRODUCT_UI_SYSTEM_CMD_TOGGLE_ROTATION, NULL,
                0U) != 0) {
            product_ui_toast_show("翻转暂不可用，请稍后再试", 1500U);
            product_ui_control_center_content_refresh_force(content);
            return;
        }
    } else {
        return;
    }
    product_ui_control_center_content_refresh_force(content);
    PRODUCT_UI_TRACE("quick_action=%s state=%u\n",
                     view->semantic_name, view->state);
}

/**
 * @brief 按静态三项规格创建快捷卡、登记命中并绑定具名动作。
 * @param[in,out] content 持久内容状态。
 * @return 完整创建／登记为 0，失败为 -1。
 */
static int product_ui_quick_create(product_ui_control_center_content_t *content)
{
    static const int16_t s_quick_x[PRODUCT_UI_CONTROL_QUICK_COUNT] = {
        PRODUCT_UI_CC_QUICK0_X, PRODUCT_UI_CC_QUICK1_X,
        PRODUCT_UI_CC_QUICK2_X,
    };
    lv_obj_t *button;
    uint8_t index;

    for (index = 0; index < PRODUCT_UI_CONTROL_QUICK_COUNT; index++) {
        button = product_ui_quick_card_create(
            content->layout, &s_quick_specs[index],
            &content->quick_cards[index]);
        if (button == NULL) {
            return -1;
        }
        lv_obj_set_pos(button, s_quick_x[index], PRODUCT_UI_CC_QUICK_Y);
        if (product_ui_control_record_interactive(content, button) != 0) {
            return -1;
        }
        lv_obj_add_event_cb(button, product_ui_quick_action_cb,
                            LV_EVENT_CLICKED, content);
    }
    product_ui_quick_card_set_state(
        &content->quick_cards[PRODUCT_UI_QUICK_TILE_ALARM],
        product_ui_service_is_ready(PRODUCT_UI_SERVICE_ALARM) ?
            PRODUCT_UI_CONTROL_INACTIVE : PRODUCT_UI_CONTROL_UNAVAILABLE);
    product_ui_quick_card_set_state(
        &content->quick_cards[PRODUCT_UI_QUICK_TILE_ROTATION],
        product_ui_service_is_ready(PRODUCT_UI_SERVICE_SYSTEM) ?
            PRODUCT_UI_CONTROL_INACTIVE : PRODUCT_UI_CONTROL_UNAVAILABLE);
    product_ui_quick_card_set_state(&content->quick_cards[PRODUCT_UI_QUICK_TILE_SETTINGS],
                                    PRODUCT_UI_CONTROL_INACTIVE);
    return 0;
}

/**
 * @brief 检查屏幕点是否位于可见对象的闭区间几何内。
 * @param[in] obj 借用对象。
 * @param[in] x 屏幕横坐标。
 * @param[in] y 屏幕纵坐标。
 * @return 命中为 1，空对象、隐藏或不在范围为 0。
 */
static uint8_t product_ui_control_point_in_obj(const lv_obj_t *obj,
                                              int16_t x, int16_t y)
{
    lv_area_t area;

    if ((obj == NULL) || lv_obj_has_flag((lv_obj_t *)obj, LV_OBJ_FLAG_HIDDEN)) {
        return 0U;
    }
    lv_obj_get_coords((lv_obj_t *)obj, &area);
    if ((x >= area.x1) && (x <= area.x2) &&
        (y >= area.y1) && (y <= area.y2)) {
        return 1U;
    }
    return 0U;
}

/**
 * @brief 完全打开时按对象几何判断点是否属于媒体卡、传输条及已登记命中对象。
 */
uint8_t product_ui_control_center_content_hit_controls(
    const product_ui_control_center_content_t *content, int16_t x, int16_t y)
{
    uint8_t index;

    if ((content == NULL) || (content->is_fully_open == 0U)) {
        return 0U;
    }
    if (product_ui_control_point_in_obj(content->media_card, x, y) != 0U) {
        return 1U;
    }
    if (product_ui_control_point_in_obj(content->media.transport, x, y) !=
        0U) {
        return 1U;
    }
    for (index = 0; index < content->passive_hit_count; index++) {
        if (product_ui_control_point_in_obj(content->passive_hit[index],
                                            x, y) != 0U) {
            return 1U;
        }
    }
    for (index = 0; index < content->interactive_count; index++) {
        if (product_ui_control_point_in_obj(content->interactive[index],
                                            x, y) != 0U) {
            return 1U;
        }
    }
    return 0U;
}

/**
 * @brief 消费注册表及连接域值，内容代次变化时更新相关显示组。
 */
void product_ui_control_center_content_refresh(
    product_ui_control_center_content_t *content)
{
    product_ui_settings_snapshot_t snapshot = {0};
    product_ui_control_services_t services = {0};
    uint8_t settings_enabled;
    uint8_t service_changed;
    uint8_t wifi_changed;
    int settings_snap_rc;

    if (content == NULL) {
        return;
    }
    service_changed = product_ui_control_services_update(content, &services);
    wifi_changed = product_ui_control_wifi_changed(content);

    if ((service_changed == 0U) && (wifi_changed == 0U)) {
        return;
    }
    if (service_changed != 0U) {
        settings_enabled = 0U;
        if (services.settings.state == PRODUCT_UI_SERVICE_READY) {
            settings_snap_rc = product_ui_control_settings_snapshot(&snapshot);
            settings_enabled = (uint8_t)(settings_snap_rc == 0);
        }
        product_ui_control_sliders_refresh(content, &snapshot, settings_enabled);
        product_ui_control_media_refresh(content, &services.media);
        product_ui_control_runtime_quick_refresh(content, &services);
    }
    if (wifi_changed != 0U) {
        product_ui_control_wifi_refresh(content);
    }
}

/**
 * @brief 创建布局、背景、WLAN、媒体、两滑条及三快捷控件，统一登记命中集合。
 */
int product_ui_control_center_content_create(
    lv_obj_t *panel, product_ui_control_center_content_t *content)
{
    if ((panel == NULL) || (content == NULL)) {
        return -1;
    }
    memset(content, 0, sizeof(*content));
    if ((product_ui_layout_create(panel, content) != 0) ||
        (product_ui_frame_create(content) != 0) ||
        (product_ui_info_cards_create(content) != 0) ||
        (product_ui_media_create(content) != 0) ||
        (product_ui_sliders_create(content) != 0) ||
        (product_ui_quick_create(content) != 0)) {
        /* 清除部分布局，不能留下半成品交互集合。 */
        if (content->layout != NULL) {
            lv_obj_delete(content->layout);
        }
        memset(content, 0, sizeof(*content));
        return -1;
    }
    product_ui_control_center_content_refresh(content);
    product_ui_control_center_content_set_progress(content, 0);
    PRODUCT_UI_TRACE("panel_content mode=cc_solid interactive=%u passive=%u\n",
                     content->interactive_count, content->passive_hit_count);
    return 0;
}

/**
 * @brief 按千分比移动面板并只在一千终点允许控件点击。
 */
void product_ui_control_center_content_set_progress(
    product_ui_control_center_content_t *content, uint16_t progress_permille)
{
    int32_t y;
    uint8_t index;
    uint8_t is_interactive;
    uint8_t was_open;

    if ((content == NULL) || (content->layout == NULL)) {
        return;
    }
    if (progress_permille > 1000U) {
        progress_permille = 1000U;
    }
    y = -((int32_t)PRODUCT_UI_SCREEN_H) +
        (((int32_t)PRODUCT_UI_SCREEN_H * (int32_t)progress_permille) / 1000);
    lv_obj_set_y(content->layout, y);

    was_open = content->is_fully_open;
    if (progress_permille == 0U) {
        lv_obj_add_flag(content->layout, LV_OBJ_FLAG_HIDDEN);
        content->is_open_visual = 0U;
        /* 下一次完全打开时强制重新投影服务值。 */
        content->refresh_initialized = 0U;
    } else {
        lv_obj_clear_flag(content->layout, LV_OBJ_FLAG_HIDDEN);
        content->is_open_visual = 1U;
    }

    is_interactive = progress_permille == 1000U;
    content->is_fully_open = is_interactive;
    if (!is_interactive) {
        for (index = 0U; index < 2U; index++) {
            if (content->sliders[index].is_dragging) {
                content->sliders[index].is_dragging = 0U;
                product_ui_slider_apply_value(&content->sliders[index],
                                               content->sliders[index].confirmed_value);
            }
        }
    }
    if (content->layout != NULL) {
        lv_obj_clear_flag(content->layout, LV_OBJ_FLAG_CLICKABLE);
    }
    /* 等打开手势完成后再刷新禁用状态，否则可能在 RELEASED 之前
     * 触发 LVGL 活跃输入重置。 */
    for (index = 0; index < content->interactive_count; index++) {
        if (is_interactive &&
            !lv_obj_has_state(content->interactive[index], LV_STATE_DISABLED)) {
            lv_obj_add_flag(content->interactive[index], LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(content->interactive[index],
                              LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (is_interactive && (was_open == 0U)) {
        product_ui_visual_capture_request("CC-04");
        PRODUCT_UI_TRACE("panel_content_open y=%d interactive=1\n", (int)y);
    } else if ((progress_permille == 0U) && (was_open != 0U)) {
        PRODUCT_UI_TRACE("panel_content_closed\n");
    }
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
