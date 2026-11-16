/**
 * @file tablet_shell_panel.c
 * @brief 横屏外壳的控制面板滑动动画与设置覆盖层。
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_TABLET_SHELL)

#include "modules/diagnostics/display/tablet_shell/tablet_shell_panel.h"

#define TABLET_STATUS_H             28
#define TABLET_PANEL_H              126
#define TABLET_SETTINGS_W           224
#define TABLET_PANEL_ANIM_MS        160

#if D_GC9307_ROTATION_DEGREES == 270
#define TABLET_ROTATION_TEXT        "270"
#else
#define TABLET_ROTATION_TEXT        "90"
#endif

typedef struct {
    const char *title;
    const char *value;
} tablet_setting_row_t;

static const tablet_setting_row_t s_setting_rows[] = {
    {"Display", "pending"},
    {"Brightness", "slider"},
    {"Rotation", TABLET_ROTATION_TEXT},
    {"Touch", "mapped"},
    {"Network", "placeholder"},
    {"Audio", "placeholder"},
    {"Storage", "placeholder"},
    {"Update", "placeholder"},
    {"About", "GC9307"},
};

static lv_obj_t *s_control_panel;
static lv_obj_t *s_panel_scrim;
static lv_obj_t *s_settings_panel;
static uint8_t s_control_panel_visible;
static uint8_t s_settings_visible;

/**
 * @brief 清空面板引用与可见状态，不删除父容器对象。
 */
static void tablet_panel_reset_refs(void)
{
    s_control_panel = NULL;
    s_panel_scrim = NULL;
    s_settings_panel = NULL;
    s_control_panel_visible = 0;
    s_settings_visible = 0;
}

/**
 * @brief 清除样式并允许事件和手势冒泡。
 * @param[in,out] obj 待调整的 LVGL 对象；为空时不处理。
 */
static void tablet_panel_clean_obj(lv_obj_t *obj)
{
    if (obj != NULL) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
}

/**
 * @brief 清除手势冒泡，让对象自行接收面板手势。
 * @param[in,out] obj 待调整的 LVGL 对象；为空时不处理。
 */
static void tablet_panel_accept_gesture(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

/**
 * @brief 创建带字体、颜色及冒泡标志的面板标签。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @param[in] text 复制的显示文本。
 * @param[in] font 持续有效的借用字体。
 * @param[in] color 文字颜色。
 * @return 新标签，分配失败返回 NULL。
 */
static lv_obj_t *tablet_panel_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL) {
        return NULL;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

/**
 * @brief 创建控制面板固定样式按钮。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @param[in] w 按钮宽度。
 * @param[in] h 按钮高度。
 * @return 新按钮，分配失败返回 NULL。
 */
static lv_obj_t *tablet_panel_button(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *button = lv_button_create(parent);

    if (button == NULL) {
        return NULL;
    }

    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x334155), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return button;
}

/**
 * @brief 创建一行静态设置标题与说明值。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @param[in] item 静态行描述，文字由标签复制。
 * @return 成功返回 0；参数或分配失败返回 -1。
 */
static int tablet_panel_create_setting_row(lv_obj_t *parent, const tablet_setting_row_t *item)
{
    lv_obj_t *row;
    lv_obj_t *title;
    lv_obj_t *value;

    if ((parent == NULL) || (item == NULL)) {
        return -1;
    }

    row = lv_obj_create(parent);
    if (row == NULL) {
        return -1;
    }
    tablet_panel_clean_obj(row);
    lv_obj_set_size(row, TABLET_SETTINGS_W - 24, 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x172033), 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x27364C), 0);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    title = tablet_panel_label(row, item->title, &lv_font_montserrat_12, lv_color_hex(0xF8FAFC));
    value = tablet_panel_label(row, item->value, &lv_font_montserrat_12, lv_color_hex(0x94A3B8));
    if ((title == NULL) || (value == NULL)) {
        return -1;
    }
    lv_obj_set_flex_grow(title, 1);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(value, 76);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    return 0;
}

/**
 * @brief 在收起动画结束时隐藏其用户数据中的对象。
 * @param[in] anim LVGL 动画完成回调对象。
 */
static void tablet_panel_hide_completed(lv_anim_t *anim)
{
    lv_obj_t *obj = (lv_obj_t *)lv_anim_get_user_data(anim);

    if (obj != NULL) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 取消同对象旧纵向动画并移动到目标坐标，可选结束后隐藏。
 * @param[in,out] obj 待调整的 LVGL 对象；为空时不处理。
 * @param[in] to 目标纵坐标。
 * @param[in] hide_on_complete 非零在结束时隐藏对象。
 */
static void tablet_panel_anim_y(lv_obj_t *obj, int32_t to, uint8_t hide_on_complete)
{
    lv_anim_t anim;
    int32_t from;

    if (obj == NULL) {
        return;
    }

    lv_anim_delete(obj, (lv_anim_exec_xcb_t)lv_obj_set_y);
    from = lv_obj_get_y(obj);
    if (from == to) {
        lv_obj_set_y(obj, to);
        if (hide_on_complete) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, TABLET_PANEL_ANIM_MS);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    if (hide_on_complete) {
        lv_anim_set_user_data(&anim, obj);
        lv_anim_set_completed_cb(&anim, tablet_panel_hide_completed);
    }
    lv_anim_start(&anim);
}

/**
 * @brief 同步清除设置可见标记并隐藏已有设置面板。
 */
static void tablet_panel_hide_settings(void)
{
    if (s_settings_panel == NULL) {
        return;
    }
    s_settings_visible = 0;
    lv_obj_add_flag(s_settings_panel, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 以竖向动画显示或隐藏控制中心，并同步遮罩；完整合同见 tablet_shell_panel.h。
 */
void tablet_shell_panel_show_control(uint8_t visible)
{
    visible = visible ? 1U : 0U;
    if ((s_control_panel == NULL) || (s_panel_scrim == NULL)) {
        return;
    }

    if (visible) {
        tablet_panel_hide_settings();
    }
    if (visible == s_control_panel_visible) {
        return;
    }

    s_control_panel_visible = visible;
    if (visible) {
        lv_obj_clear_flag(s_panel_scrim, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_control_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(s_control_panel, -1);
        tablet_panel_anim_y(s_control_panel, TABLET_STATUS_H, 0);
    } else {
        lv_obj_add_flag(s_panel_scrim, LV_OBJ_FLAG_HIDDEN);
        tablet_panel_anim_y(s_control_panel, -TABLET_PANEL_H, 1);
    }
}

/**
 * @brief 切换设置面板可见性，显示设置时关闭控制中心；完整合同见 tablet_shell_panel.h。
 */
void tablet_shell_panel_toggle_settings(void)
{
    if (s_settings_panel == NULL) {
        return;
    }

    s_settings_visible = (uint8_t)!s_settings_visible;
    if (s_settings_visible) {
        lv_obj_clear_flag(s_settings_panel, LV_OBJ_FLAG_HIDDEN);
        tablet_shell_panel_show_control(0);
        lv_obj_move_to_index(s_settings_panel, -1);
    } else {
        tablet_panel_hide_settings();
    }
}

/**
 * @brief 隐藏设置面板并请求控制中心收起动画；完整合同见 tablet_shell_panel.h。
 */
void tablet_shell_panel_hide_all(void)
{
    tablet_panel_hide_settings();
    tablet_shell_panel_show_control(0);
}

/**
 * @brief 按对象身份检查是否为当前面板遮罩；完整合同见 tablet_shell_panel.h。
 */
uint8_t tablet_shell_panel_is_scrim(lv_obj_t *obj)
{
    return (uint8_t)(obj == s_panel_scrim);
}

/**
 * @brief 创建覆盖内容区的半透明可点击遮罩并注册面板事件。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @param[in] gesture_cb 面板事件处理回调。
 * @return 成功返回 0，分配失败返回 -1。
 */
static int tablet_panel_create_scrim(lv_obj_t *parent, lv_event_cb_t gesture_cb)
{
    s_panel_scrim = lv_obj_create(parent);
    if (s_panel_scrim == NULL) {
        return -1;
    }
    tablet_panel_clean_obj(s_panel_scrim);
    lv_obj_set_size(s_panel_scrim, MY_DISP_HOR_RES, MY_DISP_VER_RES - TABLET_STATUS_H);
    lv_obj_set_pos(s_panel_scrim, 0, TABLET_STATUS_H);
    lv_obj_set_style_bg_opa(s_panel_scrim, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(s_panel_scrim, lv_color_hex(0x020617), 0);
    lv_obj_add_flag(s_panel_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_panel_scrim, gesture_cb, LV_EVENT_ALL, NULL);
    return 0;
}

/**
 * @brief 创建控制中心四个静态快捷项。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @return 成功返回 0，分配失败返回 -1。
 */
static int tablet_panel_create_tiles(lv_obj_t *parent)
{
    lv_obj_t *row;
    uint8_t i;
    static const char *items[] = {
        LV_SYMBOL_WIFI " WiFi",
        LV_SYMBOL_VOLUME_MID " Volume",
        LV_SYMBOL_BATTERY_3 " Power",
        LV_SYMBOL_REFRESH " Refresh",
    };

    row = lv_obj_create(parent);
    if (row == NULL) {
        return -1;
    }
    tablet_panel_clean_obj(row);
    lv_obj_set_size(row, MY_DISP_HOR_RES - 38, 76);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < 4; i++) {
        lv_obj_t *tile = tablet_panel_button(row, 64, 58);
        lv_obj_t *label;

        if (tile == NULL) {
            return -1;
        }
        label = tablet_panel_label(tile, items[i], &lv_font_montserrat_12, lv_color_hex(0xDDF4FF));
        if (label == NULL) {
            return -1;
        }
        lv_obj_center(label);
    }

    return 0;
}

/**
 * @brief 创建初始位于屏幕外的控制中心及快捷项。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @param[in] gesture_cb 控制面板事件回调。
 * @return 成功返回 0，子对象分配失败返回 -1。
 */
static int tablet_panel_create_control(lv_obj_t *parent, lv_event_cb_t gesture_cb)
{
    lv_obj_t *label;

    s_control_panel = lv_obj_create(parent);
    if (s_control_panel == NULL) {
        return -1;
    }
    tablet_panel_clean_obj(s_control_panel);
    lv_obj_set_size(s_control_panel, MY_DISP_HOR_RES - 18, TABLET_PANEL_H);
    lv_obj_set_pos(s_control_panel, 9, -TABLET_PANEL_H);
    lv_obj_set_style_bg_opa(s_control_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_control_panel, lv_color_hex(0x111827), 0);
    lv_obj_set_style_radius(s_control_panel, 8, 0);
    lv_obj_set_style_border_width(s_control_panel, 1, 0);
    lv_obj_set_style_border_color(s_control_panel, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(s_control_panel, 10, 0);
    lv_obj_set_style_pad_row(s_control_panel, 8, 0);
    lv_obj_set_flex_flow(s_control_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_control_panel, LV_OBJ_FLAG_HIDDEN);
    tablet_panel_accept_gesture(s_control_panel);
    lv_obj_add_event_cb(s_control_panel, gesture_cb, LV_EVENT_ALL, NULL);

    label = tablet_panel_label(s_control_panel, "Control Center", &lv_font_montserrat_16,
                               lv_color_hex(0xF8FAFC));
    if (label == NULL) {
        return -1;
    }

    return tablet_panel_create_tiles(s_control_panel);
}

/**
 * @brief 创建可竖向滚动的静态设置说明面板。
 * @param[in,out] parent 承载新建子对象的父容器。
 * @return 成功返回 0，任一行分配失败返回 -1。
 */
static int tablet_panel_create_settings(lv_obj_t *parent)
{
    lv_obj_t *label;
    uint8_t i;

    s_settings_panel = lv_obj_create(parent);
    if (s_settings_panel == NULL) {
        return -1;
    }
    tablet_panel_clean_obj(s_settings_panel);
    tablet_panel_accept_gesture(s_settings_panel);
    lv_obj_set_size(s_settings_panel, TABLET_SETTINGS_W, MY_DISP_VER_RES - TABLET_STATUS_H - 16);
    lv_obj_set_pos(s_settings_panel, MY_DISP_HOR_RES - TABLET_SETTINGS_W - 8, TABLET_STATUS_H + 8);
    lv_obj_set_style_bg_opa(s_settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_settings_panel, lv_color_hex(0x111827), 0);
    lv_obj_set_style_radius(s_settings_panel, 8, 0);
    lv_obj_set_style_border_width(s_settings_panel, 1, 0);
    lv_obj_set_style_border_color(s_settings_panel, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(s_settings_panel, 10, 0);
    lv_obj_set_style_pad_row(s_settings_panel, 8, 0);
    lv_obj_set_flex_flow(s_settings_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_settings_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_settings_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_settings_panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(s_settings_panel, LV_OBJ_FLAG_HIDDEN);

    label = tablet_panel_label(s_settings_panel, "Settings", &lv_font_montserrat_16, lv_color_hex(0xF8FAFC));
    if (label == NULL) {
        return -1;
    }

    for (i = 0; i < (uint8_t)(sizeof(s_setting_rows) / sizeof(s_setting_rows[0])); i++) {
        if (tablet_panel_create_setting_row(s_settings_panel, &s_setting_rows[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * @brief 创建遮罩、控制中心和设置面板；完整合同见 tablet_shell_panel.h。
 */
int tablet_shell_panel_create(lv_obj_t *parent, lv_event_cb_t gesture_cb)
{
    tablet_panel_reset_refs();
    if ((parent == NULL) || (gesture_cb == NULL)) {
        return -1;
    }

    if (tablet_panel_create_scrim(parent, gesture_cb) != 0 ||
        tablet_panel_create_control(parent, gesture_cb) != 0 ||
        tablet_panel_create_settings(parent) != 0) {
        tablet_panel_reset_refs();
        return -1;
    }
    return 0;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_TABLET_SHELL) */
