/** @file product_ui_frame.c @brief 页面状态栏、导航及内容框架；对象树与页面持久状态共同遵守删除生命周期。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "framework/d_page_manager.h"
#include <string.h>

static product_ui_status_view_t *s_status_views;

typedef struct {
    product_ui_status_view_t *view;
    lv_obj_t **root_out;
    uint8_t allow_pull;
    uint8_t is_light;
    uint8_t hide_connectivity;
} product_ui_status_create_params_t;

static void product_ui_status_view_apply(
    product_ui_status_view_t *view, const product_ui_status_t *status);

/**
 * @brief 从静态状态栏链解除指定状态，不删除其对象树。
 * @param[in,out] view 状态栏引用，NULL 或尚未登记也可安全忽略。
 */
static void product_ui_status_unregister(product_ui_status_view_t *view)
{
    product_ui_status_view_t **link;

    if (view == NULL) {
        return;
    }
    link = &s_status_views;
    while (*link != NULL) {
        if (*link == view) {
            *link = view->next;
            break;
        }
        link = &(*link)->next;
    }
    view->next = NULL;
    view->is_registered = 0U;
}

/**
 * @brief 清除对象默认样式及滚动能力，NULL 时忽略。
 * @param[in,out] obj 借用对象。
 */
static void product_ui_clean_obj(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 创建主题标签并复制本次文字。
 * @param[in,out] parent 标签父对象。
 * @param[in] text 本次复制的文字。
 * @param[in] style 主题样式标记。
 * @return 成功返回父树拥有的标签，失败为 NULL。
 */
static lv_obj_t *product_ui_make_label(lv_obj_t *parent, const char *text,
                                       product_ui_style_e style)
{
    lv_obj_t *label;

    label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_add_style(label, product_ui_style(style), 0);
    return label;
}

static const lv_style_prop_t s_status_icon_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_IMAGE_RECOLOR_OPA,
};
static const lv_style_const_prop_t s_status_icon_geometry[] = {
    {&s_status_icon_properties[0], {.num = PRODUCT_UI_STATUS_BT_ICON}},
    {&s_status_icon_properties[1], {.num = PRODUCT_UI_STATUS_BT_ICON}},
    {&s_status_icon_properties[2], {.num = LV_OPA_COVER}}, LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_status_icon_style, (void *)s_status_icon_geometry);

/**
 * @brief 创建指定原生状态栏尺寸的不可交互 A4 图像。
 * @param[in,out] parent 图像父对象。
 * @param[in] icon_id 静态图标编号。
 * @return 成功返回父树拥有的图像，资源缺失或创建失败为 NULL。
 */
static lv_obj_t *product_ui_make_status_icon(lv_obj_t *parent,
                                             product_ui_icon_id_t icon_id)
{
    const lv_image_dsc_t *source;
    lv_obj_t *image;

    source = product_ui_icon_get_sized(icon_id, PRODUCT_UI_STATUS_BT_ICON);
    if (source == NULL) {
        return NULL;
    }
    image = lv_image_create(parent);
    if (image == NULL) {
        return NULL;
    }
    lv_image_set_src(image, source);
    /* 生成字形及布局框使用相同原生尺寸。 */
    lv_obj_add_style(image, &s_status_icon_style, 0);
    lv_image_set_scale(image, LV_SCALE_NONE);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return image;
}

/* 状态栏 WiFi 使用 Cipher 式四根递增信号条，对应零至四格；
 * 设置行继续独立使用图标资源。 */
/* 所有页面信号几何一致，包含事务切换中同时保留的两页；
 * 用 Flash 只读样式避免重复本地样式分配。 */
static const lv_style_prop_t s_signal_geometry_props[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_X, LV_STYLE_Y, LV_STYLE_RADIUS, LV_STYLE_BORDER_WIDTH
};
#define PRODUCT_UI_SIGNAL_GEOMETRY(name, index, height) \
    static const lv_style_const_prop_t name[] = { \
        {&s_signal_geometry_props[0], {.num = PRODUCT_UI_STATUS_WIFI_BAR_W}}, \
        {&s_signal_geometry_props[1], {.num = height}}, \
        {&s_signal_geometry_props[2], {.num = index * (PRODUCT_UI_STATUS_WIFI_BAR_W + PRODUCT_UI_STATUS_WIFI_BAR_GAP)}}, \
        {&s_signal_geometry_props[3], {.num = PRODUCT_UI_STATUS_ICON_H - height}}, \
        {&s_signal_geometry_props[4], {.num = 1}}, {&s_signal_geometry_props[5], {.num = 0}}, LV_STYLE_CONST_PROPS_END \
    }
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_0, 0, PRODUCT_UI_STATUS_WIFI_BAR_H0);
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_1, 1, PRODUCT_UI_STATUS_WIFI_BAR_H1);
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_2, 2, PRODUCT_UI_STATUS_WIFI_BAR_H2);
PRODUCT_UI_SIGNAL_GEOMETRY(s_signal_3, 3, PRODUCT_UI_STATUS_WIFI_BAR_H3);
#undef PRODUCT_UI_SIGNAL_GEOMETRY
static LV_STYLE_CONST_INIT(s_signal_0_style, (void *)s_signal_0);
static LV_STYLE_CONST_INIT(s_signal_1_style, (void *)s_signal_1);
static LV_STYLE_CONST_INIT(s_signal_2_style, (void *)s_signal_2);
static LV_STYLE_CONST_INIT(s_signal_3_style, (void *)s_signal_3);
static const lv_style_t *const s_signal_styles[] = {
    &s_signal_0_style, &s_signal_1_style, &s_signal_2_style, &s_signal_3_style
};

/* 首页、控制中心及页面切换两侧复用状态几何；
 * 只有随真实状态变化的值保留为局部样式。 */
enum {
    STATUS_WIDTH, STATUS_HEIGHT, STATUS_X, STATUS_Y, STATUS_BG_OPA, STATUS_BORDER,
    STATUS_RADIUS, STATUS_PAD_LEFT, STATUS_PAD_RIGHT, STATUS_PAD_TOP, STATUS_PAD_BOTTOM,
    STATUS_ALIGN, STATUS_TEXT_ALIGN, STATUS_LAYOUT, STATUS_FLEX_FLOW, STATUS_FLEX_GROW,
    STATUS_FLEX_MAIN, STATUS_FLEX_CROSS, STATUS_FLEX_TRACK, STATUS_PAD_COLUMN,
};
static const lv_style_prop_t s_status_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_X, LV_STYLE_Y, LV_STYLE_BG_OPA, LV_STYLE_BORDER_WIDTH,
    LV_STYLE_RADIUS, LV_STYLE_PAD_LEFT, LV_STYLE_PAD_RIGHT, LV_STYLE_PAD_TOP, LV_STYLE_PAD_BOTTOM,
    LV_STYLE_ALIGN, LV_STYLE_TEXT_ALIGN, LV_STYLE_LAYOUT, LV_STYLE_FLEX_FLOW, LV_STYLE_FLEX_GROW,
    LV_STYLE_FLEX_MAIN_PLACE, LV_STYLE_FLEX_CROSS_PLACE, LV_STYLE_FLEX_TRACK_PLACE, LV_STYLE_PAD_COLUMN,
};
#define STATUS_VALUE(property, number) {&s_status_properties[STATUS_##property], {.num = (number)}}
#define STATUS_ZERO_PADDING \
    STATUS_VALUE(PAD_LEFT, 0), STATUS_VALUE(PAD_RIGHT, 0), STATUS_VALUE(PAD_TOP, 0), STATUS_VALUE(PAD_BOTTOM, 0)
static const lv_style_const_prop_t s_wifi_box_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_WIFI_BAR_COUNT * PRODUCT_UI_STATUS_WIFI_BAR_W +
                       (PRODUCT_UI_WIFI_BAR_COUNT - 1U) * PRODUCT_UI_STATUS_WIFI_BAR_GAP),
    STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_ICON_H), STATUS_VALUE(BG_OPA, LV_OPA_TRANSP),
    STATUS_VALUE(BORDER, 0), STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_host_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_BODY_W + PRODUCT_UI_STATUS_BAT_TIP_GAP + PRODUCT_UI_STATUS_BAT_TIP_W),
    STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_BODY_H), STATUS_VALUE(BG_OPA, LV_OPA_TRANSP),
    STATUS_VALUE(BORDER, 0), STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_body_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_BODY_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_BODY_H),
    STATUS_VALUE(X, 0), STATUS_VALUE(Y, 0), STATUS_VALUE(BG_OPA, LV_OPA_TRANSP),
    STATUS_VALUE(BORDER, 1), STATUS_VALUE(RADIUS, 2), STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_fill_geometry[] = {
    STATUS_VALUE(WIDTH, 0), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_FILL_H),
    STATUS_VALUE(X, PRODUCT_UI_STATUS_BAT_INSET), STATUS_VALUE(Y, PRODUCT_UI_STATUS_BAT_INSET),
    STATUS_VALUE(BG_OPA, LV_OPA_COVER), STATUS_VALUE(BORDER, 0), STATUS_VALUE(RADIUS, 0),
    STATUS_ZERO_PADDING, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_tip_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_TIP_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_TIP_H),
    STATUS_VALUE(X, PRODUCT_UI_STATUS_BAT_BODY_W + PRODUCT_UI_STATUS_BAT_TIP_GAP),
    STATUS_VALUE(Y, (PRODUCT_UI_STATUS_BAT_BODY_H - PRODUCT_UI_STATUS_BAT_TIP_H) / 2U),
    STATUS_VALUE(BG_OPA, LV_OPA_COVER), STATUS_VALUE(BORDER, 0), STATUS_VALUE(RADIUS, 1),
    LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_battery_charge_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_STATUS_BAT_BODY_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_BAT_BODY_H),
    STATUS_VALUE(ALIGN, LV_ALIGN_LEFT_MID), STATUS_VALUE(X, 0), STATUS_VALUE(Y, 0),
    STATUS_VALUE(TEXT_ALIGN, LV_TEXT_ALIGN_CENTER), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_status_root_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_SCREEN_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_H),
    STATUS_VALUE(X, 0), STATUS_VALUE(Y, 0), STATUS_VALUE(LAYOUT, LV_LAYOUT_FLEX),
    STATUS_VALUE(FLEX_FLOW, LV_FLEX_FLOW_ROW), STATUS_VALUE(FLEX_MAIN, LV_FLEX_ALIGN_START),
    STATUS_VALUE(FLEX_CROSS, LV_FLEX_ALIGN_CENTER), STATUS_VALUE(FLEX_TRACK, LV_FLEX_ALIGN_CENTER),
    STATUS_VALUE(PAD_COLUMN, PRODUCT_UI_STATUS_ICON_GAP), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_status_spacer_geometry[] = {
    STATUS_VALUE(WIDTH, 1), STATUS_VALUE(HEIGHT, PRODUCT_UI_STATUS_H),
    STATUS_VALUE(FLEX_GROW, 1), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_nav_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_SCREEN_W), STATUS_VALUE(HEIGHT, PRODUCT_UI_NAV_H),
    STATUS_VALUE(X, 0), STATUS_VALUE(Y, PRODUCT_UI_STATUS_H), LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_nav_back_geometry[] = {
    STATUS_VALUE(WIDTH, PRODUCT_UI_TOUCH_MIN), STATUS_VALUE(HEIGHT, PRODUCT_UI_TOUCH_MIN),
    STATUS_VALUE(ALIGN, LV_ALIGN_LEFT_MID), STATUS_VALUE(X, PRODUCT_UI_PAGE_PAD), STATUS_VALUE(Y, 0),
    LV_STYLE_CONST_PROPS_END
};
#define PRODUCT_UI_CONTENT_GEOMETRY(name, content_height, content_y, vertical_pad) \
    static const lv_style_const_prop_t name[] = { \
        STATUS_VALUE(WIDTH, PRODUCT_UI_SCREEN_W), STATUS_VALUE(HEIGHT, content_height), \
        STATUS_VALUE(X, 0), STATUS_VALUE(Y, content_y), \
        STATUS_VALUE(PAD_LEFT, PRODUCT_UI_PAGE_PAD), STATUS_VALUE(PAD_RIGHT, PRODUCT_UI_PAGE_PAD), \
        STATUS_VALUE(PAD_TOP, vertical_pad), STATUS_VALUE(PAD_BOTTOM, vertical_pad), \
        LV_STYLE_CONST_PROPS_END \
    }
PRODUCT_UI_CONTENT_GEOMETRY(s_page_content_geometry, PRODUCT_UI_CONTENT_H,
                            PRODUCT_UI_STATUS_H + PRODUCT_UI_NAV_H, PRODUCT_UI_SPACE_SM);
PRODUCT_UI_CONTENT_GEOMETRY(s_home_content_geometry, PRODUCT_UI_SCREEN_H - PRODUCT_UI_STATUS_H,
                            PRODUCT_UI_STATUS_H, PRODUCT_UI_PAGE_PAD);
#undef PRODUCT_UI_CONTENT_GEOMETRY
static LV_STYLE_CONST_INIT(s_wifi_box_style, (void *)s_wifi_box_geometry);
static LV_STYLE_CONST_INIT(s_battery_host_style, (void *)s_battery_host_geometry);
static LV_STYLE_CONST_INIT(s_battery_body_style, (void *)s_battery_body_geometry);
static LV_STYLE_CONST_INIT(s_battery_fill_style, (void *)s_battery_fill_geometry);
static LV_STYLE_CONST_INIT(s_battery_tip_style, (void *)s_battery_tip_geometry);
static LV_STYLE_CONST_INIT(s_battery_charge_style, (void *)s_battery_charge_geometry);
static LV_STYLE_CONST_INIT(s_status_root_style, (void *)s_status_root_geometry);
static LV_STYLE_CONST_INIT(s_status_spacer_style, (void *)s_status_spacer_geometry);
static LV_STYLE_CONST_INIT(s_nav_geometry_style, (void *)s_nav_geometry);
static LV_STYLE_CONST_INIT(s_nav_back_style, (void *)s_nav_back_geometry);
static LV_STYLE_CONST_INIT(s_page_content_style, (void *)s_page_content_geometry);
static LV_STYLE_CONST_INIT(s_home_content_style, (void *)s_home_content_geometry);
#undef STATUS_ZERO_PADDING
#undef STATUS_VALUE

/**
 * @brief 创建具有固定几何的四格信号及容器，不另取网络状态。
 * @param[in,out] view 接收信号对象引用的持续状态。
 * @param[in,out] parent 信号父容器。
 * @return 完整创建为 0，参数或对象失败为 -1，部分对象仍属父树。
 */
static int product_ui_wifi_signal_create(product_ui_status_view_t *view,
                                         lv_obj_t *parent)
{
    uint8_t i;

    if ((view == NULL) || (parent == NULL)) {
        return -1;
    }
    view->wifi = lv_obj_create(parent);
    if (view->wifi == NULL) {
        return -1;
    }
    product_ui_clean_obj(view->wifi);
    lv_obj_add_style(view->wifi, &s_wifi_box_style, 0);
    lv_obj_clear_flag(view->wifi, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++) {
        view->wifi_bar[i] = lv_obj_create(view->wifi);
        if (view->wifi_bar[i] == NULL) {
            return -1;
        }
        product_ui_clean_obj(view->wifi_bar[i]);
        lv_obj_add_style(view->wifi_bar[i], s_signal_styles[i], 0);
        lv_obj_set_style_bg_opa(view->wifi_bar[i], LV_OPA_30, 0);
        lv_obj_clear_flag(view->wifi_bar[i], LV_OBJ_FLAG_CLICKABLE |
                          LV_OBJ_FLAG_SCROLLABLE);
    }
    return 0;
}

/**
 * @brief 创建固定 flex 单元内的电池外框、独立填充、端点及充电标志。
 * @param[in,out] view 接收电池对象引用的持续状态。
 * @param[in,out] parent 电池父容器。
 * @return 完整创建为 0，参数或对象失败为 -1，部分对象仍属父树。
 * @note 填充和外框为同级绝对内缩，避免未布局时内容宽为零而出现空壳。
 */
static int product_ui_battery_create(product_ui_status_view_t *view,
                                     lv_obj_t *parent)
{
    if ((view == NULL) || (parent == NULL)) {
        return -1;
    }
    view->battery_host = lv_obj_create(parent);
    if (view->battery_host == NULL) {
        return -1;
    }
    product_ui_clean_obj(view->battery_host);
    lv_obj_add_style(view->battery_host, &s_battery_host_style, 0);
    lv_obj_clear_flag(view->battery_host, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE);

    /* 外壳仅绘边框，沿用 Cipher 几何。 */
    view->battery = lv_obj_create(view->battery_host);
    if (view->battery == NULL) {
        return -1;
    }
    product_ui_clean_obj(view->battery);
    lv_obj_add_style(view->battery, &s_battery_body_style, 0);
    lv_obj_clear_flag(view->battery, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE);

    /* 填充与外框在 host 下同级，使用绝对内缩；
     * 避免布局前 content_width 为零而出现空壳。 */
    view->battery_fill = lv_obj_create(view->battery_host);
    view->battery_tip = lv_obj_create(view->battery_host);
    view->battery_charge = product_ui_make_label(view->battery_host,
                                                  LV_SYMBOL_CHARGE,
                                                  PRODUCT_UI_STYLE_CAPTION);
    if ((view->battery_fill == NULL) || (view->battery_tip == NULL) ||
        (view->battery_charge == NULL)) {
        return -1;
    }
    product_ui_clean_obj(view->battery_fill);
    product_ui_clean_obj(view->battery_tip);
    lv_obj_add_style(view->battery_fill, &s_battery_fill_style, 0);
    lv_obj_clear_flag(view->battery_fill, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_style(view->battery_tip, &s_battery_tip_style, 0);
    lv_obj_clear_flag(view->battery_tip, LV_OBJ_FLAG_CLICKABLE);

    /* 充电标志对齐电池本体，host 还包含更宽的端点。 */
    lv_obj_add_style(view->battery_charge, &s_battery_charge_style, 0);
    lv_obj_add_flag(view->battery_charge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view->battery_charge, LV_OBJ_FLAG_CLICKABLE);
    return 0;
}

/**
 * @brief 状态根删除时先解除静态登记，再清空调用方持有的状态引用。
 * @param[in] event 根 DELETE 事件，用户数据借用持续状态。
 */
static void product_ui_status_delete_cb(lv_event_t *event)
{
    product_ui_status_view_t *view;

    view = lv_event_get_user_data(event);
    if (view != NULL) {
        product_ui_status_unregister(view);
        memset(view, 0, sizeof(*view));
    }
}

/**
 * @brief 把完整根登记到静态刷新链并附着删除收尾回调。
 * @param[in,out] view 内存寿命覆盖根对象删除回调的状态。
 * @return 登记成功为 0，空状态、无根或已经登记为 -1。
 */
static int product_ui_status_register(product_ui_status_view_t *view)
{
    if ((view == NULL) || (view->root == NULL) || view->is_registered) {
        return -1;
    }
    view->next = s_status_views;
    view->is_registered = 1U;
    s_status_views = view;
    lv_obj_add_event_cb(view->root, product_ui_status_delete_cb,
                        LV_EVENT_DELETE, view);
    return 0;
}

/**
 * @brief 创建固定行布局状态根，按参数选择透明或页面色背景。
 * @param[in,out] parent 状态根父对象。
 * @param[in] is_transparent 非零透明背景，零页面背景。
 * @return 成功返回父树拥有的根，失败为 NULL。
 */
static lv_obj_t *product_ui_status_root_create(lv_obj_t *parent,
                                               uint8_t is_transparent)
{
    lv_obj_t *root;

    root = lv_obj_create(parent);
    if (root == NULL) {
        return NULL;
    }
    product_ui_clean_obj(root);
    lv_obj_add_style(root, product_ui_style(PRODUCT_UI_STYLE_STATUS), 0);
    if (is_transparent) {
        lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_set_style_bg_color(root, product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    }
    /* 时间、伸展占位、WiFi、蓝牙及电池依次布局，避免互相碰撞。 */
    lv_obj_add_style(root, &s_status_root_style, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    return root;
}

/**
 * @brief 完成时间、占位及可选下拉绑定，登记成功后才输出状态根。
 * @param[in] params 借用创建描述，其 view／root_out 输出会更新。
 * @param[in,out] spacer 有效伸展占位对象。
 * @return 成功为 0，状态登记失败为 -1。
 */
static int product_ui_status_finish(
    const product_ui_status_create_params_t *params, lv_obj_t *spacer)
{
    product_ui_status_view_t *view = params->view;

    if (params->is_light) {
        lv_obj_set_style_text_color(view->time, lv_color_white(), 0);
    }
    product_ui_clean_obj(spacer);
    lv_obj_set_width(view->time, 40);
    lv_obj_add_style(spacer, &s_status_spacer_style, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE |
                      LV_OBJ_FLAG_CLICK_FOCUSABLE);
    if (params->allow_pull) {
        product_ui_control_center_bind_pull(view->root);
    }
    if (product_ui_status_register(view) != 0) {
        return -1;
    }
    if (params->root_out != NULL) {
        *params->root_out = view->root;
    }
    return 0;
}

/**
 * @brief 创建全部状态子对象，先应用当前值，再完成登记和可选根输出。
 * @param[in,out] parent 状态栏父容器。
 * @param[in] params 借用创建描述，view 指向的持续状态由本函数重置并填写。
 * @return 完整创建为 0，参数或任一对象失败为 -1。
 * @note 失败不删除父树，调用方按页面失败清理；当前状态值来自统一快照，不读取硬件。
 */
static int product_ui_status_create(
    lv_obj_t *parent, const product_ui_status_create_params_t *params)
{
    product_ui_status_t status;
    product_ui_status_view_t *view;
    lv_obj_t *spacer;

    if ((params == NULL) || (params->view == NULL)) {
        return -1;
    }
    view = params->view;
    memset(view, 0, sizeof(*view));
    view->root = product_ui_status_root_create(
        parent, params->is_light);
    if (view->root == NULL) {
        return -1;
    }
    view->is_light = params->is_light;
    view->hide_connectivity = params->hide_connectivity;
    /* 时间只使用状态字体角色，不附着带状态栏背景的整套状态样式。 */
    view->time = product_ui_make_label(view->root, "--:--",
                                       PRODUCT_UI_STYLE_CAPTION);
    if (view->time != NULL) {
        lv_obj_set_style_text_font(view->time,
                                   product_ui_font(PRODUCT_UI_FONT_STATUS), 0);
    }
    spacer = lv_obj_create(view->root);
    if (product_ui_wifi_signal_create(view, view->root) != 0) {
        return -1;
    }
    view->bluetooth = product_ui_make_status_icon(view->root,
                                                   PRODUCT_UI_ICON_BLUETOOTH);
    if ((view->time == NULL) || (spacer == NULL) ||
        (view->bluetooth == NULL) || (view->wifi == NULL) ||
        (product_ui_battery_create(view, view->root) != 0)) {
        return -1;
    }
    view->battery_text = product_ui_make_label(view->root, "",
                                                PRODUCT_UI_STYLE_CAPTION);
    if (view->battery_text == NULL) {
        return -1;
    }

    product_ui_status_snapshot(&status);
    product_ui_status_view_apply(view, &status);

    return product_ui_status_finish(params, spacer);
}

/**
 * @brief 把默认返回点击交给统一导航，失败显示提示。
 * @param[in] event 返回事件，当前不读取载荷。
 */
static void product_ui_back_cb(lv_event_t *event)
{
    (void)event;
    PRODUCT_UI_TRACE("nav_back action=history\n");
    if (product_ui_navigation_back() != 0) {
        product_ui_toast_show("返回失败", 1200U);
    }
}

/**
 * @brief 创建返回按钮并选择页面自定义或统一导航回调。
 * @param[in,out] nav 导航父容器。
 * @param[in] spec 借用的页面返回回调及上下文规格。
 * @return 成功为 0，按钮或图标标签失败为 -1。
 */
static int product_ui_nav_back_create(lv_obj_t *nav,
                                      const product_ui_page_spec_t *spec)
{
    lv_obj_t *back;
    lv_event_cb_t callback;
    void *user_data;

    back = lv_button_create(nav);
    if (back == NULL) {
        return -1;
    }
    product_ui_clean_obj(back);
    product_ui_button_style(back, PRODUCT_UI_BUTTON_ICON_LIGHT);
    lv_obj_set_style_text_color(back, product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
    lv_obj_add_style(back, &s_nav_back_style, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_PRESS_LOCK);
    callback = spec->nav_back_cb != NULL ? spec->nav_back_cb :
                                          product_ui_back_cb;
    user_data = spec->nav_back_cb != NULL ? spec->nav_back_user_data : NULL;
    lv_obj_add_event_cb(back, callback, LV_EVENT_CLICKED, user_data);
    back = lv_label_create(back);
    if (back == NULL) {
        return -1;
    }
    lv_obj_set_style_text_font(back, product_ui_font(PRODUCT_UI_FONT_BODY), 0);
    lv_label_set_text(back, LV_SYMBOL_LEFT);
    lv_obj_center(back);
    return 0;
}

/**
 * @brief 更新导航文字动作的显示、使能及只读状态；操作改变时撤销按压资格。
 */
void product_ui_page_action_update(product_ui_page_view_t *view,
                                    const char *text, uint8_t is_enabled,
                                    uint8_t is_read_only)
{
    lv_obj_t *action;
    const char *displayed;
    uint8_t is_visible;

    if ((view == NULL) || (view->nav_action == NULL) || (view->nav_action_label == NULL)) {
        return;
    }
    action = view->nav_action;
    is_visible = text != NULL && text[0] != '\0';
    displayed = lv_label_get_text(view->nav_action_label);
    if (is_visible == !lv_obj_has_flag(action, LV_OBJ_FLAG_HIDDEN) &&
        (!is_visible || (displayed != NULL && strcmp(displayed, text) == 0 &&
         lv_obj_has_state(action, LV_STATE_DISABLED) == (!is_enabled && !is_read_only) &&
         lv_obj_has_flag(action, LV_OBJ_FLAG_CLICKABLE) == (is_enabled && !is_read_only) &&
         view->is_nav_action_read_only == is_read_only))) {
        return;
    }
    /* 按压身份属于按下时显示的操作，更新操作须撤销旧资格。 */
    view->is_nav_action_armed = 0U;
    view->is_nav_action_read_only = is_read_only;
    lv_obj_clear_state(action, LV_STATE_PRESSED);
    lv_obj_update_flag(action, LV_OBJ_FLAG_HIDDEN, !is_visible);
    if (!is_visible) {
        return;
    }
    (void)product_ui_label_sync(view->nav_action_label, text, NULL);
    if (!is_enabled && !is_read_only) {
        lv_obj_add_state(action, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(action, LV_STATE_DISABLED);
    }
    lv_obj_update_flag(action, LV_OBJ_FLAG_CLICKABLE, is_enabled && !is_read_only);
    lv_obj_set_style_bg_opa(action, is_read_only ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(action, is_read_only ?
        product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED) : lv_color_white(), 0);
    if (is_read_only || !is_enabled) {
        lv_obj_clear_state(action, LV_STATE_PRESSED);
    }
}

/**
 * @brief 跟踪导航动作完整按压，操作变化、隐藏、禁用或资格丢失时拦截点击。
 * @param[in] event 动作按钮事件，用户数据借用页面状态。
 */
static void product_ui_nav_action_event(lv_event_t *event)
{
    product_ui_page_view_t *view = lv_event_get_user_data(event);
    lv_obj_t *action = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        view->is_nav_action_armed = 1U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        view->is_nav_action_armed = 0U;
    } else if (code == LV_EVENT_CLICKED) {
        if (!view->is_nav_action_armed || lv_obj_has_flag(action, LV_OBJ_FLAG_HIDDEN) ||
            !lv_obj_has_flag(action, LV_OBJ_FLAG_CLICKABLE) ||
            lv_obj_has_state(action, LV_STATE_DISABLED)) {
            lv_event_stop_processing(event);
        }
        view->is_nav_action_armed = 0U;
    }
}

/**
 * @brief 按文字优先、其次图像或符号创建导航动作，并先绑定输入资格门禁。
 * @param[in,out] nav 导航父容器。
 * @param[in] spec 借用动作规格；图片及回调上下文持续借用。
 * @param[in,out] view 接收动作及可用标签引用的页面状态。
 * @return 无需动作或成功创建为 0，按钮或视觉子对象失败为 -1。
 */
static int product_ui_nav_action_create(lv_obj_t *nav,
                                         const product_ui_page_spec_t *spec,
                                         product_ui_page_view_t *view)
{
    lv_obj_t *action;
    lv_obj_t *visual;
    uint8_t is_text = spec->nav_action_text != NULL;

    if (!is_text && (spec->nav_action_symbol == NULL) &&
        (spec->nav_action_image == NULL)) {
        return 0;
    }
    action = lv_button_create(nav);
    if (action == NULL) {
        return -1;
    }
    view->nav_action = action;
    product_ui_clean_obj(action);
    product_ui_button_style(action, is_text ? PRODUCT_UI_BUTTON_PRIMARY : PRODUCT_UI_BUTTON_ICON_LIGHT);
    lv_obj_clear_flag(action, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(action, is_text ? 96 : PRODUCT_UI_TOUCH_MIN, PRODUCT_UI_TOUCH_MIN);
    lv_obj_align(action, LV_ALIGN_RIGHT_MID, -PRODUCT_UI_PAGE_PAD, 0);
    lv_obj_add_event_cb(action, product_ui_nav_action_event, LV_EVENT_ALL, view);
    if (spec->nav_action_cb != NULL) {
        lv_obj_add_event_cb(action, spec->nav_action_cb, LV_EVENT_CLICKED,
                            spec->nav_action_user_data);
    }
    if (is_text) {
        visual = lv_label_create(action);
        if (visual != NULL) {
            lv_obj_set_style_text_font(visual, product_ui_font(PRODUCT_UI_FONT_BODY), 0);
            lv_obj_set_width(visual, 88);
            lv_obj_set_height(visual, product_ui_font(PRODUCT_UI_FONT_BODY)->line_height);
            lv_obj_set_style_text_align(visual, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(visual, LV_LABEL_LONG_DOT);
            view->nav_action_label = visual;
            product_ui_page_action_update(view, spec->nav_action_text,
                !spec->nav_action_disabled && spec->nav_action_cb != NULL,
                spec->nav_action_read_only);
        }
    } else if (spec->nav_action_image != NULL) {
        visual = lv_image_create(action);
        if (visual != NULL) {
            lv_image_set_src(visual, spec->nav_action_image);
            lv_obj_set_style_image_recolor(visual, product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
            lv_obj_set_style_image_recolor_opa(visual, LV_OPA_COVER, 0);
        }
    } else {
        visual = lv_label_create(action);
        if (visual != NULL) {
            lv_obj_set_style_text_font(visual, product_ui_font(PRODUCT_UI_FONT_BODY), 0);
            lv_label_set_text(visual, spec->nav_action_symbol);
        }
    }
    if (visual == NULL) {
        return -1;
    }
    lv_obj_center(visual);
    if (!is_text) {
        if (spec->nav_action_cb == NULL) {
            lv_obj_add_state(action, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(action, LV_STATE_DISABLED);
        }
        lv_obj_update_flag(action, LV_OBJ_FLAG_CLICKABLE, spec->nav_action_cb != NULL);
    }
    return 0;
}

static uint32_t s_page_enter_event;

/**
 * @brief 标题仍存在时应用进入动画的横向偏移。
 * @param[in,out] context 持久页面状态。
 * @param[in] offset 标题横向平移像素。
 */
static void product_ui_title_apply(void *context, int32_t offset)
{
    product_ui_page_view_t *view = context;
    if (view->title != NULL) lv_obj_set_style_translate_x(view->title, offset, 0);
}

/**
 * @brief 仅页面根收到共用进入事件时初始化并启动标题动效。
 * @param[in] event 同步进入事件，param 借用本次 is_back 字节。
 */
static void product_ui_page_enter_cb(lv_event_t *event)
{
    product_ui_page_view_t *view = lv_event_get_user_data(event);
    const uint8_t *is_back = lv_event_get_param(event);
    if (lv_event_get_target(event) != view->root || view->title == NULL) return;
    product_ui_motion_set(&view->title_motion,
        product_ui_motion_is_reduced() ? 0 : (*is_back ? -4 : 4));
    product_ui_motion_start(&view->title_motion, 0, PRODUCT_UI_MOTION_ENTER_MS);
}

/**
 * @brief 向已有页面同步发送共用进入事件，触发标题轻移进入效果。
 */
void product_ui_page_enter(lv_obj_t *root, uint8_t is_back)
{
    if (root != NULL && s_page_enter_event != 0U) {
        lv_obj_send_event(root, s_page_enter_event, &is_back);
    }
}

/**
 * @brief 根删除分发中先清根引用，再解除页面状态，保留其他删除监听继续执行。
 * @param[in] event 页面根 DELETE 事件，用户数据借用持续页面状态。
 */
static void product_ui_page_deleted(lv_event_t *event)
{
    product_ui_page_view_t *view = lv_event_get_user_data(event);
    if (lv_event_get_target(event) != view->root) return;
    /* 删除分发中不移除此回调，后续页面清理监听仍须运行。 */
    view->root = NULL;
    product_ui_page_view_reset(view);
}

/**
 * @brief 创建导航、标题及动作，标题借用页面状态绑定进入动画。
 * @param[in,out] parent 页面父容器。
 * @param[in] spec 本次借用的页面规格。
 * @param[in,out] view 接收导航、标题及动效状态的持久页面引用。
 * @return 完整创建为 0，任一对象或动作失败为 -1。
 */
static int product_ui_nav_create(lv_obj_t *parent,
                                 const product_ui_page_spec_t *spec,
                                 product_ui_page_view_t *view)
{
    lv_obj_t *nav;
    lv_obj_t *title;

    nav = lv_obj_create(parent);
    if (nav == NULL) {
        return -1;
    }
    product_ui_clean_obj(nav);
    lv_obj_add_style(nav, product_ui_style(PRODUCT_UI_STYLE_NAV), 0);
    lv_obj_add_style(nav, &s_nav_geometry_style, 0);
    if (spec->show_back && (product_ui_nav_back_create(nav, spec) != 0)) {
        return -1;
    }
    title = product_ui_make_label(nav, spec->title, PRODUCT_UI_STYLE_TITLE);
    if (title == NULL) {
        return -1;
    }
    lv_obj_align(title, LV_ALIGN_LEFT_MID,
                 spec->show_back ? 56 : PRODUCT_UI_PAGE_PAD, 0);
    lv_obj_set_width(title, PRODUCT_UI_SCREEN_W - 4 - (spec->show_back ? 56 : PRODUCT_UI_PAGE_PAD) -
        PRODUCT_UI_PAGE_PAD - (spec->nav_action_text != NULL ? 104 :
        ((spec->nav_action_symbol != NULL || spec->nav_action_image != NULL) ? 44 : 0)));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title, product_ui_font(PRODUCT_UI_FONT_TITLE)->line_height);
    if (product_ui_nav_action_create(nav, spec, view) != 0) {
        return -1;
    }
    view->nav = nav;
    view->title = title;
    {
        const product_ui_motion_binding_t binding = {
            .owner = title, .context = view, .apply = product_ui_title_apply,
        };
        product_ui_motion_bind(&view->title_motion, &binding);
    }
    return 0;
}

/**
 * @brief 解除标题动效、页面监听和状态栏登记，再清空页面状态，不删除父对象树。
 */
void product_ui_page_view_reset(product_ui_page_view_t *view)
{
    if (view != NULL) {
        product_ui_motion_dispose(&view->title_motion);
        if (view->root != NULL) {
            lv_obj_remove_event_cb(view->root, product_ui_page_enter_cb);
            lv_obj_remove_event_cb(view->root, product_ui_page_deleted);
        }
        product_ui_status_unregister(&view->status_view);
        memset(view, 0, sizeof(*view));
    }
}

/**
 * @brief 按是否导航返回选择共用内容几何，并按首页角色／透明主题设背景。
 * @param[in,out] parent 页面父容器。
 * @param[in] spec 借用的几何及主题规格。
 * @param[in] role 已经推导的页面角色。
 * @return 成功返回父树拥有的内容容器，失败为 NULL。
 */
static lv_obj_t *product_ui_page_content_create(
    lv_obj_t *parent, const product_ui_page_spec_t *spec,
    product_ui_page_role_e role)
{
    lv_obj_t *content;

    content = lv_obj_create(parent);
    if (content == NULL) {
        return NULL;
    }
    product_ui_clean_obj(content);
    lv_obj_add_style(content, product_ui_style(PRODUCT_UI_STYLE_PAGE), 0);
    lv_obj_add_style(content, spec->show_back ? &s_page_content_style : &s_home_content_style, 0);
    if ((role == PRODUCT_UI_PAGE_ROLE_HOME) ||
        (spec->theme == PRODUCT_UI_PAGE_THEME_TRANSPARENT)) {
        lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    }
    return content;
}

/**
 * @brief 在页面管理器提供的根对象中创建状态栏、可选导航和内容区。
 */
int product_ui_page_frame_create_ex(lv_obj_t *parent,
                                    const product_ui_page_spec_t *spec,
                                    product_ui_page_view_t *view)
{
    product_ui_status_create_params_t status_params;
    product_ui_status_t status;
    product_ui_page_role_e role;

    if ((parent == NULL) || (spec == NULL) || (spec->title == NULL) ||
        (view == NULL)) {
        return -1;
    }
    product_ui_page_view_reset(view);
    if ((product_ui_theme_init() != 0) ||
        (product_ui_control_center_init() != 0)) {
        return -1;
    }
    role = spec->role == PRODUCT_UI_PAGE_ROLE_AUTO ?
           (spec->show_back ? PRODUCT_UI_PAGE_ROLE_CONTENT :
                              PRODUCT_UI_PAGE_ROLE_HOME) : spec->role;
    product_ui_clean_obj(parent);
    lv_obj_add_style(parent, product_ui_style(PRODUCT_UI_STYLE_PAGE), 0);
    lv_obj_set_size(parent, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    view->root = parent;
    if (s_page_enter_event == 0U) s_page_enter_event = lv_event_register_id();
    lv_obj_add_event_cb(parent, product_ui_page_enter_cb, s_page_enter_event, view);
    lv_obj_add_event_cb(parent, product_ui_page_deleted, LV_EVENT_DELETE, view);

    status_params = (product_ui_status_create_params_t) {
        .view = &view->status_view,
        .root_out = &view->status,
        .allow_pull = 1U,
        .is_light = spec->show_back ? 0U : 1U,
        .hide_connectivity = 0U,
    };
    if (product_ui_status_create(parent, &status_params) != 0) {
        return -1;
    }
    if (spec->show_back &&
        (product_ui_nav_create(parent, spec, view) != 0)) {
        return -1;
    }
    view->content = product_ui_page_content_create(parent, spec, role);
    if (view->content == NULL) {
        return -1;
    }

    product_ui_status_snapshot(&status);
    product_ui_status_bar_refresh(&status);
    return 0;
}

/**
 * @brief 创建控制中心专用浅色前景状态栏，注册到共用值刷新链。
 */
lv_obj_t *product_ui_control_status_create(lv_obj_t *parent,
                                           product_ui_status_view_t *view)
{
    product_ui_status_create_params_t params;
    lv_obj_t *root = NULL;

    params = (product_ui_status_create_params_t) {
        .view = view,
        .root_out = &root,
        .allow_pull = 0U,
        .is_light = 1U,
        .hide_connectivity = 1U,
    };
    if ((parent == NULL) || (view == NULL) ||
        (product_ui_status_create(parent, &params) != 0)) {
        return NULL;
    }
    /* 深色控制中心用浅色前景，但真实能力发布时仍显示连接图标。 */
    view->hide_connectivity = 0U;
    lv_obj_set_style_border_color(
        root, product_ui_color(PRODUCT_UI_COLOR_CONTROL_BORDER), 0);
    lv_obj_set_style_border_opa(root, LV_OPA_30, 0);
    lv_obj_set_style_border_width(root, 1, 0);
    lv_obj_set_style_border_side(root, LV_BORDER_SIDE_BOTTOM, 0);
    return root;
}

/**
 * @brief 检查状态栏所有必要对象及四格信号引用是否齐全。
 * @param[in] view 借用的状态栏引用。
 * @return 全部齐全为 1，空或局部状态为 0。
 */
static uint8_t product_ui_status_view_ready(
    const product_ui_status_view_t *view)
{
    uint8_t i;

    if (view == NULL) {
        return 0U;
    }
    if ((view->root == NULL) || (view->time == NULL) ||
        (view->wifi == NULL) || (view->bluetooth == NULL) ||
        (view->battery_host == NULL) || (view->battery == NULL) ||
        (view->battery_fill == NULL) || (view->battery_tip == NULL) ||
        (view->battery_charge == NULL) || (view->battery_text == NULL)) {
        return 0U;
    }
    for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++) {
        if (view->wifi_bar[i] == NULL) {
            return 0U;
        }
    }
    return 1U;
}

/**
 * @brief 把状态值转换为显示文字、颜色、透明度及电池填充宽度。
 * @param[in] view 提供前景及连接显隐策略的状态栏配置。
 * @param[in] status 已经规范化的服务状态。
 * @return 无对象所有权的完整渲染值投影。
 */
static product_ui_status_projection_t product_ui_status_project(
    const product_ui_status_view_t *view, const product_ui_status_t *status)
{
    product_ui_status_projection_t next = {0};
    lv_opa_t dim_opa = LV_OPA_30;
    uint8_t level = 0U;
    uint8_t percent = 0U;
    uint8_t i;
    next.foreground = view->is_light ? lv_color_white() : product_ui_color(PRODUCT_UI_COLOR_TEXT);
    lv_snprintf(next.time, sizeof(next.time), "%02u:%02u", status->hour, status->minute);
    next.time_opa = status->valid_mask & PRODUCT_UI_STATUS_VALID_TIME ? LV_OPA_COVER : LV_OPA_TRANSP;
    next.wifi_opa = LV_OPA_COVER;
    if (status->valid_mask & PRODUCT_UI_STATUS_VALID_WIFI) {
        if (status->wifi_state == PRODUCT_UI_LINK_CONNECTED) {
            level = LV_MIN(status->wifi_level, PRODUCT_UI_WIFI_BAR_COUNT);
        } else if (status->wifi_state == PRODUCT_UI_LINK_ON) {
            dim_opa = LV_OPA_40;
            next.wifi_opa = LV_OPA_70;
        }
    }
    for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++)
        next.wifi_bars[i] = i < level ? LV_OPA_COVER : dim_opa;
    if (view->hide_connectivity || !(status->valid_mask & PRODUCT_UI_STATUS_VALID_WIFI))
        next.wifi_opa = LV_OPA_TRANSP;
    next.bluetooth_opa = LV_OPA_30;
    if ((status->valid_mask & PRODUCT_UI_STATUS_VALID_BLUETOOTH) &&
        status->bluetooth_state >= PRODUCT_UI_LINK_ON) next.bluetooth_opa = LV_OPA_COVER;
    if (view->hide_connectivity || !(status->valid_mask & PRODUCT_UI_STATUS_VALID_BLUETOOTH))
        next.bluetooth_opa = LV_OPA_TRANSP;
    next.battery_color = next.foreground;
    if (status->valid_mask & PRODUCT_UI_STATUS_VALID_BATTERY) {
        percent = LV_MIN(status->battery_percent, 100U);
        next.battery_opa = LV_OPA_COVER;
        if (status->battery_percent <= 20U) next.battery_color = product_ui_color(PRODUCT_UI_COLOR_DANGER);
        next.charging = status->is_charging != 0U;
    }
    next.battery_width = (uint8_t)(((uint32_t)percent * PRODUCT_UI_STATUS_BAT_FILL_MAX) / 100U);
    return next;
}

/**
 * @brief 比较前次值投影，按组只更新变化的颜色、尺寸、显隐及透明度。
 * @param[in,out] view 完整状态栏控件及旧投影。
 * @param[in] next 本次待呈现的值投影。
 * @note 不提交新投影身份；上层须等时间文字更新成功后再提交，避免失败呈现被缓存。
 */
static void product_ui_status_projection_apply(product_ui_status_view_t *view,
    const product_ui_status_projection_t *next)
{
    const product_ui_status_projection_t *old = &view->projection;
    uint8_t first = !view->has_projection;
    uint8_t i;
    if (first || !lv_color_eq(old->foreground, next->foreground)) {
        lv_obj_set_style_text_color(view->time, next->foreground, 0);
        lv_obj_set_style_image_recolor(view->bluetooth, next->foreground, 0);
        lv_obj_set_style_border_color(view->battery, next->foreground, 0);
        lv_obj_set_style_text_color(view->battery_charge, next->foreground, 0);
        lv_obj_set_style_text_color(view->battery_text, next->foreground, 0);
        for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++)
            lv_obj_set_style_bg_color(view->wifi_bar[i], next->foreground, 0);
    }
    if (first || !lv_color_eq(old->battery_color, next->battery_color)) {
        lv_obj_set_style_bg_color(view->battery_fill, next->battery_color, 0);
        lv_obj_set_style_bg_color(view->battery_tip, next->battery_color, 0);
    }
    if (first || old->battery_width != next->battery_width) {
        lv_obj_set_size(view->battery_fill, next->battery_width, PRODUCT_UI_STATUS_BAT_FILL_H);
        lv_obj_update_flag(view->battery_fill, LV_OBJ_FLAG_HIDDEN, next->battery_width == 0U);
    }
    if (first || old->charging != next->charging) {
        lv_obj_update_flag(view->battery_charge, LV_OBJ_FLAG_HIDDEN, !next->charging);
        if (next->charging) lv_obj_move_foreground(view->battery_charge);
    }
    if (first || old->time_opa != next->time_opa) lv_obj_set_style_opa(view->time, next->time_opa, 0);
    if (first || old->wifi_opa != next->wifi_opa) lv_obj_set_style_opa(view->wifi, next->wifi_opa, 0);
    if (first || old->bluetooth_opa != next->bluetooth_opa) lv_obj_set_style_opa(view->bluetooth, next->bluetooth_opa, 0);
    if (first || old->battery_opa != next->battery_opa) lv_obj_set_style_opa(view->battery_host, next->battery_opa, 0);
    for (i = 0U; i < PRODUCT_UI_WIFI_BAR_COUNT; i++)
        if (first || old->wifi_bars[i] != next->wifi_bars[i])
            lv_obj_set_style_bg_opa(view->wifi_bar[i], next->wifi_bars[i], 0);
    if (first) {
        lv_obj_set_style_bg_opa(view->battery, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(view->battery_fill, PRODUCT_UI_STATUS_BAT_INSET, PRODUCT_UI_STATUS_BAT_INSET);
        lv_obj_add_flag(view->battery_text, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 完整控件上应用值投影，时间标签成功后才更新已呈现缓存。
 * @param[in,out] view 持续有效的状态栏引用。
 * @param[in] status 本次规范化状态，NULL 时忽略。
 */
static void product_ui_status_view_apply(
    product_ui_status_view_t *view, const product_ui_status_t *status)
{
    product_ui_status_projection_t next;

    if (!product_ui_status_view_ready(view) || (status == NULL)) {
        return;
    }
    next = product_ui_status_project(view, status);
    product_ui_status_projection_apply(view, &next);
    /* 标签更新失败不能发布为完整呈现快照。 */
    if (product_ui_label_sync(view->time, next.time, NULL) < 0) return;
    view->projection = next;
    view->has_projection = 1U;
}

/**
 * @brief 把已规范化状态值投影到所有完整且仍登记的状态栏。
 */
void product_ui_status_bar_refresh(const product_ui_status_t *status)
{
    product_ui_status_view_t *view;

    if (status == NULL) {
        return;
    }
    for (view = s_status_views; view != NULL; view = view->next) {
        if (!product_ui_status_view_ready(view)) {
            continue;
        }
        product_ui_status_view_apply(view, status);
    }
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
