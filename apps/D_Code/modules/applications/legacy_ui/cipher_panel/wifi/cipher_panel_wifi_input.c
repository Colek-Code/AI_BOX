/**
 * @file cipher_panel_wifi_input.c
 * @brief Password input panel for the cipher panel WiFi page.
 */
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

uint8_t cipher_wifi_input_is_open(void)
{
    return s_input_layer != NULL ? 1U : 0U;
}

void cipher_wifi_input_set_message(const char *text, uint32_t color)
{
    if ((s_prompt_msg == NULL) || (text == NULL)) {
        return;
    }
    lv_label_set_text(s_prompt_msg, text);
    lv_obj_set_style_text_color(s_prompt_msg, lv_color_hex(color), 0);
}

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

void cipher_wifi_input_close(void)
{
    if (s_input_layer != NULL) {
        lv_obj_delete(s_input_layer);
    }
    cipher_wifi_input_reset();
}

static void cipher_wifi_input_close_async(void)
{
    if (s_input_layer != NULL) {
        lv_obj_delete_async(s_input_layer);
    }
    cipher_wifi_input_reset();
}

static void cipher_wifi_input_cancel_event(lv_event_t *e)
{
    (void)e;
    cipher_wifi_input_close_async();
}

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
