/**
 * @file cipher_panel_pages.h
 * @brief Private page helpers for the cipher panel demo.
 */
#ifndef __CIPHER_PANEL_PAGES_H__
#define __CIPHER_PANEL_PAGES_H__

#include "lvgl.h"
#include <stdint.h>

#define CIPHER_TOP_H              34
#define CIPHER_ACCENT             0x00B7FF
#define CIPHER_ACCENT_2           0x21E6C1
#define CIPHER_PURPLE             0x7C4DFF
#define CIPHER_WARN               0xFF4D6D
#define CIPHER_BG                 0x030712
#define CIPHER_BG_PANEL           0x06101F
#define CIPHER_CARD               0x07324A
#define CIPHER_CARD_PRESSED       0x041B2A
#define CIPHER_ROW                0x071726
#define CIPHER_LINE               0x1B4B68
#define CIPHER_TEXT               0xD6F7FF
#define CIPHER_MUTED              0x6D8B9A

typedef enum {
    CIPHER_PAGE_HOME = 0,
    CIPHER_PAGE_LCD,
    CIPHER_PAGE_WIFI,
    CIPHER_PAGE_SYSTEM,
    CIPHER_PAGE_VOLUME,
    CIPHER_PAGE_AICHAT,
} cipher_page_t;

void cipher_page_header(lv_obj_t *parent, const char *title);
lv_obj_t *cipher_row(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *cipher_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius);
lv_obj_t *cipher_text(lv_obj_t *parent, const char *text, int x, int y, int w,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align);
lv_obj_t *cipher_chip(lv_obj_t *parent, const char *text, int x, int y, int w, uint32_t accent);
lv_obj_t *cipher_button(lv_obj_t *parent, const char *text, const char *icon,
                        int x, int y, int w, int h, cipher_page_t page, uint32_t accent);

void cipher_panel_pages_init(void);
void cipher_panel_show_lcd(lv_obj_t *parent);
void cipher_panel_show_wifi(lv_obj_t *parent);
void cipher_panel_show_system(lv_obj_t *parent);
void cipher_panel_show_volume(lv_obj_t *parent);
void cipher_panel_show_aichat(lv_obj_t *parent);
void cipher_panel_pages_update(cipher_page_t page, uint32_t elapsed_ms);
void cipher_panel_wifi_update(uint32_t elapsed_ms);

#endif /* __CIPHER_PANEL_PAGES_H__ */
