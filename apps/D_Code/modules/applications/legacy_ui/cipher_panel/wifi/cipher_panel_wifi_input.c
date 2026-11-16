/** @file cipher_panel_wifi_input.c @brief 旧根 UI 密码面板、静态键盘布局及异步对象关闭。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL) && (D_FEATURE_WIFI)

#include "modules/applications/legacy_ui/cipher_panel/wifi/cipher_panel_wifi_input.h"
#include "modules/applications/legacy_ui/cipher_panel/pages/cipher_panel_pages.h"
#include "framework/d_ui_font.h"
#include <string.h>

#define CIPHER_WIFI_PROMPT_Y    52
#define CIPHER_WIFI_PROMPT_H    138
#define CIPHER_WIFI_KEYBOARD_H  270
#define CIPHER_WIFI_KEYBOARD_Y  (MY_DISP_VER_RES - CIPHER_WIFI_KEYBOARD_H)
#define CIPHER_WIFI_KB_CTRL(w)  (LV_KEYBOARD_CTRL_BUTTON_FLAGS | (w))

static lv_obj_t *s_input_layer = NULL;
static lv_obj_t *s_prompt = NULL;
static lv_obj_t *s_prompt_msg = NULL;
static lv_obj_t *s_password_area = NULL;
static lv_obj_t *s_password_eye_label = NULL;
static lv_obj_t *s_keyboard = NULL;
static wifi_service_scan_result_t s_selected;
static cipher_wifi_input_connect_cb_t s_connect_cb = NULL;
static void *s_connect_user_data = NULL;

static const char *s_wifi_kb_map_lower[] = {
    "a", "b", "c", "d", "e", "f", "g", "\n",
    "h", "i", "j", "k", "l", "m", "n", "\n",
    "o", "p", "q", "r", "s", "t", "u", "\n",
    "v", "w", "x", "y", "z", "@", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "1#", "-", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_lower[] = {
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, CIPHER_WIFI_KB_CTRL(1),
    CIPHER_WIFI_KB_CTRL(2), CIPHER_WIFI_KB_CTRL(2), 1, 4,
    CIPHER_WIFI_KB_CTRL(2)
};

static const char *s_wifi_kb_map_upper[] = {
    "A", "B", "C", "D", "E", "F", "G", "\n",
    "H", "I", "J", "K", "L", "M", "N", "\n",
    "O", "P", "Q", "R", "S", "T", "U", "\n",
    "V", "W", "X", "Y", "Z", "@", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "1#", "-", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_upper[] = {
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, CIPHER_WIFI_KB_CTRL(1),
    CIPHER_WIFI_KB_CTRL(2), CIPHER_WIFI_KB_CTRL(2), 1, 4,
    CIPHER_WIFI_KB_CTRL(2)
};

static const char *s_wifi_kb_map_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "\n",
    "8", "9", "0", "!", "?", "#", "$", "\n",
    "%", "&", "*", ".", ",", ":", ";", "\n",
    "/", "\\", "'", "\"", "(", ")", "+", "\n",
    "abc", "ABC", "_", "-", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_special[] = {
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1,
    CIPHER_WIFI_KB_CTRL(2), CIPHER_WIFI_KB_CTRL(2), 1, 1, 3,
    CIPHER_WIFI_KB_CTRL(2), CIPHER_WIFI_KB_CTRL(2)
};

static void cipher_wifi_input_close_async(void);

/**
 * @brief 查询静态输入层引用是否存在。
 */
uint8_t cipher_wifi_input_is_open(void)
{
    return s_input_layer != NULL ? 1U : 0U;
}

/**
 * @brief 输入提示标签存在时复制状态文字及颜色。
 */
void cipher_wifi_input_set_message(const char *text, uint32_t color)
{
    if ((s_prompt_msg == NULL) || (text == NULL)) {
        return;
    }
    lv_label_set_text(s_prompt_msg, text);
    lv_obj_set_style_text_color(s_prompt_msg, lv_color_hex(color), 0);
}

/**
 * @brief 只清空输入层对象、回调及候选网络引用，不删除实际对象。
 */
void cipher_wifi_input_reset(void)
{
    s_input_layer = NULL;
    s_prompt = NULL;
    s_prompt_msg = NULL;
    s_password_area = NULL;
    s_password_eye_label = NULL;
    s_keyboard = NULL;
    s_connect_cb = NULL;
    s_connect_user_data = NULL;
    memset(&s_selected, 0, sizeof(s_selected));
}

/**
 * @brief 同步删除当前密码层并清空候选网络及回调借用。
 */
void cipher_wifi_input_close(void)
{
    if (s_input_layer != NULL) {
        lv_obj_delete(s_input_layer);
    }
    cipher_wifi_input_reset();
}

/**
 * @brief 预约当前层异步删除后立即撤销全局引用，不继续保留密码上下文。
 */
static void cipher_wifi_input_close_async(void)
{
    if (s_input_layer != NULL) {
        lv_obj_delete_async(s_input_layer);
    }
    cipher_wifi_input_reset();
}

/**
 * @brief 取消事件只预约密码层异步关闭。
 * @param[in] e 取消事件，当前未读取载荷。
 */
static void cipher_wifi_input_cancel_event(lv_event_t *e)
{
    (void)e;
    cipher_wifi_input_close_async();
}

/**
 * @brief 密码非空时同步调用连接受理回调，接受后清密码并延后删除输入层。
 * @param[in] e 确认事件，当前未读取载荷。
 * @note 回调只借用本次 SSID／密码并须返回前复制，不应在回调内删除当前输入层。非零是受理，不代表 DHCP 或保存完成。
 */
static void cipher_wifi_input_connect_event(lv_event_t *e)
{
    const char *pwd;

    (void)e;
    if ((s_password_area == NULL) || (s_selected.ssid[0] == '\0')) {
        return;
    }
    pwd = lv_textarea_get_text(s_password_area);
    if ((pwd == NULL) || (pwd[0] == '\0')) {
        cipher_wifi_input_set_message("Enter password", CIPHER_WARN);
        return;
    }
    if ((s_connect_cb == NULL) ||
        (s_connect_cb(s_selected.ssid, pwd, s_connect_user_data) == 0U)) {
        return;
    }
    lv_textarea_set_text(s_password_area, "");
    cipher_wifi_input_close_async();
}

/**
 * @brief 文本区点击／聚焦关联键盘，READY 受理连接，CANCEL 延后关闭。
 * @param[in] e 文本输入事件。
 */
static void cipher_wifi_input_password_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *textarea = lv_event_get_target(e);

    if ((code == LV_EVENT_CLICKED) || (code == LV_EVENT_FOCUSED)) {
        if (s_keyboard != NULL) {
            lv_keyboard_set_textarea(s_keyboard, textarea);
            lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_to_index(s_keyboard, -1);
        }
    } else if (code == LV_EVENT_READY) {
        cipher_wifi_input_connect_event(e);
    } else if (code == LV_EVENT_CANCEL) {
        cipher_wifi_input_close_async();
    }
}

/**
 * @brief 切换密码掩码及操作图标，恢复文本焦点与键盘关联。
 * @param[in] e 密码可见性按钮事件，当前未读取载荷。
 */
static void cipher_wifi_input_eye_event(lv_event_t *e)
{
    bool is_hidden;

    (void)e;
    if ((s_password_area == NULL) || (s_password_eye_label == NULL)) {
        return;
    }

    is_hidden = lv_textarea_get_password_mode(s_password_area);
    lv_textarea_set_password_mode(s_password_area, is_hidden ? false : true);
    lv_label_set_text(s_password_eye_label,
                      is_hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    lv_obj_add_state(s_password_area, LV_STATE_FOCUSED);
    if (s_keyboard != NULL) {
        lv_keyboard_set_textarea(s_keyboard, s_password_area);
        lv_obj_move_to_index(s_keyboard, -1);
    }
}

/**
 * @brief 创建输入卡内固定高操作按钮并绑定持久点击回调。
 * @param[in,out] parent 按钮父卡片。
 * @param[in] text 本次传给文字构造器复制的文字。
 * @param[in] x 局部横坐标。
 * @param[in] y 局部纵坐标。
 * @param[in] w 按钮宽度。
 * @param[in] accent 边框及文字 RGB 强调色。
 * @param[in] cb 持续有效的点击回调。
 * @return 成功返回父树拥有的按钮，按钮创建失败为 NULL；文字子对象不单独汇总错误。
 */
static lv_obj_t *cipher_wifi_input_action_button(lv_obj_t *parent,
                                                 const char *text,
                                                 int x, int y, int w,
                                                 uint32_t accent,
                                                 lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);

    if (btn == NULL) {
        return NULL;
    }
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, 28);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x03101B), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    cipher_text(btn, text, 0, 7, w, &lv_font_montserrat_12,
                accent, LV_TEXT_ALIGN_CENTER);
    return btn;
}

/**
 * @brief 设置密码文本区的旧根主题。
 * @param[in,out] textarea 借用文本区，NULL 时忽略。
 */
static void cipher_wifi_input_style_textarea(lv_obj_t *textarea)
{
    if (textarea == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(textarea, lv_color_hex(0x03101B), 0);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(textarea, 1, 0);
    lv_obj_set_style_border_color(textarea, lv_color_hex(CIPHER_ACCENT), 0);
    lv_obj_set_style_radius(textarea, 6, 0);
    lv_obj_set_style_text_font(textarea, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(textarea, lv_color_hex(CIPHER_TEXT), 0);
}

/**
 * @brief 创建密码可见性操作及图标，引用留给输入层。
 * @return 按钮及图标创建成功为 1，否则为 0；部分按钮交层清理。
 */
static uint8_t cipher_wifi_input_create_eye_button(void)
{
    lv_obj_t *btn = lv_btn_create(s_prompt);

    if (btn == NULL) {
        return 0U;
    }
    lv_obj_set_pos(btn, 246, 64);
    lv_obj_set_size(btn, 32, 38);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x03101B), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(CIPHER_ACCENT), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cipher_wifi_input_eye_event,
                        LV_EVENT_CLICKED, NULL);

    s_password_eye_label = cipher_text(btn, LV_SYMBOL_EYE_OPEN,
                                       0, 11, 32, &lv_font_montserrat_14,
                                       CIPHER_ACCENT, LV_TEXT_ALIGN_CENTER);
    return s_password_eye_label != NULL ? 1U : 0U;
}

/**
 * @brief 创建全屏不透明且可接管输入的密码层，并移到前景。
 * @param[in,out] parent 输入层父容器。
 * @return 成功返回父树拥有的层，失败为 NULL。
 */
static lv_obj_t *cipher_wifi_input_create_layer(lv_obj_t *parent)
{
    lv_obj_t *layer = lv_obj_create(parent);

    if (layer == NULL) {
        return NULL;
    }
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_size(layer, MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_obj_set_style_bg_color(layer, lv_color_hex(0x020712), 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(layer, 0, 0);
    lv_obj_set_style_radius(layer, 0, 0);
    lv_obj_set_style_pad_all(layer, 0, 0);
    lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(layer, -1);
    return layer;
}

/**
 * @brief 创建当前候选网络的输入提示卡及网络／认证说明。
 * @return 卡片创建成功为 1，否则为 0；内部文字分配不单独反馈。
 */
static uint8_t cipher_wifi_input_create_prompt_card(void)
{
    s_prompt = cipher_row(s_input_layer, 12, CIPHER_WIFI_PROMPT_Y,
                          296, CIPHER_WIFI_PROMPT_H);
    if (s_prompt == NULL) {
        return 0U;
    }
    cipher_rect(s_prompt, 12, 0, 104, 2, CIPHER_ACCENT, 0);
    cipher_text(s_prompt, "JOIN NETWORK", 18, 14, 170,
                &lv_font_montserrat_14, CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    cipher_text(s_prompt, s_selected.ssid, 18, 38, 244,
                d_ui_font_get_default(),
                CIPHER_ACCENT, LV_TEXT_ALIGN_LEFT);
    cipher_text(s_prompt, wifi_service_auth_name(s_selected.auth_mode),
                210, 14, 64, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_RIGHT);
    return 1U;
}

/**
 * @brief 创建有界单行密码文本区、全部事件处理及可见性按钮。
 * @return 必要对象及可见性按钮成功为 1，否则为 0。
 */
static uint8_t cipher_wifi_input_create_password_area(void)
{
    s_password_area = lv_textarea_create(s_prompt);
    if (s_password_area == NULL) {
        return 0U;
    }
    lv_obj_set_pos(s_password_area, 18, 64);
    lv_obj_set_size(s_password_area, 222, 38);
    lv_textarea_set_one_line(s_password_area, true);
    lv_textarea_set_password_mode(s_password_area, true);
    lv_textarea_set_max_length(s_password_area, 64);
    lv_textarea_set_placeholder_text(s_password_area, "Password");
    cipher_wifi_input_style_textarea(s_password_area);
    lv_obj_add_event_cb(s_password_area, cipher_wifi_input_password_event,
                        LV_EVENT_ALL, NULL);
    return cipher_wifi_input_create_eye_button();
}

/**
 * @brief 尝试创建提示、连接及取消操作，当前 void 路径不汇总分配结果。
 */
static void cipher_wifi_input_create_actions(void)
{
    s_prompt_msg = cipher_text(s_prompt, "OK to connect",
                               18, 108, 110, &lv_font_montserrat_12,
                               CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    cipher_wifi_input_action_button(s_prompt, "Connect", 142, 106, 66,
                                    CIPHER_ACCENT_2,
                                    cipher_wifi_input_connect_event);
    cipher_wifi_input_action_button(s_prompt, "Cancel", 214, 106, 66,
                                    CIPHER_WARN,
                                    cipher_wifi_input_cancel_event);
}

/**
 * @brief 安装静态小写、大写及符号键盘布局。
 * @param[in,out] keyboard 借用键盘，NULL 时忽略。
 */
static void cipher_wifi_input_apply_keyboard_map(lv_obj_t *keyboard)
{
    if (keyboard == NULL) {
        return;
    }
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        s_wifi_kb_map_lower, s_wifi_kb_ctrl_lower);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        s_wifi_kb_map_upper, s_wifi_kb_ctrl_upper);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL,
                        s_wifi_kb_map_special, s_wifi_kb_ctrl_special);
}

/**
 * @brief 设置旧根键盘项间距及拉丁文字体。
 * @param[in,out] keyboard 借用键盘，NULL 时忽略。
 */
static void cipher_wifi_input_style_keyboard(lv_obj_t *keyboard)
{
    if (keyboard == NULL) {
        return;
    }
    lv_obj_set_style_pad_row(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, 3, LV_PART_MAIN);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14,
                               LV_PART_ITEMS);
}

/**
 * @brief 创建键盘并显式左上定位，关联已存在密码文本区。
 * @note 失败保留已创建输入卡，当前不发布整层可重试创建状态。
 */
static void cipher_wifi_input_create_keyboard(void)
{
    s_keyboard = lv_keyboard_create(s_input_layer);
    if (s_keyboard == NULL) {
        return;
    }
    lv_obj_set_size(s_keyboard, MY_DISP_HOR_RES, CIPHER_WIFI_KEYBOARD_H);
    /* lv_keyboard_create 默认 bottom align，必须改回左上角坐标系。 */
    lv_obj_align(s_keyboard, LV_ALIGN_TOP_LEFT, 0, CIPHER_WIFI_KEYBOARD_Y);
    cipher_wifi_input_apply_keyboard_map(s_keyboard);
    cipher_wifi_input_style_keyboard(s_keyboard);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_keyboard, s_password_area);
    lv_obj_move_to_index(s_keyboard, -1);
}

/**
 * @brief 关闭旧密码层、复制候选网络并创建输入卡与静态布局键盘。
 */
void cipher_wifi_input_open(lv_obj_t *parent,
                            const wifi_service_scan_result_t *item,
                            cipher_wifi_input_connect_cb_t connect_cb,
                            void *user_data)
{
    if ((parent == NULL) || (item == NULL)) {
        return;
    }
    cipher_wifi_input_close();
    s_selected = *item;
    s_connect_cb = connect_cb;
    s_connect_user_data = user_data;
    s_input_layer = cipher_wifi_input_create_layer(parent);
    if (s_input_layer == NULL) {
        cipher_wifi_input_reset();
        return;
    }
    if ((cipher_wifi_input_create_prompt_card() == 0U) ||
        (cipher_wifi_input_create_password_area() == 0U)) {
        cipher_wifi_input_close();
        return;
    }
    cipher_wifi_input_create_actions();
    cipher_wifi_input_create_keyboard();
    lv_obj_add_state(s_password_area, LV_STATE_FOCUSED);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_CIPHER_PANEL) && D_FEATURE_WIFI */
