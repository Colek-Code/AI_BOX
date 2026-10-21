/**
 * @file cipher_panel_demo.c
 * @brief Cyber control panel main page demo.
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL)

#include "modules/applications/legacy_ui/cipher_panel/cipher_panel_demo.h"
#include "framework/d_page_manager.h"
#include "platform/screen/power/d_screen_power.h"
#include "modules/applications/legacy_ui/cipher_panel/assets/main_page_assets.h"
#include "modules/applications/legacy_ui/cipher_panel/pages/cipher_panel_pages.h"
#include "lvgl.h"
#if defined(TCFG_ADKEY_ENABLE) && TCFG_ADKEY_ENABLE
#include "asm/adc_api.h"
#include "system/app_power_manage.h"
#endif
#if D_FEATURE_WIFI
#include "wifi/wifi_connect.h"
#endif
#include <stdint.h>
#include <time.h>

#define LOG_TAG "[MAIN_PAGE] "

#define CIPHER_TIMEZONE_OFFSET    (8 * 60 * 60)
#define CIPHER_CLOCK_UPDATE_MS    1000U
#define CIPHER_STATUS_UPDATE_MS   2000U
#define CIPHER_BATTERY_FULL_VALUE 420U

typedef struct {
    const char *title;
    const char *icon;
    cipher_page_t page;
    uint32_t accent;
} cipher_tile_t;

static const cipher_tile_t s_tiles[] = {
    {"AIChat", LV_SYMBOL_EDIT,     CIPHER_PAGE_AICHAT, CIPHER_ACCENT},
    {"WiFi",   LV_SYMBOL_WIFI,     CIPHER_PAGE_WIFI,   CIPHER_ACCENT_2},
    {"System", LV_SYMBOL_SETTINGS, CIPHER_PAGE_SYSTEM, CIPHER_PURPLE},
};

static const char *const s_weekdays[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_status_bar = NULL;
static lv_obj_t *s_clock_shadow = NULL;
static lv_obj_t *s_clock_label = NULL;
static lv_obj_t *s_second_label = NULL;
static lv_obj_t *s_date_label = NULL;
static cipher_page_t s_current_cipher_page = CIPHER_PAGE_HOME;
static uint32_t s_status_update_ms = 0;
static uint32_t s_clock_update_ms = 0;
static int s_style_inited = 0;
static lv_style_t s_style_root;
static lv_style_t s_style_status;
static lv_style_t s_style_title_main;
static lv_style_t s_style_title_sub;
static lv_style_t s_style_card;
static lv_style_t s_style_card_pressed;
static lv_style_t s_style_glass_card;
static lv_style_t s_style_glass_pressed;
static lv_style_t s_style_row;
static lv_style_t s_style_small_btn;

static void cipher_show_page(cipher_page_t page);

static void cipher_style_init(void)
{
    if (s_style_inited) {
        return;
    }
    s_style_inited = 1;

    lv_style_init(&s_style_root);
    lv_style_set_bg_color(&s_style_root, lv_color_hex(CIPHER_BG));
    lv_style_set_bg_opa(&s_style_root, LV_OPA_COVER);
    lv_style_set_pad_all(&s_style_root, 0);
    lv_style_set_border_width(&s_style_root, 0);
    lv_style_set_radius(&s_style_root, 0);

    lv_style_init(&s_style_status);
    lv_style_set_bg_color(&s_style_status, lv_color_hex(0x02040A));
    lv_style_set_bg_opa(&s_style_status, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_status, 1);
    lv_style_set_border_side(&s_style_status, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&s_style_status, lv_color_hex(CIPHER_ACCENT));
    lv_style_set_pad_all(&s_style_status, 0);
    lv_style_set_radius(&s_style_status, 0);

    lv_style_init(&s_style_title_main);
    lv_style_set_text_font(&s_style_title_main, &cipher_clock_font_54);
    lv_style_set_text_color(&s_style_title_main, lv_color_hex(CIPHER_ACCENT));
    lv_style_set_text_letter_space(&s_style_title_main, 1);

    lv_style_init(&s_style_title_sub);
    lv_style_set_text_font(&s_style_title_sub, &lv_font_montserrat_14);
    lv_style_set_text_color(&s_style_title_sub, lv_color_hex(CIPHER_MUTED));

    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, lv_color_hex(CIPHER_CARD));
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_card, 1);
    lv_style_set_border_color(&s_style_card, lv_color_hex(0x29D9FF));
    lv_style_set_radius(&s_style_card, 8);
    lv_style_set_pad_all(&s_style_card, 0);
    lv_style_set_shadow_width(&s_style_card, 3);
    lv_style_set_shadow_offset_y(&s_style_card, 2);
    lv_style_set_shadow_color(&s_style_card, lv_color_hex(0x001E32));
    lv_style_set_shadow_opa(&s_style_card, LV_OPA_20);

    lv_style_init(&s_style_card_pressed);
    lv_style_set_bg_color(&s_style_card_pressed, lv_color_hex(CIPHER_CARD_PRESSED));
    lv_style_set_border_color(&s_style_card_pressed, lv_color_hex(CIPHER_ACCENT_2));

    lv_style_init(&s_style_glass_card);
    lv_style_set_bg_color(&s_style_glass_card, lv_color_hex(0xCFFBFF));
    lv_style_set_bg_grad_color(&s_style_glass_card, lv_color_hex(0x08233A));
    lv_style_set_bg_grad_dir(&s_style_glass_card, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&s_style_glass_card, LV_OPA_40);
    lv_style_set_bg_main_opa(&s_style_glass_card, LV_OPA_30);
    lv_style_set_bg_grad_opa(&s_style_glass_card, LV_OPA_50);
    lv_style_set_border_width(&s_style_glass_card, 1);
    lv_style_set_border_color(&s_style_glass_card, lv_color_hex(0xD8FBFF));
    lv_style_set_border_opa(&s_style_glass_card, LV_OPA_80);
    lv_style_set_outline_width(&s_style_glass_card, 1);
    lv_style_set_outline_pad(&s_style_glass_card, 1);
    lv_style_set_outline_color(&s_style_glass_card, lv_color_hex(0x0B4B68));
    lv_style_set_outline_opa(&s_style_glass_card, LV_OPA_30);
    lv_style_set_radius(&s_style_glass_card, 8);
    lv_style_set_pad_all(&s_style_glass_card, 0);
    lv_style_set_shadow_width(&s_style_glass_card, 4);
    lv_style_set_shadow_offset_y(&s_style_glass_card, 2);
    lv_style_set_shadow_color(&s_style_glass_card, lv_color_hex(0x001524));
    lv_style_set_shadow_opa(&s_style_glass_card, LV_OPA_20);

    lv_style_init(&s_style_glass_pressed);
    lv_style_set_bg_color(&s_style_glass_pressed, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&s_style_glass_pressed, LV_OPA_20);
    lv_style_set_border_opa(&s_style_glass_pressed, LV_OPA_COVER);

    lv_style_init(&s_style_row);
    lv_style_set_bg_color(&s_style_row, lv_color_hex(CIPHER_ROW));
    lv_style_set_bg_opa(&s_style_row, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_row, 1);
    lv_style_set_border_color(&s_style_row, lv_color_hex(CIPHER_LINE));
    lv_style_set_radius(&s_style_row, 7);
    lv_style_set_pad_all(&s_style_row, 0);

    lv_style_init(&s_style_small_btn);
    lv_style_set_bg_color(&s_style_small_btn, lv_color_hex(0x041B2A));
    lv_style_set_bg_opa(&s_style_small_btn, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_small_btn, 1);
    lv_style_set_border_color(&s_style_small_btn, lv_color_hex(CIPHER_ACCENT));
    lv_style_set_radius(&s_style_small_btn, 6);
    lv_style_set_pad_all(&s_style_small_btn, 0);
}

static void cipher_reset_obj(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *cipher_obj(lv_obj_t *parent, int x, int y, int w, int h, const lv_style_t *style)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (obj == NULL) {
        return NULL;
    }
    cipher_reset_obj(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    if (style != NULL) {
        lv_obj_add_style(obj, style, 0);
    }
    return obj;
}

lv_obj_t *cipher_row(lv_obj_t *parent, int x, int y, int w, int h)
{
    return cipher_obj(parent, x, y, w, h, &s_style_row);
}

lv_obj_t *cipher_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = cipher_obj(parent, x, y, w, h, NULL);
    if (obj == NULL) {
        return NULL;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *cipher_opa_rect(lv_obj_t *parent, int x, int y, int w, int h,
                                 uint32_t color, lv_opa_t opa, int radius)
{
    lv_obj_t *obj = cipher_rect(parent, x, y, w, h, color, radius);
    if (obj == NULL) {
        return NULL;
    }
    lv_obj_set_style_bg_opa(obj, opa, 0);
    return obj;
}

static lv_obj_t *cipher_grad_rect(lv_obj_t *parent, int x, int y, int w, int h,
                                  uint32_t color, uint32_t grad_color,
                                  lv_opa_t opa, int radius, lv_grad_dir_t dir)
{
    lv_obj_t *obj = cipher_opa_rect(parent, x, y, w, h, color, opa, radius);
    if (obj == NULL) {
        return NULL;
    }
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(grad_color), 0);
    lv_obj_set_style_bg_grad_dir(obj, dir, 0);
    lv_obj_set_style_bg_main_opa(obj, opa, 0);
    lv_obj_set_style_bg_grad_opa(obj, opa, 0);
    return obj;
}

static lv_obj_t *cipher_border_rect(lv_obj_t *parent, int x, int y, int w, int h,
                                    uint32_t color, lv_opa_t opa, int radius)
{
    lv_obj_t *obj = cipher_obj(parent, x, y, w, h, NULL);
    if (obj == NULL) {
        return NULL;
    }
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(obj, opa, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void cipher_glass_backdrop(lv_obj_t *parent, int w, int h, uint32_t accent)
{
    cipher_grad_rect(parent, 6, 7, w - 12, h - 14, 0x041321, 0x0C2E48,
                     LV_OPA_30, 7, LV_GRAD_DIR_VER);
    cipher_grad_rect(parent, 14, 9, w - 28, 10, 0xCFFBFF, accent,
                     LV_OPA_10, 5, LV_GRAD_DIR_HOR);
    cipher_opa_rect(parent, 18, h - 9, w - 36, 4, accent, LV_OPA_10, 2);
}

static void cipher_lens_stack(lv_obj_t *parent, int x, int y, uint32_t accent)
{
    cipher_opa_rect(parent, x + 4, y + 5, 36, 36, CIPHER_PURPLE, LV_OPA_20, 18);
    cipher_grad_rect(parent, x, y, 40, 40, 0x17334A, 0x030711,
                     LV_OPA_50, 20, LV_GRAD_DIR_VER);
    cipher_border_rect(parent, x + 1, y + 1, 38, 38, 0xCFFBFF, LV_OPA_20, 19);
    cipher_opa_rect(parent, x + 5, y + 5, 30, 30, accent, LV_OPA_20, 15);
    cipher_grad_rect(parent, x + 8, y + 8, 24, 24, 0x42111D, 0x030405,
                     LV_OPA_50, 12, LV_GRAD_DIR_VER);
    cipher_border_rect(parent, x + 8, y + 8, 24, 24, 0xB45BFF, LV_OPA_20, 12);
    cipher_opa_rect(parent, x + 12, y + 12, 16, 16, 0x270509, LV_OPA_60, 8);
    cipher_opa_rect(parent, x + 15, y + 15, 10, 10, 0x030203, LV_OPA_70, 5);
    cipher_opa_rect(parent, x + 9, y + 9, 20, 3, CIPHER_PURPLE, LV_OPA_20, 2);
    cipher_opa_rect(parent, x + 12, y + 27, 18, 2, accent, LV_OPA_20, 1);
    cipher_opa_rect(parent, x + 11, y + 8, 8, 2, 0xE8FDFF, LV_OPA_30, 1);
}

lv_obj_t *cipher_text(lv_obj_t *parent, const char *text, int x, int y, int w,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    cipher_reset_obj(label);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

lv_obj_t *cipher_chip(lv_obj_t *parent, const char *text, int x, int y, int w, uint32_t accent)
{
    lv_obj_t *chip = cipher_obj(parent, x, y, w, 20, &s_style_small_btn);
    if (chip == NULL) {
        return NULL;
    }
    lv_obj_set_style_border_color(chip, lv_color_hex(accent), 0);
    cipher_text(chip, text, 0, 4, w, &lv_font_montserrat_12, accent, LV_TEXT_ALIGN_CENTER);
    return chip;
}

static int cipher_wifi_signal_level(void)
{
#if D_FEATURE_WIFI
    int rssi;
    enum wifi_sta_connect_state state;

    if (!wifi_is_on()) {
        return 0;
    }

    state = wifi_get_sta_connect_state();
    if (state != WIFI_STA_NETWORK_STACK_DHCP_SUCC) {
        return 0;
    }

    rssi = (int)wifi_get_rssi();
    if (rssi > 0) {
        rssi -= 256;
    }
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -67) {
        return 3;
    }
    if (rssi >= -75) {
        return 2;
    }
    if (rssi >= -85) {
        return 1;
    }
#endif
    return 0;
}

static int cipher_battery_percent(void)
{
#if defined(TCFG_ADKEY_ENABLE) && TCFG_ADKEY_ENABLE
    u32 vbat;
    u32 pct;

    /* 只读取 VBAT 百分比，不引入 common/system/power_manage.c 的电源事件逻辑。 */
    vbat = adc_get_voltage(AD_CH_VBAT) * 4U / 10U;
    if (vbat <= LOW_POWER_OFF_VAL) {
        return 0;
    }

    if (vbat >= CIPHER_BATTERY_FULL_VALUE) {
        return 100;
    }

    pct = ((vbat - LOW_POWER_OFF_VAL) * 100U) /
          (CIPHER_BATTERY_FULL_VALUE - LOW_POWER_OFF_VAL);
    if (pct > 100) {
        return 100;
    }
    return (int)pct;
#else
    return 0;
#endif
}

static void cipher_signal_icon(lv_obj_t *parent, int x, int y, int level)
{
    const int bar_w = 3;
    const int gap = 3;
    const int h[] = {4, 7, 10, 13};

    if (level < 0) {
        level = 0;
    } else if (level > 4) {
        level = 4;
    }

    for (int i = 0; i < 4; i++) {
        uint32_t color = (i < level) ? CIPHER_ACCENT_2 : CIPHER_LINE;
        cipher_rect(parent, x + i * (bar_w + gap), y + 13 - h[i], bar_w, h[i], color, 1);
    }
}

static void cipher_battery_icon(lv_obj_t *parent, int x, int y, int pct)
{
    lv_obj_t *body = cipher_obj(parent, x, y, 26, 14, NULL);
    uint32_t fill_color = CIPHER_ACCENT_2;
    int fill_w;

    if (body != NULL) {
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(body, 1, 0);
        lv_obj_set_style_border_color(body, lv_color_hex(CIPHER_ACCENT_2), 0);
        lv_obj_set_style_radius(body, 2, 0);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
    }

    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }
    if (pct <= 20) {
        fill_color = CIPHER_WARN;
    }
    fill_w = (22 * pct) / 100;
    if (fill_w > 0) {
        cipher_rect(parent, x + 2, y + 2, fill_w, 10, fill_color, 1);
    }
    cipher_rect(parent, x + 27, y + 4, 2, 6, fill_color, 1);
}

static void cipher_status_bar_update(void)
{
    int wifi_level;
    int battery_pct;

    if (s_status_bar == NULL) {
        return;
    }

    wifi_level = cipher_wifi_signal_level();
    battery_pct = cipher_battery_percent();
    lv_obj_clean(s_status_bar);
    cipher_signal_icon(s_status_bar, 248, 10, wifi_level);
    cipher_battery_icon(s_status_bar, 282, 10, battery_pct);
}

static void cipher_background(lv_obj_t *parent)
{
    cipher_rect(parent, 0, CIPHER_TOP_H, MY_DISP_HOR_RES, 112, 0x04101B, 0);
    cipher_rect(parent, 12, 56, MY_DISP_HOR_RES - 24, 112, CIPHER_BG_PANEL, 8);
    cipher_rect(parent, 12, 178, MY_DISP_HOR_RES - 24, 264, 0x030B15, 8);
    cipher_rect(parent, 0, CIPHER_TOP_H, MY_DISP_HOR_RES, 1, CIPHER_ACCENT, 0);
}

static void cipher_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = cipher_obj(parent, 0, 0, MY_DISP_HOR_RES, CIPHER_TOP_H, &s_style_status);
    if (bar == NULL) {
        return;
    }
    s_status_bar = bar;
    cipher_status_bar_update();
}

static void cipher_clock_reset(void)
{
    s_clock_shadow = NULL;
    s_clock_label = NULL;
    s_second_label = NULL;
    s_date_label = NULL;
    s_clock_update_ms = 0;
}

static const char *cipher_page_name(cipher_page_t page)
{
    switch (page) {
    case CIPHER_PAGE_HOME:
        return "Home";
    case CIPHER_PAGE_LCD:
        return "LCD";
    case CIPHER_PAGE_WIFI:
        return "WiFi";
    case CIPHER_PAGE_SYSTEM:
        return "System";
    case CIPHER_PAGE_VOLUME:
        return "Volume";
    case CIPHER_PAGE_AICHAT:
        return "AIChat";
    default:
        return "Unknown";
    }
}

static int cipher_get_local_time(struct tm *timeinfo)
{
    time_t timestamp;

    if (timeinfo == NULL) {
        return -1;
    }

    timestamp = time(NULL);
    if (timestamp < 0) {
        return -1;
    }
    timestamp += CIPHER_TIMEZONE_OFFSET;

    if (localtime_r(&timestamp, timeinfo) == NULL) {
        return -1;
    }
    return 0;
}

static void cipher_clock_update(void)
{
    struct tm timeinfo;
    const char *weekday = "---";

    if ((s_clock_label == NULL) || (s_date_label == NULL)) {
        return;
    }

    if (cipher_get_local_time(&timeinfo) != 0) {
        lv_label_set_text(s_clock_label, "--:--");
        if (s_clock_shadow != NULL) {
            lv_label_set_text(s_clock_shadow, "--:--");
        }
        if (s_second_label != NULL) {
            lv_label_set_text(s_second_label, "--");
        }
        lv_label_set_text(s_date_label, "--- --.--");
        return;
    }

    if ((timeinfo.tm_wday >= 0) && (timeinfo.tm_wday < 7)) {
        weekday = s_weekdays[timeinfo.tm_wday];
    }

    lv_label_set_text_fmt(s_clock_label, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    if (s_clock_shadow != NULL) {
        lv_label_set_text_fmt(s_clock_shadow, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    }
    if (s_second_label != NULL) {
        lv_label_set_text_fmt(s_second_label, "%02d", timeinfo.tm_sec);
    }
    lv_label_set_text_fmt(s_date_label, "%s %02d.%02d",
                          weekday, timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

static void cipher_title_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = cipher_obj(parent, 24, 54, 272, 112, &s_style_row);
    if (panel == NULL) {
        return;
    }

    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x04111F), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x123A55), 0);
    lv_obj_set_style_shadow_width(panel, 3, 0);
    lv_obj_set_style_shadow_offset_y(panel, 2, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x001827), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);

    cipher_rect(parent, 40, 54, 86, 2, CIPHER_ACCENT, 0);
    cipher_rect(parent, 188, 54, 48, 2, CIPHER_PURPLE, 0);
    cipher_rect(parent, 36, 64, 2, 26, CIPHER_ACCENT, 0);
    cipher_rect(parent, 282, 130, 2, 26, CIPHER_PURPLE, 0);
}

static void cipher_title_lockup(lv_obj_t *parent)
{
    lv_obj_t *shadow;
    lv_obj_t *clock;
    lv_obj_t *second;
    lv_obj_t *date;

    cipher_title_panel(parent);

    shadow = lv_label_create(parent);
    clock = lv_label_create(parent);
    second = lv_label_create(parent);
    date = lv_label_create(parent);

    if (shadow != NULL) {
        cipher_reset_obj(shadow);
        lv_obj_set_pos(shadow, 1, 72);
        lv_obj_set_width(shadow, MY_DISP_HOR_RES);
        lv_obj_add_style(shadow, &s_style_title_main, 0);
        lv_obj_set_style_text_color(shadow, lv_color_hex(0x00435C), 0);
        lv_obj_set_style_text_opa(shadow, LV_OPA_60, 0);
        lv_obj_set_style_text_align(shadow, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (clock != NULL) {
        cipher_reset_obj(clock);
        lv_obj_set_pos(clock, 0, 70);
        lv_obj_set_width(clock, MY_DISP_HOR_RES);
        lv_obj_add_style(clock, &s_style_title_main, 0);
        lv_obj_set_style_text_align(clock, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (second != NULL) {
        cipher_reset_obj(second);
        lv_obj_set_pos(second, 262, 90);
        lv_obj_set_width(second, 30);
        lv_obj_set_style_text_font(second, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(second, lv_color_hex(CIPHER_ACCENT_2), 0);
        lv_obj_set_style_text_align(second, LV_TEXT_ALIGN_LEFT, 0);
    }
    if (date != NULL) {
        cipher_reset_obj(date);
        lv_obj_set_pos(date, 0, 114);
        lv_obj_set_width(date, MY_DISP_HOR_RES);
        lv_obj_add_style(date, &s_style_title_sub, 0);
        lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0);
    }

    s_clock_shadow = shadow;
    s_clock_label = clock;
    s_second_label = second;
    s_date_label = date;
    cipher_clock_update();

    cipher_rect(parent, 104, 140, 72, 2, CIPHER_ACCENT, 0);
    cipher_rect(parent, 180, 140, 36, 2, CIPHER_PURPLE, 0);
    cipher_text(parent, "CIPHER CONTROL", 0, 148, MY_DISP_HOR_RES,
                &lv_font_montserrat_12, CIPHER_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void cipher_back_event(lv_event_t *e)
{
    (void)e;
    printf(LOG_TAG "nav back page=%s(%d)\n",
           cipher_page_name(CIPHER_PAGE_HOME), (int)CIPHER_PAGE_HOME);
    cipher_show_page(CIPHER_PAGE_HOME);
}

static void cipher_add_back(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    if (btn == NULL) {
        return;
    }
    cipher_reset_obj(btn);
    lv_obj_set_pos(btn, 12, 44);
    lv_obj_set_size(btn, 38, 34);
    lv_obj_add_style(btn, &s_style_small_btn, 0);
    lv_obj_add_style(btn, &s_style_card_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cipher_back_event, LV_EVENT_CLICKED, NULL);
    cipher_text(btn, LV_SYMBOL_LEFT, 0, 8, 38, &lv_font_montserrat_18, CIPHER_ACCENT, LV_TEXT_ALIGN_CENTER);
}

static void cipher_nav_event(lv_event_t *e)
{
    cipher_page_t page = (cipher_page_t)(intptr_t)lv_event_get_user_data(e);

    printf(LOG_TAG "nav click page=%s(%d)\n",
           cipher_page_name(page), (int)page);
    cipher_show_page(page);
}

lv_obj_t *cipher_button(lv_obj_t *parent, const char *text, const char *icon,
                        int x, int y, int w, int h, cipher_page_t page, uint32_t accent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    if (btn == NULL) {
        return NULL;
    }
    cipher_reset_obj(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, &s_style_card, 0);
    lv_obj_add_style(btn, &s_style_card_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(accent), 0);
    lv_obj_add_event_cb(btn, cipher_nav_event, LV_EVENT_CLICKED, (void *)(intptr_t)page);

    cipher_rect(btn, 10, 0, w - 20, 2, accent, 0);
    if ((text == NULL) || (text[0] == '\0')) {
        cipher_text(btn, icon, 0, 8, w, &lv_font_montserrat_18, accent, LV_TEXT_ALIGN_CENTER);
        return btn;
    }
    if (h <= 60) {
        cipher_text(btn, icon, 16, 17, 28, &lv_font_montserrat_22, accent, LV_TEXT_ALIGN_CENTER);
        cipher_text(btn, text, 52, 18, w - 64, &lv_font_montserrat_16, CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    } else {
        cipher_text(btn, icon, 0, 14, w, &lv_font_montserrat_24, accent, LV_TEXT_ALIGN_CENTER);
        cipher_text(btn, text, 0, 46, w, &lv_font_montserrat_16, CIPHER_TEXT, LV_TEXT_ALIGN_CENTER);
    }
    return btn;
}

static lv_obj_t *cipher_home_glass_button(lv_obj_t *parent, const cipher_tile_t *tile,
                                          int x, int y, int w, int h)
{
    lv_obj_t *btn;

    if (tile == NULL) {
        return NULL;
    }

    btn = lv_btn_create(parent);
    if (btn == NULL) {
        return NULL;
    }
    cipher_reset_obj(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, &s_style_glass_card, 0);
    lv_obj_add_style(btn, &s_style_glass_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(tile->accent), 0);
    lv_obj_add_event_cb(btn, cipher_nav_event, LV_EVENT_CLICKED, (void *)(intptr_t)tile->page);

    cipher_glass_backdrop(btn, w, h, tile->accent);

    cipher_border_rect(btn, 4, 4, w - 8, h - 8, 0xCFFBFF, LV_OPA_20, 7);
    cipher_opa_rect(btn, 8, 6, w - 16, 12, 0xCFFBFF, LV_OPA_10, 6);
    cipher_opa_rect(btn, 14, 7, w - 28, 1, 0xE8FDFF, LV_OPA_30, 1);
    cipher_opa_rect(btn, 20, 12, 40, 1, 0xCFFBFF, LV_OPA_10, 1);
    cipher_opa_rect(btn, 7, 20, 1, 32, 0xCFFBFF, LV_OPA_10, 1);
    cipher_opa_rect(btn, w - 10, 16, 2, 44, tile->accent, LV_OPA_30, 1);
    cipher_opa_rect(btn, 9, h - 11, w - 18, 7, 0x00111F, LV_OPA_20, 4);
    cipher_opa_rect(btn, 14, h - 4, w - 28, 1, tile->accent, LV_OPA_50, 1);
    cipher_lens_stack(btn, 15, 18, tile->accent);
    cipher_opa_rect(btn, 62, 18, 1, 39, tile->accent, LV_OPA_20, 1);

    cipher_text(btn, tile->icon, 68, 16, 34, &lv_font_montserrat_18, 0xE8FDFF, LV_TEXT_ALIGN_CENTER);
    cipher_text(btn, tile->title, 61, 43, 56, &lv_font_montserrat_14, CIPHER_TEXT, LV_TEXT_ALIGN_CENTER);
    return btn;
}

static void cipher_home(lv_obj_t *parent)
{
    cipher_background(parent);
    cipher_status_bar(parent);
    cipher_title_lockup(parent);

    for (int i = 0; i < (int)(sizeof(s_tiles) / sizeof(s_tiles[0])); i++) {
        int x = 24 + (i % 2) * 146;
        int y = 188 + (i / 2) * 102;
        cipher_home_glass_button(parent, &s_tiles[i], x, y, 126, 76);
    }
}

void cipher_page_header(lv_obj_t *parent, const char *title)
{
    cipher_background(parent);
    cipher_status_bar(parent);
    cipher_add_back(parent);
    cipher_text(parent, title, 64, 52, MY_DISP_HOR_RES - 82, &lv_font_montserrat_18,
                CIPHER_ACCENT, LV_TEXT_ALIGN_LEFT);
    cipher_rect(parent, 64, 78, 78, 2, CIPHER_ACCENT, 0);
    cipher_rect(parent, 146, 78, 34, 2, CIPHER_PURPLE, 0);
}

static void cipher_show_page(cipher_page_t page)
{
    cipher_page_t next_page = page;

    if (s_root == NULL) {
        return;
    }

    if ((next_page < CIPHER_PAGE_HOME) || (next_page > CIPHER_PAGE_AICHAT)) {
        next_page = CIPHER_PAGE_HOME;
    }

    printf(LOG_TAG "show page=%s(%d)\n",
           cipher_page_name(next_page), (int)next_page);
    s_current_cipher_page = next_page;
    s_status_bar = NULL;
    cipher_clock_reset();
    lv_obj_clean(s_root);
    switch (next_page) {
    case CIPHER_PAGE_LCD:
        cipher_panel_show_lcd(s_root);
        break;
    case CIPHER_PAGE_WIFI:
        cipher_panel_show_wifi(s_root);
        break;
    case CIPHER_PAGE_SYSTEM:
        cipher_panel_show_system(s_root);
        break;
    case CIPHER_PAGE_VOLUME:
        cipher_panel_show_volume(s_root);
        break;
    case CIPHER_PAGE_AICHAT:
        cipher_panel_show_aichat(s_root);
        break;
    default:
        cipher_home(s_root);
        break;
    }
}

static int cipher_panel_demo_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return -1;
    }

    cipher_style_init();
    d_screen_power_init();
    cipher_panel_pages_init();
    s_root = parent;
    cipher_reset_obj(parent);
    lv_obj_set_size(parent, MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_obj_add_style(parent, &s_style_root, 0);
    cipher_show_page(CIPHER_PAGE_HOME);
    printf(LOG_TAG "Cipher panel demo created\n");
    return 0;
}

static void cipher_panel_demo_enter(void)
{
    printf(LOG_TAG "Cipher panel demo enter\n");
}

static void cipher_panel_demo_update(uint32_t elapsed_ms)
{
    d_screen_power_update(elapsed_ms);

    s_status_update_ms += elapsed_ms;
    s_clock_update_ms += elapsed_ms;

    if (s_clock_update_ms >= CIPHER_CLOCK_UPDATE_MS) {
        s_clock_update_ms = 0;
        cipher_clock_update();
    }

    if (s_status_update_ms >= CIPHER_STATUS_UPDATE_MS) {
        s_status_update_ms = 0;
        cipher_status_bar_update();
    }

    cipher_panel_pages_update(s_current_cipher_page, elapsed_ms);
}

static const page_ops_t s_cipher_panel_demo_ops = {
    .create  = cipher_panel_demo_create,
    .enter   = cipher_panel_demo_enter,
    .exit    = NULL,
    .destroy = NULL,
    .update  = cipher_panel_demo_update,
    .gesture = NULL,
};

static const page_desc_t s_cipher_panel_demo_desc = {
    .page_type  = PAGE_TYPE_MAIN,
    .name       = "CipherPanelDemo",
    .ops        = &s_cipher_panel_demo_ops,
    .container  = NULL,
    .is_created = 0,
    .is_active  = 0,
};

int cipher_panel_demo_register(void)
{
    return page_manager_register(&s_cipher_panel_demo_desc);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_CIPHER_PANEL) */
