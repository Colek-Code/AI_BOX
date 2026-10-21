/**
 * @file product_ui_settings_connectivity_pages.c
 * @brief WiFi detail page (screen provisioning) and Bluetooth placeholder.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/settings/product_ui_settings_details.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_details_common.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/shared/text/product_ui_text.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

#if D_FEATURE_WIFI
#include "modules/services/wifi/wifi_service.h"
#endif

static product_ui_page_view_t s_wifi_page_view;
static product_ui_page_view_t s_bluetooth_page_view;

#if D_FEATURE_WIFI

#define PRODUCT_UI_WIFI_SCAN_MAX        WIFI_SERVICE_SCAN_MAX_RESULTS
#define PRODUCT_UI_WIFI_STATUS_FORCE_MS 2000U
#define PRODUCT_UI_WIFI_SSID_SHOW       WIFI_SERVICE_SCAN_SSID_MAX
#define PRODUCT_UI_WIFI_SUB_SHOW        40U
#define PRODUCT_UI_WIFI_PWD_MAX         PRODUCT_UI_CONNECTION_PWD_MAX
/* Landscape 320x240: large QWERTY keys (4 rows). */
#define PRODUCT_UI_WIFI_KB_H            152
#define PRODUCT_UI_WIFI_KB_Y            (PRODUCT_UI_SCREEN_H - PRODUCT_UI_WIFI_KB_H)
#define PRODUCT_UI_WIFI_TA_Y            40
#define PRODUCT_UI_WIFI_TA_H            40
#define PRODUCT_UI_WIFI_TA_X            12
#define PRODUCT_UI_WIFI_EYE_W           PRODUCT_UI_TOUCH_MIN
#define PRODUCT_UI_WIFI_EYE_GAP         8
/* 320 - pad12 - eye36 - gap8 - pad12 */
#define PRODUCT_UI_WIFI_TA_W \
    (PRODUCT_UI_SCREEN_W - (PRODUCT_UI_WIFI_TA_X * 2) - \
     PRODUCT_UI_WIFI_EYE_W - PRODUCT_UI_WIFI_EYE_GAP)
#define PRODUCT_UI_WIFI_EYE_X \
    (PRODUCT_UI_WIFI_TA_X + PRODUCT_UI_WIFI_TA_W + PRODUCT_UI_WIFI_EYE_GAP)
#define PRODUCT_UI_WIFI_KB_CTRL(w)      (LV_KEYBOARD_CTRL_BUTTON_FLAGS | (w))

typedef struct {
    product_ui_page_view_t *page;
    lv_obj_t *status_label;
    lv_obj_t *scan_label;
    lv_obj_t *empty_label;
    lv_obj_t *pwd_layer;
    uint8_t is_pull_held;
    lv_obj_t *pwd_title;
    lv_obj_t *pwd_ta;
    lv_obj_t *pwd_eye_btn;
    lv_obj_t *pwd_eye_label;
    lv_obj_t *pwd_kb;
    product_ui_virtual_list_t list;
    product_ui_list_item_t slot_rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    char slot_title[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT][PRODUCT_UI_WIFI_SSID_SHOW];
    char slot_sub[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT][PRODUCT_UI_WIFI_SUB_SHOW];
    wifi_service_scan_result_t results[PRODUCT_UI_WIFI_SCAN_MAX];
    wifi_service_scan_result_t pending;
    uint8_t result_count;
    uint8_t list_item_count;
    uint8_t list_active;
    uint8_t expect_connect_toast;
    uint8_t cred_save_fail_toasted;
    uint32_t seen_scan_version;
    uint32_t seen_status_version;
    uint32_t status_force_ms;
} product_ui_wifi_page_t;

static product_ui_wifi_page_t s_wifi;

static void product_ui_wifi_render_status(void);

static uint8_t product_ui_wifi_is_busy_connecting(void)
{
    wifi_service_status_t status;

    wifi_service_get_status(&status);
    return ((status.state == WIFI_SERVICE_CONN_CONNECTING) ||
            (status.state == WIFI_SERVICE_CONN_ASSOCIATED)) ? 1U : 0U;
}

static const char *product_ui_wifi_scan_text(wifi_service_scan_state_t state)
{
    switch (state) {
    case WIFI_SERVICE_SCAN_SCANNING:
        return "正在扫描...";
    case WIFI_SERVICE_SCAN_BUSY:
        return "WiFi 忙，请稍后";
    case WIFI_SERVICE_SCAN_NOT_READY:
        return "WiFi 未就绪";
    case WIFI_SERVICE_SCAN_TIMEOUT:
        return "扫描超时";
    case WIFI_SERVICE_SCAN_ERROR:
        return "扫描失败";
    case WIFI_SERVICE_SCAN_DONE:
        return "扫描完成";
    default:
        return "点击右上角刷新";
    }
}

static const char *product_ui_wifi_error_text(wifi_service_conn_error_t error)
{
    switch (error) {
    case WIFI_SERVICE_ERR_NOT_READY:
        return "WiFi 未就绪";
    case WIFI_SERVICE_ERR_BUSY:
        return "WiFi 忙";
    case WIFI_SERVICE_ERR_NO_SAVED:
        return "无已保存网络";
    case WIFI_SERVICE_ERR_NOT_FOUND:
        return "找不到该网络";
    case WIFI_SERVICE_ERR_ASSOC_FAIL:
        return "关联失败（密码可能错误）";
    case WIFI_SERVICE_ERR_ASSOC_TIMEOUT:
        return "关联超时";
    case WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT:
        return "切换超时，旧网络未断开";
    case WIFI_SERVICE_ERR_DHCP_TIMEOUT:
        return "获取 IP 超时";
    case WIFI_SERVICE_ERR_DISCONNECTED:
        return "已断开";
    default:
        return "未知错误";
    }
}

/* QWERTY maps for 320x240 landscape (large keys, familiar 26-key order). */
static const char *s_wifi_kb_map_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "@", "-", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_lower[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 1, 1, 1, 1, PRODUCT_UI_WIFI_KB_CTRL(2),
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 4, PRODUCT_UI_WIFI_KB_CTRL(2)
};

static const char *s_wifi_kb_map_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "@", "-", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_upper[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 1, 1, 1, 1, PRODUCT_UI_WIFI_KB_CTRL(2),
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 4, PRODUCT_UI_WIFI_KB_CTRL(2)
};

static const char *s_wifi_kb_map_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "!", "?", "#", "$", "%", "&", "*", "/", "\\", "\n",
    "abc", ".", ",", ":", ";", "'", "\"", "(", ")", "\n",
    "ABC", "_", "+", "-", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_special[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 3,
    PRODUCT_UI_WIFI_KB_CTRL(2), PRODUCT_UI_WIFI_KB_CTRL(2)
};

static void product_ui_wifi_pwd_close(void)
{
    lv_obj_t *layer = s_wifi.pwd_layer;
    s_wifi.pwd_layer = NULL;
    s_wifi.pwd_title = NULL;
    s_wifi.pwd_ta = NULL;
    s_wifi.pwd_eye_btn = NULL;
    s_wifi.pwd_eye_label = NULL;
    s_wifi.pwd_kb = NULL;
    memset(&s_wifi.pending, 0, sizeof(s_wifi.pending));
    product_ui_control_center_release_pull(&s_wifi.is_pull_held);
    if (layer != NULL) lv_obj_delete(layer);
}

static void product_ui_wifi_pwd_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_wifi.pwd_layer) return;
    s_wifi.pwd_layer = NULL;
    product_ui_wifi_pwd_close();
}

static void product_ui_wifi_list_teardown(void)
{
    if (s_wifi.list.root != NULL) {
        lv_obj_delete(s_wifi.list.root);
    }
    s_wifi.status_label = NULL;
    s_wifi.scan_label = NULL;
    s_wifi.empty_label = NULL;
    s_wifi.list_active = 0U;
    s_wifi.list_item_count = 0U;
}

static void product_ui_wifi_start_connect(const char *ssid, const char *pwd)
{
    product_ui_connection_wifi_connect_t req;
    d_err_t err;

    if (product_ui_wifi_is_busy_connecting() != 0U) {
        product_ui_toast_show("正在连接，请稍后", 1200U);
        return;
    }
    memset(&req, 0, sizeof(req));
    (void)product_ui_utf8_copy(req.ssid, sizeof(req.ssid), ssid);
    if (pwd != NULL) {
        (void)product_ui_utf8_copy(req.pwd, sizeof(req.pwd), pwd);
    }
    if (req.ssid[0] == '\0') {
        product_ui_toast_show("连接失败", 1500U);
        return;
    }
    err = (d_err_t)product_ui_connection_command(
        PRODUCT_UI_CONNECTION_WIFI_CONNECT, &req, (uint16_t)sizeof(req));
    if (err == D_OK) {
        s_wifi.expect_connect_toast = 1U;
        product_ui_toast_show("正在连接...", 1200U);
    } else if (err == D_ERR_BUSY) {
        product_ui_toast_show("WiFi 忙，请稍后", 1500U);
    } else if (err == D_ERR_NOT_READY) {
        product_ui_toast_show("WiFi 未就绪", 1500U);
    } else {
        product_ui_toast_show("连接失败", 1500U);
    }
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    product_ui_wifi_render_status();
}

static void product_ui_wifi_pwd_ok_cb(lv_event_t *event)
{
    const char *pwd;

    (void)event;
    if ((s_wifi.pwd_ta == NULL) || (s_wifi.pending.ssid[0] == '\0')) {
        product_ui_wifi_pwd_close();
        return;
    }
    pwd = lv_textarea_get_text(s_wifi.pwd_ta);
    if ((pwd == NULL) || (pwd[0] == '\0')) {
        product_ui_toast_show("请输入密码", 1200U);
        return;
    }
    product_ui_wifi_start_connect(s_wifi.pending.ssid, pwd);
    product_ui_wifi_pwd_close();
}

static void product_ui_wifi_pwd_cancel_cb(lv_event_t *event)
{
    (void)event;
    product_ui_wifi_pwd_close();
}

static void product_ui_wifi_pwd_kb_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_READY) {
        product_ui_wifi_pwd_ok_cb(event);
        return;
    }
    if (code == LV_EVENT_CANCEL) {
        product_ui_wifi_pwd_close();
    }
}

/*
 * Cipher-style visibility toggle: password_mode on → bullets + open-eye icon
 * (tap to reveal); off → plain text + closed-eye icon (tap to hide).
 */
static void product_ui_wifi_pwd_eye_cb(lv_event_t *event)
{
    uint8_t is_hidden;

    (void)event;
    if ((s_wifi.pwd_ta == NULL) || (s_wifi.pwd_eye_label == NULL)) {
        return;
    }
    is_hidden = lv_textarea_get_password_mode(s_wifi.pwd_ta) ? 1U : 0U;
    lv_textarea_set_password_mode(s_wifi.pwd_ta, is_hidden == 0U);
    lv_label_set_text(s_wifi.pwd_eye_label,
                      (is_hidden != 0U) ? LV_SYMBOL_EYE_CLOSE :
                                          LV_SYMBOL_EYE_OPEN);
    lv_obj_add_state(s_wifi.pwd_ta, LV_STATE_FOCUSED);
    if (s_wifi.pwd_kb != NULL) {
        lv_keyboard_set_textarea(s_wifi.pwd_kb, s_wifi.pwd_ta);
    }
}

static void product_ui_wifi_pwd_apply_kb_map(lv_obj_t *keyboard)
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

static void product_ui_wifi_pwd_style_keyboard(lv_obj_t *keyboard)
{
    if (keyboard == NULL) {
        return;
    }
    lv_obj_set_style_pad_row(keyboard, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14,
                               LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, PRODUCT_UI_RADIUS_MD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
}

static int product_ui_wifi_pwd_open(const wifi_service_scan_result_t *item)
{
    lv_obj_t *label;
    lv_obj_t *cancel;
    char title[48];
    char ssid_show[PRODUCT_UI_WIFI_SSID_SHOW];

    if ((item == NULL) || (s_wifi.page == NULL) ||
        (s_wifi.page->root == NULL)) {
        return -1;
    }
    product_ui_wifi_pwd_close();
    s_wifi.pending = *item;

    s_wifi.pwd_layer = lv_obj_create(s_wifi.page->root);
    if (s_wifi.pwd_layer == NULL) {
        memset(&s_wifi.pending, 0, sizeof(s_wifi.pending));
        return -1;
    }
    product_ui_control_center_hold_pull(&s_wifi.is_pull_held);
    lv_obj_add_event_cb(s_wifi.pwd_layer, product_ui_wifi_pwd_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(s_wifi.pwd_layer);
    lv_obj_set_size(s_wifi.pwd_layer, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(s_wifi.pwd_layer, 0, 0);
    lv_obj_set_style_bg_color(s_wifi.pwd_layer,
                              product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    lv_obj_set_style_bg_opa(s_wifi.pwd_layer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_wifi.pwd_layer, LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_wifi.pwd_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_wifi.pwd_layer);

    cancel = lv_button_create(s_wifi.pwd_layer);
    if (cancel == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_remove_style_all(cancel);
    lv_obj_set_size(cancel, PRODUCT_UI_TOUCH_MIN, PRODUCT_UI_TOUCH_MIN);
    lv_obj_set_pos(cancel, 4, 2);
    lv_obj_add_style(cancel, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_event_cb(cancel, product_ui_wifi_pwd_cancel_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(cancel);
    if (label == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);

    s_wifi.pwd_title = lv_label_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_title == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    (void)product_ui_utf8_copy(ssid_show, sizeof(ssid_show), item->ssid);
    lv_snprintf(title, sizeof(title), "输入密码: %s", ssid_show);
    lv_obj_add_style(s_wifi.pwd_title, product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
    lv_obj_set_pos(s_wifi.pwd_title, 44, 10);
    lv_obj_set_width(s_wifi.pwd_title, 268);
    lv_obj_set_height(s_wifi.pwd_title, product_ui_font(PRODUCT_UI_FONT_BODY)->line_height);
    lv_label_set_long_mode(s_wifi.pwd_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_wifi.pwd_title, title);

    s_wifi.pwd_ta = lv_textarea_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_ta == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_set_size(s_wifi.pwd_ta, PRODUCT_UI_WIFI_TA_W, PRODUCT_UI_WIFI_TA_H);
    lv_obj_set_pos(s_wifi.pwd_ta, PRODUCT_UI_WIFI_TA_X, PRODUCT_UI_WIFI_TA_Y);
    lv_textarea_set_one_line(s_wifi.pwd_ta, true);
    lv_textarea_set_password_mode(s_wifi.pwd_ta, true);
    lv_textarea_set_max_length(s_wifi.pwd_ta, PRODUCT_UI_WIFI_PWD_MAX - 1);
    lv_textarea_set_placeholder_text(s_wifi.pwd_ta, "WiFi 密码");
    lv_obj_set_style_text_font(s_wifi.pwd_ta,
                               product_ui_font(PRODUCT_UI_FONT_BODY), 0);
    lv_obj_set_style_text_font(s_wifi.pwd_ta,
                               product_ui_font(PRODUCT_UI_FONT_BODY),
                               LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(s_wifi.pwd_ta,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_wifi.pwd_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wifi.pwd_ta, 1, 0);
    lv_obj_set_style_border_color(s_wifi.pwd_ta,
                                  product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
    lv_obj_set_style_radius(s_wifi.pwd_ta, PRODUCT_UI_RADIUS_MD, 0);
    lv_obj_set_style_pad_hor(s_wifi.pwd_ta, 10, 0);
    lv_obj_set_style_pad_ver(s_wifi.pwd_ta, 8, 0);

    /* Show/hide password (same symbol semantics as Cipher portrait input). */
    s_wifi.pwd_eye_btn = lv_button_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_eye_btn == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_remove_style_all(s_wifi.pwd_eye_btn);
    lv_obj_set_size(s_wifi.pwd_eye_btn, PRODUCT_UI_WIFI_EYE_W,
                    PRODUCT_UI_WIFI_TA_H);
    lv_obj_set_pos(s_wifi.pwd_eye_btn, PRODUCT_UI_WIFI_EYE_X,
                   PRODUCT_UI_WIFI_TA_Y);
    lv_obj_add_style(s_wifi.pwd_eye_btn,
                     product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_set_style_border_width(s_wifi.pwd_eye_btn, 1, 0);
    lv_obj_set_style_border_color(s_wifi.pwd_eye_btn,
                                  product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
    lv_obj_set_style_radius(s_wifi.pwd_eye_btn, PRODUCT_UI_RADIUS_MD, 0);
    lv_obj_add_event_cb(s_wifi.pwd_eye_btn, product_ui_wifi_pwd_eye_cb,
                        LV_EVENT_CLICKED, NULL);
    s_wifi.pwd_eye_label = lv_label_create(s_wifi.pwd_eye_btn);
    if (s_wifi.pwd_eye_label == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_label_set_text(s_wifi.pwd_eye_label, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(s_wifi.pwd_eye_label);
    lv_obj_set_style_text_color(s_wifi.pwd_eye_label,
                                product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);

    s_wifi.pwd_kb = lv_keyboard_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_kb == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_set_size(s_wifi.pwd_kb, PRODUCT_UI_SCREEN_W, PRODUCT_UI_WIFI_KB_H);
    /* Override default bottom-align so layout matches Cipher top-left coords. */
    lv_obj_align(s_wifi.pwd_kb, LV_ALIGN_TOP_LEFT, 0, PRODUCT_UI_WIFI_KB_Y);
    product_ui_wifi_pwd_apply_kb_map(s_wifi.pwd_kb);
    product_ui_wifi_pwd_style_keyboard(s_wifi.pwd_kb);
    lv_keyboard_set_textarea(s_wifi.pwd_kb, s_wifi.pwd_ta);
    lv_keyboard_set_mode(s_wifi.pwd_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(s_wifi.pwd_kb, product_ui_wifi_pwd_kb_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_wifi.pwd_kb, product_ui_wifi_pwd_kb_cb,
                        LV_EVENT_CANCEL, NULL);
    return 0;
}

static int product_ui_wifi_row_bind(uint16_t item_index, uint8_t slot_index,
                                    product_ui_row_spec_t *row_spec,
                                    void *user_data)
{
    product_ui_wifi_page_t *page = user_data;
    const wifi_service_scan_result_t *item;

    if ((page == NULL) || (row_spec == NULL) ||
        (slot_index >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) ||
        (item_index >= page->result_count)) {
        return -1;
    }
    item = &page->results[item_index];
    (void)product_ui_utf8_copy(page->slot_title[slot_index],
                               sizeof(page->slot_title[slot_index]),
                               item->ssid);
    lv_snprintf(page->slot_sub[slot_index], sizeof(page->slot_sub[slot_index]),
                "%d dBm · %u%% · %s",
                (int)item->rssi,
                (unsigned)item->signal_strength,
                wifi_service_auth_name(item->auth_mode));
    page->slot_rows[slot_index] = (product_ui_list_item_t) {
        .item_id = "wifi_ap",
        .title = page->slot_title[slot_index],
        .icon_id = PRODUCT_UI_ICON_WIFI,
        .accent = PRODUCT_UI_COLOR_SUCCESS,
    };
    *row_spec = (product_ui_row_spec_t) {
        .item = &page->slot_rows[slot_index],
        .subtitle = page->slot_sub[slot_index],
        .interaction = PRODUCT_UI_ROW_INTERACTION_NAVIGATE,
        /* PRODUCT_UI_VIRTUAL_ACTION_INJECTED */
        .trailing = PRODUCT_UI_ROW_TRAILING_CHEVRON,
        .is_enabled = 1U,
    };
    return 0;
}

static void product_ui_wifi_row_action(uint16_t item_index, void *user_data)
{
    product_ui_wifi_page_t *page = user_data;
    const wifi_service_scan_result_t *item;

    if ((page == NULL) || (item_index >= page->result_count)) {
        return;
    }
    if (product_ui_wifi_is_busy_connecting() != 0U) {
        product_ui_toast_show("正在连接，请稍后", 1200U);
        return;
    }
    item = &page->results[item_index];
    if (wifi_service_auth_requires_password(item->auth_mode) == 0U) {
        product_ui_wifi_start_connect(item->ssid, "");
        return;
    }
    if (product_ui_wifi_pwd_open(item) != 0) {
        product_ui_toast_show("无法打开密码输入", 1500U);
    }
}

static void product_ui_wifi_reconnect_cb(lv_event_t *event)
{
    d_err_t err;
    wifi_service_status_t status;

    (void)event;
    if (product_ui_wifi_is_busy_connecting() != 0U) {
        product_ui_toast_show("正在连接，请稍后", 1200U);
        return;
    }
    err = (d_err_t)product_ui_connection_command(
        PRODUCT_UI_CONNECTION_WIFI_RECONNECT, NULL, 0U);
    if (err == D_OK) {
        wifi_service_get_status(&status);
        if (status.state == WIFI_SERVICE_CONN_CONNECTED) {
            s_wifi.expect_connect_toast = 0U;
            product_ui_toast_show("已连接", 1000U);
        } else {
            s_wifi.expect_connect_toast = 1U;
            product_ui_toast_show("正在重连...", 1200U);
        }
    } else if (err == D_ERR_NOT_FOUND) {
        product_ui_toast_show("无已保存网络", 1500U);
    } else if (err == D_ERR_BUSY) {
        product_ui_toast_show("WiFi 忙，请稍后", 1500U);
    } else {
        product_ui_toast_show("重连失败", 1500U);
    }
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    product_ui_wifi_render_status();
}

static void product_ui_wifi_forget_cb(lv_event_t *event)
{
    d_err_t err;

    (void)event;
    product_ui_wifi_pwd_close();
    err = (d_err_t)product_ui_connection_command(
        PRODUCT_UI_CONNECTION_WIFI_FORGET, NULL, 0U);
    if (err == D_OK) {
        product_ui_toast_show("已忘记网络", 1200U);
    } else if (err == D_ERR_IO) {
        product_ui_toast_show("清除凭证失败", 1500U);
    } else {
        product_ui_toast_show("忘记网络失败", 1500U);
    }
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    product_ui_wifi_render_status();
}

static void product_ui_wifi_refresh_cb(lv_event_t *event)
{
    d_err_t err;

    (void)event;
    product_ui_wifi_pwd_close();
    err = (d_err_t)product_ui_connection_command(
        PRODUCT_UI_CONNECTION_WIFI_SCAN_START, NULL, 0U);
    if (err == D_OK) {
        product_ui_toast_show("开始扫描", 800U);
    } else if (err == D_ERR_BUSY) {
        product_ui_toast_show("WiFi 忙，请稍后", 1500U);
    } else {
        product_ui_toast_show("无法开始扫描", 1200U);
    }
    s_wifi.seen_scan_version = 0xFFFFFFFFU;
}

static void product_ui_wifi_render_status(void)
{
    wifi_service_status_t status;
    uint32_t version;
    char line[96];
    char ssid_show[PRODUCT_UI_WIFI_SSID_SHOW];
    if (s_wifi.status_label == NULL) {
        return;
    }
    version = wifi_service_get_status_version();
    if (version == s_wifi.seen_status_version) {
        return;
    }
    s_wifi.seen_status_version = version;
    wifi_service_get_status(&status);
    (void)product_ui_utf8_copy(ssid_show, sizeof(ssid_show), status.ssid);

    if (status.state == WIFI_SERVICE_CONN_CONNECTED) {
        if (status.cred_save_failed != 0U) {
            lv_snprintf(line, sizeof(line), "已连接: %s（未保存）",
                        ssid_show[0] != '\0' ? ssid_show : "WiFi");
            if (s_wifi.cred_save_fail_toasted == 0U) {
                s_wifi.cred_save_fail_toasted = 1U;
                product_ui_toast_show("已上网，但未保存密码", 2000U);
            }
        } else {
            s_wifi.cred_save_fail_toasted = 0U;
            lv_snprintf(line, sizeof(line), "已连接: %s  %d%%",
                        ssid_show[0] != '\0' ? ssid_show : "WiFi",
                        (int)status.signal_strength);
            if (s_wifi.expect_connect_toast != 0U) {
                s_wifi.expect_connect_toast = 0U;
                product_ui_toast_show("已连接", 1500U);
            }
        }
    } else if ((status.state == WIFI_SERVICE_CONN_CONNECTING) ||
               (status.state == WIFI_SERVICE_CONN_ASSOCIATED)) {
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "连接中: %s",
                    ssid_show[0] != '\0' ? ssid_show : "...");
    } else if ((status.state == WIFI_SERVICE_CONN_ERROR) &&
               (status.error == WIFI_SERVICE_ERR_NO_SAVED)) {
        if (status.has_saved_credentials != 0U) {
            lv_snprintf(line, sizeof(line), "已保存: %s（未连接）",
                        ssid_show[0] != '\0' ? ssid_show : "网络");
        } else {
            lv_snprintf(line, sizeof(line), "未连接（无已保存网络）");
        }
    } else if (status.state == WIFI_SERVICE_CONN_ERROR) {
        s_wifi.expect_connect_toast = 0U;
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "错误: %s",
                    product_ui_wifi_error_text(status.error));
    } else if (status.has_saved_credentials != 0U) {
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "已保存: %s（未连接）",
                    ssid_show[0] != '\0' ? ssid_show : "网络");
    } else {
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "未连接（可用手机 BLE 或本页配网）");
    }
    lv_label_set_text(s_wifi.status_label, line);
}

static void product_ui_wifi_show_list_empty(const char *message)
{
    if (s_wifi.empty_label == NULL) {
        return;
    }
    lv_label_set_text(s_wifi.empty_label, message != NULL ? message : "点击右上角刷新");
    lv_obj_clear_flag(s_wifi.empty_label, LV_OBJ_FLAG_HIDDEN);
    (void)product_ui_virtual_list_set_count(&s_wifi.list, 0U);
    s_wifi.list_active = 0U;
    s_wifi.list_item_count = 0U;
}

static int product_ui_wifi_list_rebuild(uint8_t count)
{
    if (s_wifi.list.root == NULL) {
        return -1;
    }
    lv_obj_add_flag(s_wifi.empty_label, LV_OBJ_FLAG_HIDDEN);
    s_wifi.list_active = 1U;
    s_wifi.list_item_count = count;
    return product_ui_virtual_list_set_count(&s_wifi.list, count);
}

static void product_ui_wifi_render_scan(void)
{
    wifi_service_scan_state_t state;
    uint32_t version;
    uint8_t count;

    if (s_wifi.list.root == NULL) {
        return;
    }
    state = wifi_service_scan_get_state();
    version = wifi_service_scan_get_version();
    if (version == s_wifi.seen_scan_version) {
        return;
    }
    count = wifi_service_scan_copy_results(s_wifi.results,
                                           PRODUCT_UI_WIFI_SCAN_MAX);
    s_wifi.result_count = count;

    if (s_wifi.scan_label != NULL) {
        if (state == WIFI_SERVICE_SCAN_DONE) {
            lv_label_set_text_fmt(s_wifi.scan_label, "发现 %u 个网络",
                                  (unsigned)count);
        } else {
            lv_label_set_text(s_wifi.scan_label,
                              product_ui_wifi_scan_text(state));
        }
    }

    if ((state != WIFI_SERVICE_SCAN_DONE) || (count == 0U)) {
        product_ui_wifi_show_list_empty(product_ui_wifi_scan_text(state));
        s_wifi.seen_scan_version = version;
        return;
    }

    if ((s_wifi.list_active != 0U) &&
        (s_wifi.list_item_count == count) &&
        (s_wifi.list.root != NULL)) {
        if (product_ui_virtual_list_refresh(&s_wifi.list) == 0) {
            s_wifi.seen_scan_version = version;
        }
        return;
    }

    if (product_ui_wifi_list_rebuild(count) == 0) {
        s_wifi.seen_scan_version = version;
    }
}

static int product_ui_wifi_build_actions(lv_obj_t *content)
{
    lv_obj_t *row;
    lv_obj_t *btn;
    const char *labels[2] = { "重连已保存", "忘记网络" };
    lv_event_cb_t cbs[2] = {
        product_ui_wifi_reconnect_cb,
        product_ui_wifi_forget_cb,
    };
    uint8_t i;

    row = lv_obj_create(content);
    if (row == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD),
                    PRODUCT_UI_TOUCH_MIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    /* 固定操作条：禁止参与父级滚动链，避免被拖走后回不来。 */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (i = 0U; i < 2U; i++) {
        btn = lv_button_create(row);
        if (btn == NULL) {
            return -1;
        }
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 140, PRODUCT_UI_TOUCH_MIN);
        lv_obj_add_style(btn,
                         product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, NULL);
        {
            lv_obj_t *lab = lv_label_create(btn);
            if (lab != NULL) {
                lv_label_set_text(lab, labels[i]);
                lv_obj_center(lab);
                lv_obj_add_style(lab,
                                 product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
            }
        }
    }
    return 0;
}

static void product_ui_wifi_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
    product_ui_virtual_list_update(&s_wifi.list, elapsed_ms);
    s_wifi.status_force_ms += elapsed_ms;
    if (s_wifi.status_force_ms >= PRODUCT_UI_WIFI_STATUS_FORCE_MS) {
        s_wifi.status_force_ms = 0U;
        s_wifi.seen_status_version = 0xFFFFFFFFU;
    }
    wifi_service_scan_update(elapsed_ms);
    product_ui_wifi_render_status();
    product_ui_wifi_render_scan();
}

static void product_ui_wifi_page_destroy(void)
{
    product_ui_wifi_pwd_close();
    product_ui_wifi_list_teardown();
    product_ui_page_view_reset(&s_wifi_page_view);
    memset(&s_wifi, 0, sizeof(s_wifi));
    s_wifi.seen_scan_version = 0xFFFFFFFFU;
    s_wifi.seen_status_version = 0xFFFFFFFFU;
}

int product_ui_settings_wifi_page_create(lv_obj_t *parent)
{
    product_ui_page_spec_t spec = {
        .title = "WiFi",
        .back_title = "设置",
        .show_back = 1,
        .role = PRODUCT_UI_PAGE_ROLE_SETTINGS,
        .nav_action_symbol = LV_SYMBOL_REFRESH,
        .nav_action_cb = product_ui_wifi_refresh_cb,
    };
    const product_ui_virtual_list_spec_t list_spec = {
        .row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_wifi_row_bind,
        .action_cb = product_ui_wifi_row_action,
        .user_data = &s_wifi,
    };
    lv_obj_t *content;

    memset(&s_wifi, 0, sizeof(s_wifi));
    s_wifi.seen_scan_version = 0xFFFFFFFFU;
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    s_wifi.page = &s_wifi_page_view;

    if (product_ui_page_frame_create_ex(parent, &spec,
                                        &s_wifi_page_view) != 0) {
        return -1;
    }
    content = s_wifi_page_view.content;
    if (content == NULL) {
        return -1;
    }
    if (product_ui_virtual_list_create(content, &s_wifi.list, &list_spec) != 0) {
        return -1;
    }
    content = s_wifi.list.header;
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, PRODUCT_UI_SPACE_SM, 0);

    s_wifi.status_label = lv_label_create(content);
    if (s_wifi.status_label == NULL) {
        return -1;
    }
    lv_obj_set_width(s_wifi.status_label,
                     PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD));
    lv_label_set_long_mode(s_wifi.status_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(s_wifi.status_label,
                     product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
    lv_label_set_text(s_wifi.status_label, "读取状态...");

    if (product_ui_wifi_build_actions(content) != 0) {
        return -1;
    }

    s_wifi.scan_label = lv_label_create(content);
    if (s_wifi.scan_label == NULL) {
        return -1;
    }
    lv_obj_set_width(s_wifi.scan_label,
                     PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD));
    lv_obj_add_style(s_wifi.scan_label,
                     product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_set_style_text_color(
        s_wifi.scan_label, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(s_wifi.scan_label, "正在扫描...");

    s_wifi.empty_label = lv_label_create(content);
    if (s_wifi.empty_label == NULL) {
        return -1;
    }
    lv_obj_add_style(s_wifi.empty_label, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_set_width(s_wifi.empty_label, LV_PCT(100));
    lv_label_set_long_mode(s_wifi.empty_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_wifi.empty_label, "正在扫描...");

    (void)product_ui_connection_command(PRODUCT_UI_CONNECTION_WIFI_SCAN_START,
                                        NULL, 0U);
    product_ui_wifi_render_status();
    product_ui_wifi_render_scan();
    return 0;
}

#else /* !D_FEATURE_WIFI */

int product_ui_settings_wifi_page_create(lv_obj_t *parent)
{
    lv_obj_t *content;

    content = product_ui_settings_detail_frame_create(parent, "WiFi",
                                                       &s_wifi_page_view);
    if (content == NULL) {
        return -1;
    }
    return product_ui_settings_detail_feedback(
        content, PRODUCT_UI_FEEDBACK_ERROR, "WiFi 不可用",
        "当前固件未启用 WiFi");
}

static void product_ui_wifi_page_update(uint32_t elapsed_ms)
{
    product_ui_settings_detail_page_update(elapsed_ms);
}

static void product_ui_wifi_page_destroy(void)
{
    product_ui_page_view_reset(&s_wifi_page_view);
}

#endif /* D_FEATURE_WIFI */

static int product_ui_settings_connectivity_unavailable(
    lv_obj_t *parent, const char *page_title, const char *feature_title,
    uint32_t capability, product_ui_page_view_t *view)
{
    product_ui_settings_snapshot_t snapshot;
    lv_obj_t *content;

    content = product_ui_settings_detail_frame_create(parent, page_title,
                                                       view);
    if (content == NULL) {
        return -1;
    }
    if (product_ui_settings_detail_snapshot(&snapshot) != 0) {
        return product_ui_settings_detail_feedback(
            content, PRODUCT_UI_FEEDBACK_ERROR, "设置服务不可用",
            "无法读取连接能力状态");
    }
    if ((snapshot.capability_mask & capability) == 0U) {
        return product_ui_settings_detail_feedback(
            content, PRODUCT_UI_FEEDBACK_ERROR, feature_title,
            "当前固件未提供此能力");
    }
    return product_ui_settings_detail_feedback(
        content, PRODUCT_UI_FEEDBACK_ERROR, "详情不可用",
        "设置服务未提供可验证的连接详情");
}

int product_ui_settings_bluetooth_page_create(lv_obj_t *parent)
{
    return product_ui_settings_connectivity_unavailable(
        parent, "蓝牙", "蓝牙不可用",
        PRODUCT_UI_SETTING_CAP_BLUETOOTH, &s_bluetooth_page_view);
}

static void product_ui_settings_bluetooth_destroy(void)
{
    product_ui_page_view_reset(&s_bluetooth_page_view);
}

static const page_ops_t s_settings_wifi_ops = {
    .create = product_ui_settings_wifi_page_create,
    .destroy = product_ui_wifi_page_destroy,
    .update = product_ui_wifi_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_settings_wifi_desc = {
    .name = "WiFi 详情",
    .ops = &s_settings_wifi_ops,
};

static int settings_wifi_register_page(int page_type)
{
    s_settings_wifi_desc.page_type = page_type;
    return page_manager_register(&s_settings_wifi_desc);
}

static const page_ops_t s_settings_bluetooth_ops = {
    .create = product_ui_settings_bluetooth_page_create,
    .destroy = product_ui_settings_bluetooth_destroy,
    .update = product_ui_settings_detail_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_settings_bluetooth_desc = {
    .name = "蓝牙详情",
    .ops = &s_settings_bluetooth_ops,
};

static int settings_bluetooth_register_page(int page_type)
{
    s_settings_bluetooth_desc.page_type = page_type;
    return page_manager_register(&s_settings_bluetooth_desc);
}

D_UI_MODULE_DEFINE(product_settings_wifi, "WiFi 详情", LV_SYMBOL_SETTINGS,
                settings_wifi_register_page, NULL);
D_UI_MODULE_DEFINE(product_settings_bluetooth, "蓝牙详情", LV_SYMBOL_SETTINGS,
                settings_bluetooth_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
