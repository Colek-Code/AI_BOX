/**
 * @file cipher_panel_pages.c
 * @brief Sub pages for the cipher panel demo.
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL)

#include "modules/applications/legacy_ui/cipher_panel/pages/cipher_panel_pages.h"
#include "modules/applications/legacy_ui/cipher_panel/aichat/cipher_panel_aichat.h"
#include "platform/screen/power/d_screen_power.h"
#include "platform/screen/d_display.h"
#include "modules/services/audio/d_device_volume.h"

#define CIPHER_LOCK_TIMEOUT_OPTIONS  "30s\n1min\n5min\n15min\nNever"

static void cipher_lcd_brightness_event(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
    int32_t value;

    if ((slider == NULL) || (value_label == NULL)) {
        return;
    }

    value = lv_slider_get_value(slider);
    if (d_display_set_brightness((uint8_t)value) == D_OK) {
        lv_label_set_text_fmt(value_label, "%d%%", (int)value);
    } else {
        lv_label_set_text(value_label, "ERR");
    }
}

static void cipher_volume_event(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
    int32_t value;

    if ((slider == NULL) || (value_label == NULL)) {
        return;
    }

    value = lv_slider_get_value(slider);
    if (d_device_volume_set((uint8_t)value) == D_OK) {
        lv_label_set_text_fmt(value_label, "%d%%", (int)value);
    } else {
        lv_label_set_text(value_label, "ERR");
    }
}

static void cipher_lcd_timeout_event(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    uint32_t selected;

    if (dropdown == NULL) {
        return;
    }

    selected = lv_dropdown_get_selected(dropdown);
    d_screen_power_set_timeout_index((uint8_t)selected);
}

static void cipher_lcd_touch_wake_event(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);

    if (sw == NULL) {
        return;
    }

    d_screen_power_set_touch_wake_enabled(
        lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1U : 0U);
}

void cipher_panel_pages_init(void)
{
    cipher_panel_aichat_init();
}

static void cipher_style_slider(lv_obj_t *slider)
{
    if (slider == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(slider, lv_color_hex(0x03101B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_height(slider, 14, LV_PART_MAIN);

    lv_obj_set_style_bg_color(slider, lv_color_hex(CIPHER_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(slider, lv_color_hex(0xD6F7FF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(CIPHER_ACCENT_2), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 14);
}

static void cipher_lcd_style_dropdown(lv_obj_t *dropdown)
{
    if (dropdown == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x03101B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(CIPHER_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, 6, LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(CIPHER_TEXT), LV_PART_MAIN);
}

static void cipher_lcd_style_switch(lv_obj_t *sw)
{
    if (sw == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(sw, lv_color_hex(0x03101B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, lv_color_hex(CIPHER_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(CIPHER_ACCENT_2),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xD6F7FF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
}

static lv_obj_t *cipher_volume_value_label(lv_obj_t *card, uint8_t volume)
{
    lv_obj_t *value_label = lv_label_create(card);

    if (value_label == NULL) {
        return NULL;
    }

    lv_obj_set_pos(value_label, 154, 48);
    lv_obj_set_width(value_label, 100);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(CIPHER_ACCENT_2), 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(value_label, "%d%%", (int)volume);
    return value_label;
}

static void cipher_volume_slider(lv_obj_t *card, lv_obj_t *value_label, uint8_t volume)
{
    lv_obj_t *slider;

    if ((card == NULL) || (value_label == NULL)) {
        return;
    }

    slider = lv_slider_create(card);
    if (slider == NULL) {
        return;
    }
    lv_obj_set_pos(slider, 18, 100);
    lv_obj_set_size(slider, 240, 18);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, volume, LV_ANIM_OFF);
    cipher_style_slider(slider);
    lv_obj_add_event_cb(slider, cipher_volume_event,
                        LV_EVENT_VALUE_CHANGED, value_label);
}

void cipher_panel_show_lcd(lv_obj_t *parent)
{
    lv_obj_t *card;
    lv_obj_t *lock_card;
    lv_obj_t *value_label;
    lv_obj_t *slider;
    lv_obj_t *dropdown;
    lv_obj_t *wake_switch;
    uint8_t brightness;

    cipher_page_header(parent, "LCD settings");

    card = cipher_row(parent, 22, 112, 276, 136);
    if (card == NULL) {
        return;
    }

    cipher_rect(card, 12, 0, 90, 2, CIPHER_ACCENT, 0);
    cipher_rect(card, 106, 0, 42, 2, CIPHER_PURPLE, 0);
    cipher_text(card, "BRIGHTNESS", 18, 18, 150, &lv_font_montserrat_14,
                CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    cipher_chip(card, "PWM", 202, 15, 52, CIPHER_ACCENT_2);

    value_label = lv_label_create(card);
    if (value_label == NULL) {
        return;
    }
    lv_obj_set_pos(value_label, 154, 48);
    lv_obj_set_width(value_label, 100);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(CIPHER_ACCENT), 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);

    brightness = d_display_get_brightness();
    if (brightness < 5) {
        brightness = 5;
    } else if (brightness > 100) {
        brightness = 100;
    }
    lv_label_set_text_fmt(value_label, "%d%%", (int)brightness);

    cipher_text(card, "LCD BACKLIGHT", 18, 54, 132, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);

    slider = lv_slider_create(card);
    if (slider == NULL) {
        return;
    }
    lv_obj_set_pos(slider, 18, 92);
    lv_obj_set_size(slider, 240, 18);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
    cipher_style_slider(slider);
    lv_obj_add_event_cb(slider, cipher_lcd_brightness_event,
                        LV_EVENT_VALUE_CHANGED, value_label);

    cipher_text(card, "5", 18, 113, 36, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    cipher_text(card, "100", 218, 113, 40, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_RIGHT);

    lock_card = cipher_row(parent, 22, 270, 276, 154);
    if (lock_card == NULL) {
        return;
    }

    cipher_rect(lock_card, 12, 0, 90, 2, CIPHER_ACCENT_2, 0);
    cipher_rect(lock_card, 106, 0, 42, 2, CIPHER_PURPLE, 0);
    cipher_text(lock_card, "LOCK SCREEN", 18, 18, 150, &lv_font_montserrat_14,
                CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    cipher_chip(lock_card, "RAM", 202, 15, 52, CIPHER_PURPLE);

    cipher_text(lock_card, "TIMEOUT", 18, 52, 90, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    dropdown = lv_dropdown_create(lock_card);
    if (dropdown != NULL) {
        lv_obj_set_pos(dropdown, 132, 43);
        lv_obj_set_size(dropdown, 126, 34);
        lv_dropdown_set_options_static(dropdown, CIPHER_LOCK_TIMEOUT_OPTIONS);
        lv_dropdown_set_dir(dropdown, LV_DIR_TOP);
        lv_dropdown_set_selected(dropdown, d_screen_power_get_timeout_index());
        cipher_lcd_style_dropdown(dropdown);
        lv_obj_add_event_cb(dropdown, cipher_lcd_timeout_event,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }

    cipher_text(lock_card, "TOUCH WAKE", 18, 106, 120, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    wake_switch = lv_switch_create(lock_card);
    if (wake_switch != NULL) {
        lv_obj_set_pos(wake_switch, 204, 96);
        lv_obj_set_size(wake_switch, 52, 28);
        cipher_lcd_style_switch(wake_switch);
        if (d_screen_power_get_touch_wake_enabled() != 0U) {
            lv_obj_add_state(wake_switch, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(wake_switch, cipher_lcd_touch_wake_event,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }
}

void cipher_panel_show_system(lv_obj_t *parent)
{
    cipher_page_header(parent, "System menu");
    cipher_button(parent, "WiFi", LV_SYMBOL_WIFI, 28, 112, 264, 54,
                  CIPHER_PAGE_WIFI, CIPHER_ACCENT_2);
    cipher_button(parent, "LCD settings", LV_SYMBOL_TINT, 28, 178, 264, 54,
                  CIPHER_PAGE_LCD, CIPHER_ACCENT);
    cipher_button(parent, "Volume", LV_SYMBOL_VOLUME_MAX, 28, 244, 264, 54,
                  CIPHER_PAGE_VOLUME, CIPHER_PURPLE);
}

void cipher_panel_show_volume(lv_obj_t *parent)
{
    lv_obj_t *card;
    lv_obj_t *value_label;
    uint8_t volume;

    cipher_page_header(parent, "Volume");

    card = cipher_row(parent, 22, 132, 276, 154);
    if (card == NULL) {
        return;
    }

    cipher_rect(card, 12, 0, 90, 2, CIPHER_ACCENT_2, 0);
    cipher_rect(card, 106, 0, 42, 2, CIPHER_PURPLE, 0);
    cipher_text(card, "DEVICE VOLUME", 18, 18, 150, &lv_font_montserrat_14,
                CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    cipher_chip(card, "QWEN", 202, 15, 52, CIPHER_PURPLE);

    volume = d_device_volume_get();
    value_label = cipher_volume_value_label(card, volume);
    if (value_label == NULL) {
        return;
    }

    cipher_text(card, "PLAYBACK", 18, 54, 132, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    cipher_volume_slider(card, value_label, volume);

    cipher_text(card, "0", 18, 123, 36, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    cipher_text(card, "100", 218, 123, 40, &lv_font_montserrat_12,
                CIPHER_MUTED, LV_TEXT_ALIGN_RIGHT);
}

void cipher_panel_show_aichat(lv_obj_t *parent)
{
    cipher_panel_aichat_show(parent);
}

void cipher_panel_pages_update(cipher_page_t page, uint32_t elapsed_ms)
{
    if (page == CIPHER_PAGE_AICHAT) {
        (void)elapsed_ms;
        cipher_panel_aichat_update();
        return;
    }

#if D_FEATURE_WIFI
    if (page != CIPHER_PAGE_WIFI) {
        return;
    }

    cipher_panel_wifi_update(elapsed_ms);
#else
    (void)page;
    (void)elapsed_ms;
#endif
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_CIPHER_PANEL) */
