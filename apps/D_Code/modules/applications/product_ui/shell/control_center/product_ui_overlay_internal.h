/** @file product_ui_overlay_internal.h @brief 控制中心内容的内部创建、进度、快照消费与几何命中合同。 */

#ifndef __PRODUCT_UI_OVERLAY_INTERNAL_H__
#define __PRODUCT_UI_OVERLAY_INTERNAL_H__

#include "lvgl.h"
#include "modules/applications/product_ui/shell/control_center/product_ui_control_widgets.h"
#include "modules/applications/product_ui/runtime/status/product_ui_status_model.h"

/* 三个媒体操作、两个滑条、一个 WLAN 导航及三个快捷卡，共九个主动命中对象。 */
#define PRODUCT_UI_CONTROL_INTERACTIVE_MAX    9
#define PRODUCT_UI_CONTROL_QUICK_COUNT         PRODUCT_UI_QUICK_TILE_COUNT
#define PRODUCT_UI_CONTROL_PASSIVE_HIT_MAX    2

typedef struct {
    /* 独立 WLAN 内容投影，不套用状态栏 LINK_OFF 映射。 */
    uint32_t service_version;
    uint8_t capability_mask;
    uint8_t valid_mask;
    uint8_t has_saved_credentials;
    uint8_t conn_state;
    uint8_t conn_error;
    uint8_t ready;
} product_ui_cc_wifi_cache_t;

typedef struct {
    /* layout 是不可点击的滑动 host，空白命中交外层遮罩处理。
     * frame 是不透明全屏背景。
     * interactive[] 为三媒体、两滑条、WLAN 导航及三快捷，仅进度一千可点击。
     * passive_hit[] 预留给额外仅几何命中区域，当前创建路径不登记条目。
     */
    lv_obj_t *layout;
    lv_obj_t *frame;
    lv_obj_t *media_card;
    lv_obj_t *interactive[PRODUCT_UI_CONTROL_INTERACTIVE_MAX];
    lv_obj_t *passive_hit[PRODUCT_UI_CONTROL_PASSIVE_HIT_MAX];
    product_ui_media_controls_t media;
    product_ui_slider_view_t sliders[2];
    product_ui_quick_card_view_t quick_cards[PRODUCT_UI_CONTROL_QUICK_COUNT];
    product_ui_info_card_view_t wifi_card;
    product_ui_cc_wifi_cache_t wifi_cache;
    uint8_t wifi_cache_initialized;
    uint8_t interactive_count;
    uint8_t passive_hit_count;
    uint8_t is_open_visual;
    uint8_t is_fully_open;
    uint32_t settings_revision;
    uint32_t volume_feedback_request;
    uint8_t volume_feedback_state;
    uint32_t media_revision;
    uint32_t alarm_revision;
    uint32_t system_revision;
    uint8_t refresh_initialized;
} product_ui_control_center_content_t;

/**
 * @brief 创建布局、背景、WLAN、媒体、两滑条及三快捷控件，统一登记命中集合。
 * @param[in,out] panel 全局控制中心拥有的父容器。
 * @param[out] content 持续有效的内容状态，须覆盖子控件回调寿命。
 * @return 完整创建为 0；参数或任一步失败为 -1，失败删除局部布局并清零状态。
 * @note 创建后投影一次服务值并置关闭进度；不拥有媒体、WiFi、音量或存储后台资源。
 */
int product_ui_control_center_content_create(
    lv_obj_t *panel, product_ui_control_center_content_t *content);
/**
 * @brief 消费注册表及连接域值，内容代次变化时更新相关显示组。
 * @param[in,out] content 已创建的持久内容状态，NULL 时忽略。
 * @note 外层仅在完全打开时调用；无内部锁或跨服务原子快照。当前显示 API 不返回各标签分配结果，不把本调用当作全部像素成功呈现证明。
 */
void product_ui_control_center_content_refresh(
    product_ui_control_center_content_t *content);
/**
 * @brief 按千分比移动面板并只在一千终点允许控件点击。
 * @param[in,out] content 已创建的持久内容状态。
 * @param[in] progress_permille 零至一千进度，超过一千钳制。
 * @note 离开完全打开时撤销滑条预览并恢复确认值；关闭使下次打开强制重取服务投影。过程中只改点击资格，避免在 RELEASED 前改禁用状态触发重置。
 */
void product_ui_control_center_content_set_progress(
    product_ui_control_center_content_t *content, uint16_t progress_permille);
/**
 * @brief 完全打开时按对象几何判断点是否属于媒体卡、传输条及已登记命中对象。
 * @param[in] content 借用的内容状态。
 * @param[in] x 屏幕横坐标。
 * @param[in] y 屏幕纵坐标。
 * @return 命中返回 1，无内容、未完全打开或均未命中返回 0。
 * @note 禁用控件仍可占用几何区域，避免无意关闭遮罩；此函数不代替业务受理校验。
 */
uint8_t product_ui_control_center_content_hit_controls(
    const product_ui_control_center_content_t *content, int16_t x, int16_t y);

#endif /* __PRODUCT_UI_OVERLAY_INTERNAL_H__ */
