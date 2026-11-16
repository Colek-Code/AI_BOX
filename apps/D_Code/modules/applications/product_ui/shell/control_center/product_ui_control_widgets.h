/** @file product_ui_control_widgets.h @brief 控制中心深色实体控件构造与值呈现，不直接调用业务服务。 */

#ifndef __PRODUCT_UI_CONTROL_WIDGETS_H__
#define __PRODUCT_UI_CONTROL_WIDGETS_H__

#include "modules/applications/product_ui/product_ui.h"

typedef struct {
    lv_obj_t *title;
    lv_obj_t *subtitle;
    /* 卡内传输条，父对象为媒体卡壳体。 */
    lv_obj_t *transport;
    lv_obj_t *previous;
    lv_obj_t *play;
    lv_obj_t *next;
    lv_obj_t *play_glyph;
    uint8_t is_playing;
} product_ui_media_controls_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *fill;
    lv_obj_t *value_label;
    lv_obj_t *icon;
    const char *name;
    int32_t confirmed_value;
    uint8_t is_dragging;
} product_ui_slider_view_t;

typedef enum {
    PRODUCT_UI_CONTROL_UNAVAILABLE,
    PRODUCT_UI_CONTROL_INACTIVE,
    PRODUCT_UI_CONTROL_ACTIVE,
} product_ui_control_state_e;

typedef enum {
    PRODUCT_UI_QUICK_TILE_ALARM = 0,
    PRODUCT_UI_QUICK_TILE_ROTATION,
    PRODUCT_UI_QUICK_TILE_SETTINGS,
    PRODUCT_UI_QUICK_TILE_COUNT
} product_ui_quick_tile_e;

typedef struct {
    int16_t width;
    int16_t height;
    product_ui_quick_tile_e tile_id;
    const char *semantic_name;
} product_ui_quick_card_spec_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *icon;
    lv_obj_t *label;
    product_ui_quick_tile_e tile_id;
    const char *semantic_name;
    product_ui_control_state_e state;
} product_ui_quick_card_view_t;

/** 信息卡构造不绑定业务回调；内容控制器可把 WLAN 卡另绑为导航并纳入主动命中。 */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    product_ui_control_state_e state;
} product_ui_info_card_view_t;

/**
 * @brief 创建封面、标题及副标题的媒体信息卡，传输控件由独立构造器提供。
 * @param[in,out] parent 有效卡片父容器。
 * @param[in] width 至少八十像素宽度。
 * @param[in] height 至少八十像素高度。
 * @param[in,out] controls 首次清零的持久媒体引用；已有传输字段会被保留。
 * @return 成功返回父树拥有的卡片，尺寸、状态或任一创建失败返回 NULL。
 * @note 文字由标签复制，静态图标资源借用；不拥有播放服务。
 */
lv_obj_t *product_ui_media_card_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_media_controls_t *controls);

/**
 * @brief 在媒体卡内部创建三个不重叠传输命中区及居中图标。
 * @param[in,out] parent 必须为媒体卡壳体，坐标相对于卡片。
 * @param[in,out] controls 持久媒体引用，传输字段被重置后填写。
 * @return 完整创建返回传输根，空参数或子对象失败返回 NULL，失败删除局部树并清传输引用。
 * @note 无扩展点击区，命中不超出卡内行；命令编号仅存对象用户数据，控件不自行调用服务。
 */
lv_obj_t *product_ui_media_transport_create(
    lv_obj_t *parent, product_ui_media_controls_t *controls);

/**
 * @brief 向已有的上一首、播放及下一首按钮追加 CLICKED 回调。
 * @param[in] controls 有效持久媒体引用，可为空。
 * @param[in] cb 持续有效的控制器回调，NULL 时忽略。
 * @param[in] user_data 持续借用的控制器上下文。
 * @note 不替换已有回调；控制器仍负责完全打开、可用性及请求受理。
 */
void product_ui_media_transport_bind(
    product_ui_media_controls_t *controls, lv_event_cb_t cb, void *user_data);

/**
 * @brief 复制更新已有媒体标题与副标题，空字段保持原文字。
 * @param[in] controls 指向持久控件引用的描述，可为空。
 * @param[in] title 本次复制的新标题，NULL 不改。
 * @param[in] subtitle 本次复制的新副标题，NULL 不改。
 */
void product_ui_media_card_set_text(product_ui_media_controls_t *controls,
                                    const char *title,
                                    const char *subtitle);
/**
 * @brief 播放投影变化时重绑播放／暂停静态图标。
 * @param[in,out] controls 持久媒体引用及已显示播放标志。
 * @param[in] is_playing 非零显示暂停操作图标，零显示播放操作图标。
 * @note 只呈现真实服务值，不提交播放命令；资源缺失时保留原标志及图标。
 */
void product_ui_media_card_set_playing(product_ui_media_controls_t *controls,
                                       uint8_t is_playing);

/**
 * @brief 创建图标、标题及右侧状态说明的信息卡，初始不绑定点击。
 * @param[in,out] parent 有效卡片父容器。
 * @param[in] width 至少一百四十四像素。
 * @param[in] height 至少三十二像素。
 * @param[in] icon_id 已生成原生尺寸图标编号。
 * @param[in] title 本次复制的标题。
 * @param[out] view 持久控件引用，创建开始清零，须覆盖外部附着回调寿命。
 * @return 成功返回父树拥有的卡片，必要参数、尺寸、资源或创建失败返回 NULL。
 * @note 控制器可另外把 WLAN 卡绑定为导航；构造器不拥有连接服务。
 */
lv_obj_t *product_ui_info_card_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_icon_id_t icon_id, const char *title,
    product_ui_info_card_view_t *view);
/**
 * @brief 保持实体壳不透明，按激活／未激活／不可用更新内容色及可选副标题。
 * @param[in,out] view 持久信息卡引用。
 * @param[in] state 值呈现状态。
 * @param[in] subtitle 本次复制的状态说明，NULL 保留。
 * @note 不管理业务回调或最终可点击资格，交互由内容控制器决定。
 */
void product_ui_info_card_set_state(product_ui_info_card_view_t *view,
                                    product_ui_control_state_e state,
                                    const char *subtitle);

/**
 * @brief 创建零至一百的竖滑条、独立填充、图标及拖动时数值说明。
 * @param[in,out] parent 有效滑条父容器。
 * @param[in] width 由主题提供的正宽度。
 * @param[in] height 由主题提供的正高度。
 * @param[in] icon_id 亮度或音量的生成图标编号，决定静态语义名。
 * @param[in] value 初始目标值，实际滑条范围由 LVGL 钳制。
 * @param[out] view 持续状态及事件上下文，须覆盖控件寿命。
 * @return 成功返回父树拥有的滑条，空输出或任一创建失败返回 NULL，失败删除局部树。
 * @note 只绑定本地预览；松手提交、取消及维护准入由内容控制器附着。
 */
lv_obj_t *product_ui_vertical_slider_create(
    lv_obj_t *parent, int16_t width, int16_t height,
    product_ui_icon_id_t icon_id, int32_t value,
    product_ui_slider_view_t *view);
/**
 * @brief 保存权威确认值，非拖动时再应用到滑条及本地视觉。
 * @param[in,out] view 持久滑条状态。
 * @param[in] value 当前权威值；控件按自身范围约束实际位置。
 * @note 拖动期间只更新 confirmed_value，不覆盖用户预览，供取消时恢复。
 */
void product_ui_slider_apply_value(product_ui_slider_view_t *view,
                                   int32_t value);

/**
 * @brief 按静态语义规格创建快捷卡，并投影为初始未激活。
 * @param[in,out] parent 有效父容器。
 * @param[in] spec 借用规格，semantic_name 须持续有效。
 * @param[out] view 成功接收持久卡片引用；失败保持原输出。
 * @return 成功返回父树拥有的卡片；参数、未映射图标或创建失败返回 NULL，失败删除局部卡片。
 */
lv_obj_t *product_ui_quick_card_create(
    lv_obj_t *parent, const product_ui_quick_card_spec_t *spec,
    product_ui_quick_card_view_t *view);
/**
 * @brief 保持实体壳不透明，按状态更新背景及图标文字色，并记录显示状态。
 * @param[in,out] view 持久卡片引用。
 * @param[in] state 激活、未激活或不可用。
 * @note 此函数会清除 DISABLED，内容控制器随后必须按真实能力及完全打开状态再次设置交互资格；颜色状态本身不表示受理完成。
 */
void product_ui_quick_card_set_state(product_ui_quick_card_view_t *view,
                                     product_ui_control_state_e state);

#endif /* __PRODUCT_UI_CONTROL_WIDGETS_H__ */
