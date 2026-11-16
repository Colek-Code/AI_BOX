/** @file product_ui_feedback.c @brief 有限反馈面板、单个提示条及确认对话框，业务持久化由调用方拥有。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "platform/screen/transition/d_page_transition.h"

typedef struct {
    lv_obj_t *scrim;
    product_ui_confirm_cb_t callback;
    void *user_data;
    uint8_t is_pull_held;
} product_ui_confirm_state_t;

static product_ui_confirm_state_t s_confirm;
/**
 * @brief 查询当前单个确认模态根是否存在。
 */
uint8_t product_ui_feedback_is_modal(void) { return s_confirm.scrim != NULL; }
static product_ui_motion_t s_toast_motion;

/**
 * @brief 只在透明度变化时更新本动效所有者的局部样式。
 * @param[in,out] context 借用且仍有效的 LVGL 所有者对象。
 * @param[in] opacity 动画提供的目标透明度数值。
 */
static void product_ui_feedback_opacity(void *context, int32_t opacity)
{
    lv_obj_t *owner = context;
    if (lv_obj_get_style_opa(owner, 0) == (lv_opa_t)opacity) return;
    lv_obj_set_style_opa(owner, (lv_opa_t)opacity, 0);
}

/**
 * @brief 绑定对象淡入动效，减少动态效果时立即使用不透明终态。
 * @param[in,out] owner 有效反馈对象。
 * @param[in,out] motion 覆盖对象寿命的持久动画状态。
 */
static void product_ui_feedback_reveal(lv_obj_t *owner, product_ui_motion_t *motion)
{
    const product_ui_motion_binding_t binding = {
        .owner = owner, .context = owner, .apply = product_ui_feedback_opacity,
    };
    product_ui_motion_bind(motion, &binding);
    product_ui_motion_set(motion, product_ui_motion_is_reduced() ? LV_OPA_COVER : LV_OPA_70);
    product_ui_motion_start(motion, LV_OPA_COVER, PRODUCT_UI_MOTION_RELEASE_MS);
    product_ui_motion_apply_tree(owner);
}

/**
 * @brief 清除默认视觉样式及滚动能力，供明确布局使用。
 * @param[in,out] obj 有效 LVGL 对象。
 */
static void product_ui_feedback_clean(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 创建复制文本且居中自动换行的主题标签。
 * @param[in,out] parent 标签父对象。
 * @param[in] text 本次复制的文字。
 * @param[in] style 主题样式。
 * @return 成功返回父树拥有的标签，失败为 NULL。
 */
static lv_obj_t *product_ui_feedback_label(lv_obj_t *parent, const char *text,
                                           product_ui_style_e style)
{
    lv_obj_t *label;

    label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_add_style(label, product_ui_style(style), 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

/**
 * @brief 有完整动作规格时创建按钮，无动作文字时以父对象表示无需额外控件。
 * @param[in,out] parent 动作按钮父对象。
 * @param[in] spec 有效反馈规格，回调及上下文持续借用。
 * @return 按钮成功或无需动作返回非空，规格／创建失败返回 NULL。
 */
static lv_obj_t *product_ui_feedback_action_create(
    lv_obj_t *parent, const product_ui_feedback_spec_t *spec)
{
    lv_obj_t *action;

    if (spec->action_label == NULL) {
        return parent;
    }
    if (spec->action_cb == NULL) {
        return NULL;
    }
    action = lv_button_create(parent);
    if (action == NULL) {
        return NULL;
    }
    lv_obj_add_event_cb(action, spec->action_cb, LV_EVENT_CLICKED,
                        spec->user_data);
    if (product_ui_feedback_label(action, spec->action_label,
                                  PRODUCT_UI_STYLE_BODY) == NULL) {
        lv_obj_delete(action);
        return NULL;
    }
    product_ui_button_style(action, PRODUCT_UI_BUTTON_PRIMARY);
    lv_obj_set_size(action, 96, PRODUCT_UI_TOUCH_MIN);
    lv_obj_center(lv_obj_get_child(action, 0));
    return action;
}

/**
 * @brief 创建可选动作反馈面板，动作文字和回调必须同时有或同时无。
 */
lv_obj_t *product_ui_feedback_create_ex(
    lv_obj_t *parent, const product_ui_feedback_spec_t *spec)
{
    static const char *symbols[] = {
        LV_SYMBOL_DIRECTORY,
        LV_SYMBOL_REFRESH,
        LV_SYMBOL_WARNING,
    };
    lv_obj_t *root;
    lv_obj_t *symbol;

    if ((parent == NULL) || (spec == NULL) || (spec->title == NULL) ||
        (spec->message == NULL) ||
        (spec->state > PRODUCT_UI_FEEDBACK_ERROR) ||
        ((spec->action_label == NULL) != (spec->action_cb == NULL))) {
        return NULL;
    }
    root = lv_obj_create(parent);
    if (root == NULL) {
        return NULL;
    }
    product_ui_feedback_clean(root);
    lv_obj_set_size(root, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(root, PRODUCT_UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(root, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    symbol = product_ui_feedback_label(root, symbols[spec->state],
                                       PRODUCT_UI_STYLE_TITLE);
    if ((symbol == NULL) ||
        (product_ui_feedback_label(root, spec->title,
                                   PRODUCT_UI_STYLE_BODY) == NULL) ||
        (product_ui_feedback_label(root, spec->message,
                                   PRODUCT_UI_STYLE_CAPTION) == NULL) ||
        (product_ui_feedback_action_create(root, spec) == NULL)) {
        lv_obj_delete(root);
        return NULL;
    }
    lv_obj_set_style_text_color(symbol,
        product_ui_color(spec->state == PRODUCT_UI_FEEDBACK_ERROR ?
                         PRODUCT_UI_COLOR_DANGER : PRODUCT_UI_COLOR_ACCENT), 0);
    return root;
}

/**
 * @brief 按合法反馈状态创建无动作面板，复制标题及说明。
 */
lv_obj_t *product_ui_feedback_create(lv_obj_t *parent,
                                     product_ui_feedback_state_e state,
                                     const char *title,
                                     const char *message)
{
    product_ui_feedback_spec_t spec;

    spec = (product_ui_feedback_spec_t) {
        .state = state,
        .title = title,
        .message = message,
    };
    return product_ui_feedback_create_ex(parent, &spec);
}

enum {
    TOAST_WIDTH, TOAST_HEIGHT, TOAST_MIN_WIDTH, TOAST_MAX_WIDTH, TOAST_MIN_HEIGHT,
    TOAST_BG_OPA, TOAST_RADIUS, TOAST_PAD_LEFT, TOAST_PAD_RIGHT, TOAST_PAD_TOP,
    TOAST_PAD_BOTTOM, TOAST_ALIGN, TOAST_X, TOAST_Y,
};
static const lv_style_prop_t s_toast_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_MIN_WIDTH, LV_STYLE_MAX_WIDTH, LV_STYLE_MIN_HEIGHT,
    LV_STYLE_BG_OPA, LV_STYLE_RADIUS, LV_STYLE_PAD_LEFT, LV_STYLE_PAD_RIGHT, LV_STYLE_PAD_TOP,
    LV_STYLE_PAD_BOTTOM, LV_STYLE_ALIGN, LV_STYLE_X, LV_STYLE_Y,
};
#define TOAST_VALUE(property, number) {&s_toast_properties[TOAST_##property], {.num = (number)}}
static const lv_style_const_prop_t s_toast_geometry[] = {
    TOAST_VALUE(WIDTH, LV_SIZE_CONTENT), TOAST_VALUE(HEIGHT, LV_SIZE_CONTENT),
    TOAST_VALUE(MIN_WIDTH, 96), TOAST_VALUE(MAX_WIDTH, 248 + 2 * PRODUCT_UI_SPACE_MD),
    TOAST_VALUE(MIN_HEIGHT, 34), TOAST_VALUE(BG_OPA, LV_OPA_COVER), TOAST_VALUE(RADIUS, PRODUCT_UI_RADIUS_LG),
    TOAST_VALUE(PAD_LEFT, PRODUCT_UI_SPACE_MD), TOAST_VALUE(PAD_RIGHT, PRODUCT_UI_SPACE_MD),
    TOAST_VALUE(PAD_TOP, PRODUCT_UI_SPACE_SM), TOAST_VALUE(PAD_BOTTOM, PRODUCT_UI_SPACE_SM),
    TOAST_VALUE(ALIGN, LV_ALIGN_BOTTOM_MID), TOAST_VALUE(X, 0), TOAST_VALUE(Y, -PRODUCT_UI_SPACE_MD),
    LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_toast_geometry_style, (void *)s_toast_geometry);
#undef TOAST_VALUE

static lv_obj_t *s_toast_obj;
static lv_timer_t *s_toast_timer;

/**
 * @brief 解除提示条动画、删除未触发的定时器及当前提示对象。
 */
static void product_ui_toast_clear(void)
{
    product_ui_motion_dispose(&s_toast_motion);
    if (s_toast_timer != NULL) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast_obj != NULL) {
        lv_obj_delete(s_toast_obj);
        s_toast_obj = NULL;
    }
}

/**
 * @brief 清除当前提示条及确认模态，归还本模态的下拉占用。
 */
void product_ui_feedback_clear(void)
{
    lv_obj_t *scrim = s_confirm.scrim;

    product_ui_toast_clear();
    s_confirm.scrim = NULL;
    s_confirm.callback = NULL;
    s_confirm.user_data = NULL;
    product_ui_control_center_release_pull(&s_confirm.is_pull_held);
    if (scrim != NULL) {
        lv_obj_delete(scrim);
        d_screen_request_composed_frame();
    }
}

/**
 * @brief 单次显示期限到达时撤销定时器引用并删除提示条。
 * @param[in] timer LVGL 单次定时器，当前未读取内容。
 */
static void product_ui_toast_delete_cb(lv_timer_t *timer)
{
    (void)timer;
    s_toast_timer = NULL;
    if (s_toast_obj != NULL) {
        lv_obj_delete(s_toast_obj);
        s_toast_obj = NULL;
    }
}

/**
 * @brief 替换单个非模态提示条，按期限自动删除并应用当前动效策略。
 */
void product_ui_toast_show(const char *message, uint16_t duration_ms)
{
    lv_point_t text_size;
    if ((message == NULL) || (message[0] == '\0') || (duration_ms == 0U)) {
        return;
    }
    /* 全局只显示一个提示条，替换前项避免空白胶囊层叠。 */
    product_ui_toast_clear();

    /* 标签直接承载背景、间距及动效，避免页面保留时
     * 额外建立只有一个文本子项的包装容器。 */
    s_toast_obj = lv_label_create(lv_layer_top());
    if (s_toast_obj == NULL) {
        return;
    }
    product_ui_feedback_clean(s_toast_obj);
    lv_obj_add_style(s_toast_obj, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_add_style(s_toast_obj, &s_toast_geometry_style, 0);
    lv_label_set_text(s_toast_obj, message);
    lv_label_set_long_mode(s_toast_obj, LV_LABEL_LONG_WRAP);
    lv_text_get_size(&text_size, message, product_ui_font(PRODUCT_UI_FONT_CAPTION),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    /* SIZE_CONTENT 不会因 max_width 裁边而自行换行；
     * 长消息在布局前设置明确内容宽度。 */
    if (text_size.x > 248) {
        lv_obj_set_width(s_toast_obj, 248 + 2 * PRODUCT_UI_SPACE_MD);
    }
    /* 提示条只反馈，不接管输入；保存后用户继续编辑时，
     * 即使它覆盖底部操作区域也不抢占该操作。 */
    lv_obj_remove_flag(s_toast_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    /*
     * ON_SURFACE is a light "ink on surface" token (≈白), PAGE 也是浅色。
     * 深色条 + 浅色字才是 toast 对比度；之前浅底浅字会显示成空白小胶囊。
     */
    lv_obj_set_style_bg_color(s_toast_obj,
                              product_ui_color(PRODUCT_UI_COLOR_STATUS_BG), 0);
    lv_obj_set_style_text_color(s_toast_obj,
                                product_ui_color(PRODUCT_UI_COLOR_ON_STATUS), 0);
    s_toast_timer = lv_timer_create(product_ui_toast_delete_cb, duration_ms,
                                    NULL);
    if (s_toast_timer == NULL) {
        product_ui_toast_clear();
        return;
    }
    lv_timer_set_repeat_count(s_toast_timer, 1);
    product_ui_feedback_reveal(s_toast_obj, &s_toast_motion);
}

/**
 * @brief 先取走选择及回调绑定，删除模态并归还占用后才同步通知业务。
 * @param[in] event 选择按钮 CLICKED 事件，用户数据编码 0／1／2。
 * @note 回调可安全创建新模态或预约导航，不会被旧模态的删除回调清空。
 */
static void product_ui_confirm_event_cb(lv_event_t *event)
{
    uint8_t confirmed;
    product_ui_confirm_cb_t callback;
    void *user_data;
    lv_obj_t *scrim;

    confirmed = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    callback = s_confirm.callback;
    user_data = s_confirm.user_data;
    scrim = s_confirm.scrim;
    s_confirm.scrim = NULL;
    s_confirm.callback = NULL;
    s_confirm.user_data = NULL;
    product_ui_control_center_release_pull(&s_confirm.is_pull_held);
    if (scrim != NULL) {
        lv_obj_delete(scrim);
        d_screen_request_composed_frame();
    }
    if (callback != NULL) {
        callback(confirmed, user_data);
    }
}

/**
 * @brief 当前遮罩由外部删除时清空回调借用并归还下拉占用。
 * @param[in] event 模态根删除事件。
 * @note 不调用业务回调，不把外部清理解释成用户已经作出选择。
 */
static void product_ui_confirm_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_confirm.scrim) return;
    s_confirm.scrim = NULL;
    s_confirm.callback = NULL;
    s_confirm.user_data = NULL;
    product_ui_control_center_release_pull(&s_confirm.is_pull_held);
    d_screen_request_composed_frame();
}

/**
 * @brief 替换按钮的视觉／本地样式并安装主题角色，不改变事件回调与点击资格。
 */
void product_ui_button_style(lv_obj_t *button, product_ui_button_role_e role)
{
    static const product_ui_style_e normal[] = {PRODUCT_UI_STYLE_DIALOG_SECONDARY,
        PRODUCT_UI_STYLE_DIALOG_PRIMARY, PRODUCT_UI_STYLE_DIALOG_DANGER};
    static const product_ui_style_e pressed[] = {PRODUCT_UI_STYLE_DIALOG_SECONDARY_PRESSED,
        PRODUCT_UI_STYLE_DIALOG_PRIMARY_PRESSED, PRODUCT_UI_STYLE_DIALOG_DANGER_PRESSED};
    uint32_t index;
    if (button == NULL) return;
    if (role < PRODUCT_UI_BUTTON_SECONDARY || role > PRODUCT_UI_BUTTON_ICON_DARK) role = PRODUCT_UI_BUTTON_SECONDARY;
    lv_obj_remove_style_all(button);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_DIALOG_BUTTON), 0);
    if (role <= PRODUCT_UI_BUTTON_DANGER) {
        lv_obj_add_style(button, product_ui_style(normal[role]), 0);
        lv_obj_add_style(button, product_ui_style(pressed[role]), LV_STATE_PRESSED);
        lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_DIALOG_DISABLED), LV_STATE_DISABLED);
    } else {
        uint8_t dark = role == PRODUCT_UI_BUTTON_ICON_DARK;
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(button, product_ui_color(dark ?
            PRODUCT_UI_COLOR_CONTROL_TEXT : PRODUCT_UI_COLOR_TEXT_MUTED), 0);
        lv_obj_set_style_bg_color(button, product_ui_color(dark ?
            PRODUCT_UI_COLOR_CONTROL_BTN_PRESSED : PRODUCT_UI_COLOR_PRIMARY_SOFT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_opa(button, LV_OPA_30, LV_STATE_DISABLED);
    }
    if (role == PRODUCT_UI_BUTTON_SECONDARY || role >= PRODUCT_UI_BUTTON_ICON_LIGHT) {
        uint8_t dark = role == PRODUCT_UI_BUTTON_ICON_DARK;
        lv_obj_add_style(button, product_ui_style(dark ? PRODUCT_UI_STYLE_BUTTON_SELECTED_DARK :
            PRODUCT_UI_STYLE_BUTTON_SELECTED), LV_STATE_CHECKED);
        lv_obj_add_style(button, product_ui_style(dark ? PRODUCT_UI_STYLE_BUTTON_SELECTED_DARK_PRESSED :
            PRODUCT_UI_STYLE_BUTTON_SELECTED_PRESSED), LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_DIALOG_DISABLED), LV_STATE_CHECKED | LV_STATE_DISABLED);
    }
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_DIALOG_FOCUS), LV_STATE_FOCUS_KEY);
    for (index = 0U; index < lv_obj_get_child_count(button); index++) {
        lv_obj_t *label = lv_obj_get_child(button, index);
        if (!lv_obj_check_type(label, &lv_label_class)) continue;
        /* 标签保留字体／布局，文字颜色跟随按钮状态。 */
        lv_obj_remove_style(label, product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
        (void)lv_obj_remove_local_style_prop(label, LV_STYLE_TEXT_COLOR, 0);
    }
}

/**
 * @brief 以旧对话框三角色合同调用共用按钮样式，其他角色回退次要。
 */
void product_ui_dialog_button_style(lv_obj_t *button, product_ui_dialog_button_role_e role)
{
    if (role < PRODUCT_UI_DIALOG_SECONDARY || role > PRODUCT_UI_DIALOG_DANGER) role = PRODUCT_UI_DIALOG_SECONDARY;
    product_ui_button_style(button, role);
}

/**
 * @brief 对刚创建的当前模态选择紧凑布局，短正文缩至内容，长正文保留滚动。
 */
int product_ui_dialog_compact(lv_obj_t *modal)
{
    lv_obj_t *dialog, *title, *message_area, *message, *actions;
    int32_t chrome, body_height, maximum;
    if (modal == NULL || modal != s_confirm.scrim || lv_obj_get_child_count(modal) != 1U) return -1;
    dialog = lv_obj_get_child(modal, 0);
    if (lv_obj_get_child_count(dialog) != 3U) return -1;
    title = lv_obj_get_child(dialog, 0);
    message_area = lv_obj_get_child(dialog, 1);
    actions = lv_obj_get_child(dialog, 2);
    message = lv_obj_get_child(message_area, 0);
    if (message == NULL || !lv_obj_check_type(message, &lv_label_class)) return -1;
    lv_obj_update_layout(dialog);
    chrome = lv_obj_get_height(title) + lv_obj_get_height(actions) +
        2 * lv_obj_get_style_pad_row(dialog, 0) + lv_obj_get_style_pad_top(dialog, 0) +
        lv_obj_get_style_pad_bottom(dialog, 0) + 2 * lv_obj_get_style_border_width(dialog, 0);
    maximum = PRODUCT_UI_SCREEN_H - 32 - chrome;
    if (maximum <= 0) return -1;
    body_height = LV_MIN(maximum, LV_MAX(lv_obj_get_self_height(message),
        lv_font_get_line_height(product_ui_font(PRODUCT_UI_FONT_BODY))));
    lv_obj_set_flex_grow(message_area, 0U);
    lv_obj_set_height(message_area, body_height);
    lv_obj_set_height(dialog, chrome + body_height);
    lv_obj_center(dialog);
    return 0;
}

/**
 * @brief 创建返回固定选择值的主题模态按钮。
 * @param[in,out] parent 按钮父容器。
 * @param[in] text 本次复制的按钮文字。
 * @param[in] confirmed 此按钮报告的选择值。
 * @param[in] role 按钮主题角色。
 * @return 成功返回父树拥有的按钮，失败删除局部对象并返回 NULL。
 */
static lv_obj_t *product_ui_confirm_button(lv_obj_t *parent, const char *text,
    uint8_t confirmed, product_ui_dialog_button_role_e role)
{
    lv_obj_t *button;

    button = lv_button_create(parent);
    if (button == NULL) {
        return NULL;
    }
    lv_obj_add_event_cb(button, product_ui_confirm_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)confirmed);
    if (product_ui_feedback_label(button, text,
                                  PRODUCT_UI_STYLE_BODY) == NULL) {
        lv_obj_delete(button);
        return NULL;
    }
    product_ui_dialog_button_style(button, role);
    lv_obj_set_size(button, 0, PRODUCT_UI_DIALOG_ACTION_H);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_center(lv_obj_get_child(button, 0));
    return button;
}

typedef enum {
    PRODUCT_UI_CONFIRM_STANDARD,
    PRODUCT_UI_CONFIRM_SAVE,
    PRODUCT_UI_CONFIRM_DANGER,
} product_ui_confirm_kind_e;

/**
 * @brief 创建当前唯一模态，完整失败时统一回收局部树及下拉占用。
 * @param[in] title 本次复制的标题。
 * @param[in] message 本次复制的正文。
 * @param[in] callback 选择时同步调用的可选持续有效回调。
 * @param[in] user_data 覆盖模态寿命的借用上下文。
 * @param[in] kind 普通、保存三选或危险确认布局。
 * @return 成功返回全局遮罩，必要参数、已有模态或分配失败返回 NULL。
 */
static lv_obj_t *product_ui_confirm_dialog_build(const char *title,
                                         const char *message,
                                         product_ui_confirm_cb_t callback,
                                         void *user_data, product_ui_confirm_kind_e kind)
{
    lv_obj_t *dialog;
    lv_obj_t *actions;
    lv_obj_t *message_area;
    uint8_t save_choice = kind == PRODUCT_UI_CONFIRM_SAVE;

    if ((title == NULL) || (message == NULL) || (s_confirm.scrim != NULL)) {
        return NULL;
    }
    s_confirm.scrim = lv_obj_create(lv_layer_top());
    if (s_confirm.scrim == NULL) {
        return NULL;
    }
    product_ui_control_center_hold_pull(&s_confirm.is_pull_held);
    lv_obj_add_event_cb(s_confirm.scrim, product_ui_confirm_deleted, LV_EVENT_DELETE, NULL);
    s_confirm.callback = callback;
    s_confirm.user_data = user_data;
    product_ui_feedback_clean(s_confirm.scrim);
    lv_obj_set_size(s_confirm.scrim, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_style_bg_color(s_confirm.scrim, lv_color_black(), 0);
    /* 对话框从首帧即不透明，只把外部遮罩背景调暗。 */
    lv_obj_set_style_opa(s_confirm.scrim, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_confirm.scrim, LV_OPA_20, 0);
    lv_obj_add_flag(s_confirm.scrim, LV_OBJ_FLAG_CLICKABLE);

    dialog = lv_obj_create(s_confirm.scrim);
    if (dialog == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    product_ui_feedback_clean(dialog);
    lv_obj_set_size(dialog, 272, 208);
    lv_obj_add_style(dialog, product_ui_style(PRODUCT_UI_STYLE_SURFACE), 0);
    lv_obj_set_style_pad_all(dialog, PRODUCT_UI_SPACE_MD, 0);
    lv_obj_set_style_pad_row(dialog, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(dialog);
    if (product_ui_feedback_label(dialog, title, PRODUCT_UI_STYLE_TITLE) == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    message_area = lv_obj_create(dialog);
    if (message_area == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    product_ui_feedback_clean(message_area);
    lv_obj_set_width(message_area, LV_PCT(100));
    lv_obj_set_flex_grow(message_area, 1);
    lv_obj_add_flag(message_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(message_area, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(message_area, LV_DIR_VER);
    if (product_ui_feedback_label(message_area, message, PRODUCT_UI_STYLE_BODY) == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    actions = lv_obj_create(dialog);
    if (actions == NULL) {
        product_ui_feedback_clear();
        return NULL;
    }
    product_ui_feedback_clean(actions);
    lv_obj_add_flag(actions, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(actions, LV_PCT(100), PRODUCT_UI_DIALOG_ACTION_H);
    lv_obj_set_style_pad_column(actions, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    {
        const char *labels[] = {"取消", save_choice ? "不保存" : "确定", "保存"};
        const uint8_t choices[] = {0U, save_choice ? 2U : 1U, 1U};
        uint8_t index;
        for (index = 0U; index < (save_choice ? 3U : 2U); index++) {
            product_ui_dialog_button_role_e role = choices[index] == 0U ? PRODUCT_UI_DIALOG_SECONDARY :
                choices[index] == 2U || kind == PRODUCT_UI_CONFIRM_DANGER ? PRODUCT_UI_DIALOG_DANGER : PRODUCT_UI_DIALOG_PRIMARY;
            lv_obj_t *button = product_ui_confirm_button(actions, labels[index], choices[index], role);
            if (button == NULL) {
                product_ui_feedback_clear();
                return NULL;
            }
        }
    }
    product_ui_motion_apply_tree(s_confirm.scrim);
    d_screen_request_composed_frame();
    return s_confirm.scrim;
}

/**
 * @brief 创建全局单个普通确认模态。
 */
lv_obj_t *product_ui_confirm_dialog_show(const char *title, const char *message,
    product_ui_confirm_cb_t callback, void *user_data)
{
    return product_ui_confirm_dialog_build(title, message, callback, user_data, PRODUCT_UI_CONFIRM_STANDARD);
}

/**
 * @brief 创建取消／不保存／保存的三选模态，只报告用户意图。
 */
lv_obj_t *product_ui_save_dialog_show(const char *title, const char *message,
    product_ui_confirm_cb_t callback, void *user_data)
{
    return product_ui_confirm_dialog_build(title, message, callback, user_data, PRODUCT_UI_CONFIRM_SAVE);
}

/**
 * @brief 创建危险操作确认模态，沿用普通确认的零／一回调与生命周期。
 */
lv_obj_t *product_ui_danger_dialog_show(const char *title, const char *message,
    product_ui_confirm_cb_t callback, void *user_data)
{
    return product_ui_confirm_dialog_build(title, message, callback, user_data, PRODUCT_UI_CONFIRM_DANGER);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
