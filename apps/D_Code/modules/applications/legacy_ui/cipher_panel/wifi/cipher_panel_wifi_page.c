/**
 * @file cipher_panel_wifi_page.c
 * @brief WiFi sub page for the cipher panel demo.
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL)

#include "modules/applications/legacy_ui/cipher_panel/pages/cipher_panel_pages.h"
#include "modules/applications/legacy_ui/cipher_panel/wifi/cipher_panel_wifi_input.h"
#include "framework/d_ui_font.h"
#if D_FEATURE_WIFI
#include "modules/services/wifi/wifi_service.h"
#endif
#include <string.h>

#define CIPHER_WIFI_LIST_W      276
#define CIPHER_WIFI_ROW_H       64
#define CIPHER_WIFI_STATUS_Y    88
#define CIPHER_WIFI_STATUS_H    98
#define CIPHER_WIFI_SCAN_Y      192
#define CIPHER_WIFI_LIST_Y      212
#define CIPHER_WIFI_LIST_H      214
#define CIPHER_WIFI_STATUS_REFRESH_MS 2000U

#if D_FEATURE_WIFI
static lv_obj_t *s_wifi_page = NULL;
static lv_obj_t *s_wifi_status_card = NULL;
static lv_obj_t *s_wifi_scan_label = NULL;
static lv_obj_t *s_wifi_list = NULL;
static wifi_service_scan_result_t s_wifi_results[WIFI_SERVICE_SCAN_MAX_RESULTS];
static uint8_t s_wifi_result_count = 0;
static uint8_t s_wifi_row_index[WIFI_SERVICE_SCAN_MAX_RESULTS];
static uint32_t s_wifi_seen_scan_version = 0xFFFFFFFFU;
static uint32_t s_wifi_seen_status_version = 0xFFFFFFFFU;
static uint32_t s_wifi_status_refresh_ms = 0;

static void cipher_wifi_render_scan(void);
static void cipher_wifi_render_status(void);

static const char *cipher_wifi_scan_text(wifi_service_scan_state_t state)
{
    switch (state) {
    case WIFI_SERVICE_SCAN_SCANNING:
        return "Scanning...";
    case WIFI_SERVICE_SCAN_BUSY:
        return "WiFi busy";
    case WIFI_SERVICE_SCAN_NOT_READY:
        return "WiFi not ready";
    case WIFI_SERVICE_SCAN_TIMEOUT:
        return "Scan timeout";
    case WIFI_SERVICE_SCAN_ERROR:
        return "Scan error";
    default:
        return "Tap refresh";
    }
}

static const char *cipher_wifi_error_text(wifi_service_conn_error_t error)
{
    switch (error) {
    case WIFI_SERVICE_ERR_NOT_READY:
        return "WiFi not ready";
    case WIFI_SERVICE_ERR_BUSY:
        return "WiFi busy";
    case WIFI_SERVICE_ERR_NO_SAVED:
        return "No saved network";
    case WIFI_SERVICE_ERR_NOT_FOUND:
        return "SSID not found";
    case WIFI_SERVICE_ERR_ASSOC_FAIL:
        return "Associate failed";
    case WIFI_SERVICE_ERR_ASSOC_TIMEOUT:
        return "Associate timeout";
    case WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT:
        return "Switch timeout (old link)";
    case WIFI_SERVICE_ERR_DHCP_TIMEOUT:
        return "DHCP timeout";
    case WIFI_SERVICE_ERR_DISCONNECTED:
        return "Disconnected";
    default:
        return "No error";
    }
}

static const char *cipher_wifi_connect_error_text(d_err_t err)
{
    switch (err) {
    case D_ERR_BUSY:
        return "WiFi busy";
    case D_ERR_NOT_READY:
        return "WiFi not ready";
    case D_ERR_NOT_FOUND:
        return "No saved network";
    case D_ERR_INVALID_ARG:
        return "Invalid network";
    default:
        return "Connect failed";
    }
}

static void cipher_wifi_set_input_message(const char *text, uint32_t color)
{
    if (cipher_wifi_input_is_open() != 0U) {
        cipher_wifi_input_set_message(text, color);
        return;
    }
    if (s_wifi_scan_label != NULL) {
        lv_label_set_text(s_wifi_scan_label, text);
    }
}

static uint8_t cipher_wifi_start_connect(const char *ssid, const char *pwd)
{
    d_err_t err = wifi_service_connect_ui(ssid, pwd);

    if (err == D_OK) {
        cipher_wifi_render_status();
        return 1U;
    }
    cipher_wifi_set_input_message(cipher_wifi_connect_error_text(err), CIPHER_WARN);
    cipher_wifi_render_status();
    return 0U;
}

static uint8_t cipher_wifi_connect_input_cb(const char *ssid, const char *pwd,
                                            void *user_data)
{
    (void)user_data;
    return cipher_wifi_start_connect(ssid, pwd);
}

static lv_obj_t *cipher_wifi_action_button(lv_obj_t *parent, const char *text,
                                           int x, int y, int w, uint32_t accent,
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
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    cipher_text(btn, text, 0, 7, w, &lv_font_montserrat_12,
                accent, LV_TEXT_ALIGN_CENTER);
    return btn;
}

static void cipher_wifi_reconnect_event(lv_event_t *e)
{
    (void)e;
    (void)wifi_service_reconnect_saved();
    cipher_wifi_render_status();
}

static void cipher_wifi_forget_event(lv_event_t *e)
{
    (void)e;
    cipher_wifi_input_close();
    (void)wifi_service_forget_saved();
    cipher_wifi_render_status();
}

static void cipher_wifi_row_event(lv_event_t *e)
{
    uint8_t *index = (uint8_t *)lv_event_get_user_data(e);
    const wifi_service_scan_result_t *item;

    if ((index == NULL) || (*index >= s_wifi_result_count)) {
        return;
    }
    item = &s_wifi_results[*index];
    if (wifi_service_auth_requires_password(item->auth_mode) == 0U) {
        (void)cipher_wifi_start_connect(item->ssid, "");
        return;
    }
    cipher_wifi_input_open(s_wifi_page, item, cipher_wifi_connect_input_cb, NULL);
}

static void cipher_wifi_render_status_text(lv_obj_t *card,
                                           const wifi_service_status_t *status)
{
    if ((card == NULL) || (status == NULL)) {
        return;
    }
    if (status->state == WIFI_SERVICE_CONN_CONNECTED) {
        lv_obj_t *signal_label;

        cipher_text(card, status->ssid, 18, 38, 170, d_ui_font_get_default(),
                    CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
        signal_label = cipher_text(card, "", 18, 64, 170,
                                   &lv_font_montserrat_12, CIPHER_MUTED,
                                   LV_TEXT_ALIGN_LEFT);
        if (signal_label != NULL) {
            lv_label_set_text_fmt(signal_label, "%d dBm  %d%%",
                                  (int)status->rssi,
                                  (int)status->signal_strength);
        }
    } else if ((status->state == WIFI_SERVICE_CONN_CONNECTING) ||
               (status->state == WIFI_SERVICE_CONN_ASSOCIATED)) {
        cipher_text(card, status->ssid[0] ? status->ssid : "WiFi",
                    18, 42, 170, d_ui_font_get_default(), CIPHER_TEXT,
                    LV_TEXT_ALIGN_LEFT);
        cipher_text(card,
                    status->state == WIFI_SERVICE_CONN_ASSOCIATED ? "IP setup..." : "Connecting...",
                    18, 66, 170, &lv_font_montserrat_12, CIPHER_ACCENT,
                    LV_TEXT_ALIGN_LEFT);
    } else if (status->state == WIFI_SERVICE_CONN_ERROR) {
        cipher_text(card, cipher_wifi_error_text(status->error),
                    18, 42, 170, &lv_font_montserrat_14, CIPHER_WARN,
                    LV_TEXT_ALIGN_LEFT);
    } else {
        cipher_text(card, "WiFi not connected", 18, 44, 170,
                    &lv_font_montserrat_14, CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    }
}

static void cipher_wifi_render_status(void)
{
    wifi_service_status_t status;
    uint32_t version;

    if (s_wifi_status_card == NULL) {
        return;
    }
    version = wifi_service_get_status_version();
    if (version == s_wifi_seen_status_version) {
        return;
    }
    s_wifi_seen_status_version = version;
    wifi_service_get_status(&status);
    lv_obj_clean(s_wifi_status_card);

    cipher_rect(s_wifi_status_card, 12, 0, 90, 2, CIPHER_ACCENT_2, 0);
    cipher_text(s_wifi_status_card, "CURRENT NETWORK", 18, 14, 164,
                &lv_font_montserrat_12, CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    cipher_wifi_render_status_text(s_wifi_status_card, &status);

    cipher_wifi_action_button(s_wifi_status_card, "Reconnect", 178, 22, 80,
                              CIPHER_ACCENT, cipher_wifi_reconnect_event);
    cipher_wifi_action_button(s_wifi_status_card, "Forget", 178, 58, 80,
                              CIPHER_WARN, cipher_wifi_forget_event);
}

static void cipher_wifi_add_row(lv_obj_t *parent, int y, uint8_t index)
{
    lv_obj_t *row;
    lv_obj_t *label;
    const wifi_service_scan_result_t *item;

    if ((parent == NULL) || (index >= s_wifi_result_count)) {
        return;
    }
    item = &s_wifi_results[index];

    row = cipher_row(parent, 8, y, CIPHER_WIFI_LIST_W - 16, CIPHER_WIFI_ROW_H);
    if (row == NULL) {
        return;
    }

    s_wifi_row_index[index] = index;
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cipher_wifi_row_event, LV_EVENT_CLICKED,
                        &s_wifi_row_index[index]);
    cipher_rect(row, 0, 0, 3, CIPHER_WIFI_ROW_H, CIPHER_ACCENT_2, 0);
    label = cipher_text(row, "", 14, 8, 224, d_ui_font_get_default(),
                        CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    if (label != NULL) {
        lv_label_set_text_fmt(label, "%s\n%d dBm  %d%%  %s",
                              item->ssid, (int)item->rssi,
                              (int)item->signal_strength,
                              wifi_service_auth_name(item->auth_mode));
        lv_obj_set_style_text_line_space(label, 6, 0);
    }
    cipher_text(row, ">", 236, 22, 18, &lv_font_montserrat_14,
                CIPHER_ACCENT, LV_TEXT_ALIGN_RIGHT);
}

static void cipher_wifi_render_scan(void)
{
    wifi_service_scan_state_t state;
    uint32_t version;
    uint8_t i;

    if (s_wifi_list == NULL) {
        return;
    }

    state = wifi_service_scan_get_state();
    version = wifi_service_scan_get_version();
    if (version == s_wifi_seen_scan_version) {
        return;
    }

    s_wifi_result_count = wifi_service_scan_copy_results(s_wifi_results,
                                                         WIFI_SERVICE_SCAN_MAX_RESULTS);
    s_wifi_seen_scan_version = version;
    if (s_wifi_scan_label != NULL) {
        if (state == WIFI_SERVICE_SCAN_DONE) {
            lv_label_set_text_fmt(s_wifi_scan_label, "Found %d networks",
                                  (int)s_wifi_result_count);
        } else {
            lv_label_set_text(s_wifi_scan_label, cipher_wifi_scan_text(state));
        }
    }
    lv_obj_clean(s_wifi_list);

    if ((state != WIFI_SERVICE_SCAN_DONE) || (s_wifi_result_count == 0U)) {
        cipher_text(s_wifi_list, cipher_wifi_scan_text(state), 0, 88, CIPHER_WIFI_LIST_W,
                    &lv_font_montserrat_14, CIPHER_MUTED, LV_TEXT_ALIGN_CENTER);
        return;
    }

    for (i = 0; i < s_wifi_result_count; i++) {
        int y = 8 + (int)i * 74;
        cipher_wifi_add_row(s_wifi_list, y, i);
    }
}
#endif

void cipher_panel_show_wifi(lv_obj_t *parent)
{
#if D_FEATURE_WIFI
    s_wifi_page = parent;
    s_wifi_status_card = NULL;
    s_wifi_scan_label = NULL;
    s_wifi_list = NULL;
    cipher_wifi_input_reset();
    s_wifi_result_count = 0;
    s_wifi_seen_scan_version = 0xFFFFFFFFU;
    s_wifi_seen_status_version = 0xFFFFFFFFU;
    s_wifi_status_refresh_ms = 0;
#endif

    cipher_page_header(parent, "WiFi");

#if D_FEATURE_WIFI
    cipher_button(parent, "", LV_SYMBOL_REFRESH, 270, 44, 38, 34,
                  CIPHER_PAGE_WIFI, CIPHER_ACCENT_2);
    s_wifi_status_card = cipher_row(parent, 22, CIPHER_WIFI_STATUS_Y,
                                    CIPHER_WIFI_LIST_W, CIPHER_WIFI_STATUS_H);
    s_wifi_scan_label = cipher_text(parent, "Scanning...", 22, CIPHER_WIFI_SCAN_Y,
                                    276, &lv_font_montserrat_12, CIPHER_MUTED,
                                    LV_TEXT_ALIGN_LEFT);
    s_wifi_list = cipher_row(parent, 22, CIPHER_WIFI_LIST_Y,
                             CIPHER_WIFI_LIST_W, CIPHER_WIFI_LIST_H);
    if (s_wifi_list != NULL) {
        lv_obj_add_flag(s_wifi_list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_pad_top(s_wifi_list, 8, 0);
        lv_obj_set_style_pad_bottom(s_wifi_list, 8, 0);
        lv_obj_set_style_clip_corner(s_wifi_list, 1, 0);
    }

    (void)wifi_service_scan_start();
    cipher_wifi_render_status();
    cipher_wifi_render_scan();
#else
    cipher_text(parent, "WiFi disabled", 22, 128, 276,
                &lv_font_montserrat_14, CIPHER_MUTED, LV_TEXT_ALIGN_CENTER);
#endif
}

void cipher_panel_wifi_update(uint32_t elapsed_ms)
{
#if D_FEATURE_WIFI
    s_wifi_status_refresh_ms += elapsed_ms;
    if (s_wifi_status_refresh_ms >= CIPHER_WIFI_STATUS_REFRESH_MS) {
        s_wifi_status_refresh_ms = 0;
        s_wifi_seen_status_version = 0xFFFFFFFFU;
    }

    wifi_service_scan_update(elapsed_ms);
    cipher_wifi_render_status();
    cipher_wifi_render_scan();
#else
    (void)elapsed_ms;
#endif
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_CIPHER_PANEL) */
