/**
 * @file product_ui_theme.h
 * @brief Product UI design tokens and reusable styles.
 */

#ifndef PRODUCT_UI_THEME_H
#define PRODUCT_UI_THEME_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

#define PRODUCT_UI_SCREEN_W              320
#define PRODUCT_UI_SCREEN_H              240
#define PRODUCT_UI_STATUS_H              20
#define PRODUCT_UI_NAV_H                 38
#define PRODUCT_UI_CONTENT_H             182
#define PRODUCT_UI_STATUS_PAD_H          10
#define PRODUCT_UI_NAV_PAD_H             10
#define PRODUCT_UI_PAGE_PAD              12

/*
 * Status-bar visual contract (320 x PRODUCT_UI_STATUS_H).
 * Time and secondary text use 12px; glyph geometry stays independent of fonts.
 * Right-side glyphs share ICON_H / ICON_GAP so signal, BT and battery read as one row.
 */
#define PRODUCT_UI_STATUS_ICON_GAP       6
#define PRODUCT_UI_STATUS_ICON_H         14
#define PRODUCT_UI_STATUS_WIFI_BAR_W     3
#define PRODUCT_UI_STATUS_WIFI_BAR_GAP   3
#define PRODUCT_UI_STATUS_WIFI_BAR_H0    4
#define PRODUCT_UI_STATUS_WIFI_BAR_H1    7
#define PRODUCT_UI_STATUS_WIFI_BAR_H2    10
#define PRODUCT_UI_STATUS_WIFI_BAR_H3    13
#define PRODUCT_UI_STATUS_BT_ICON        14
/*
 * Battery geometry mirrors portrait Cipher status bar (cipher_battery_icon):
 *   body 26x14, border-only shell; fill inset 2px → max 22x10; tip 2x6.
 * See: main_page/cipher_panel/cipher_panel_demo.c + knowledge card 08.
 */
#define PRODUCT_UI_STATUS_BAT_BODY_W     26
#define PRODUCT_UI_STATUS_BAT_BODY_H     14
#define PRODUCT_UI_STATUS_BAT_TIP_W      2
#define PRODUCT_UI_STATUS_BAT_TIP_H      6
#define PRODUCT_UI_STATUS_BAT_TIP_GAP    1
#define PRODUCT_UI_STATUS_BAT_INSET      2
#define PRODUCT_UI_STATUS_BAT_FILL_MAX   22
#define PRODUCT_UI_STATUS_BAT_FILL_H     10
/* Legacy alias kept so any remaining pad references still compile. */
#define PRODUCT_UI_STATUS_BAT_PAD        PRODUCT_UI_STATUS_BAT_INSET
#define PRODUCT_UI_STATUS_BAT_TRACK_OPA  LV_OPA_TRANSP
#if (D_UI_TARGET == D_UI_PRODUCT)
#define PRODUCT_UI_ROW_H                 36
#else
/* The existing 16px CJK fallback needs the legacy height for two-line rows. */
#define PRODUCT_UI_ROW_H                 48
#endif
#define PRODUCT_UI_TOUCH_MIN             36
/* Ten-pixel horizontal tracks retain a 38px touch area. */
#define PRODUCT_UI_SLIDER_TOUCH_EXTEND   14
#define PRODUCT_UI_ROW_SINGLE_H          36
#define PRODUCT_UI_ROW_DOUBLE_H          44
#define PRODUCT_UI_TOUCH_TARGET          48

#define PRODUCT_UI_SPACE_XS              4
#define PRODUCT_UI_SPACE_SM              8
#define PRODUCT_UI_SPACE_MD              12
#define PRODUCT_UI_SPACE_LG              16

#define PRODUCT_UI_RADIUS_SM             4
#define PRODUCT_UI_RADIUS_MD             8
#define PRODUCT_UI_RADIUS_LG             12
#define PRODUCT_UI_ANIM_FAST_MS          140
#define PRODUCT_UI_ANIM_NORMAL_MS        180

/*
 * Control-center geometry — sole source of truth for 320x240 GC9307 layout.
 * Columns 108/80/36/36 with 12px gaps inside the 296px content width.
 */
#define PRODUCT_UI_CC_WIFI_X               12
#define PRODUCT_UI_CC_WIFI_Y               36
#define PRODUCT_UI_CC_WIFI_W              108
#define PRODUCT_UI_CC_WIFI_H               51
#define PRODUCT_UI_CC_FOCUS_X             132
#define PRODUCT_UI_CC_FOCUS_Y              36
#define PRODUCT_UI_CC_FOCUS_W              80
#define PRODUCT_UI_CC_FOCUS_H              51
#define PRODUCT_UI_CC_MEDIA_X              12
#define PRODUCT_UI_CC_MEDIA_Y              93
#define PRODUCT_UI_CC_MEDIA_W             108
#define PRODUCT_UI_CC_MEDIA_H             111
#define PRODUCT_UI_CC_QUICK0_X            132
#define PRODUCT_UI_CC_QUICK1_X            176
#define PRODUCT_UI_CC_QUICK_Y0             99
#define PRODUCT_UI_CC_QUICK_Y1            153
#define PRODUCT_UI_CC_QUICK_SIZE           36
#define PRODUCT_UI_CC_SLIDER_W             36
#define PRODUCT_UI_CC_SLIDER_H            168
#define PRODUCT_UI_CC_SLIDER_0_X          224
#define PRODUCT_UI_CC_SLIDER_1_X          272
#define PRODUCT_UI_CC_SLIDER_Y             36
/* Card-local transport: three independent 36px targets. */
#define PRODUCT_UI_CC_MEDIA_TRANSPORT_Y    75
#define PRODUCT_UI_CC_MEDIA_TRANSPORT_W   108
#define PRODUCT_UI_CC_MEDIA_TRANSPORT_H    36
#define PRODUCT_UI_CC_MEDIA_HIT_W0         36
#define PRODUCT_UI_CC_MEDIA_HIT_W1         36
#define PRODUCT_UI_CC_MEDIA_HIT_W2         36
#define PRODUCT_UI_CC_MEDIA_HIT_H          36
#define PRODUCT_UI_CC_MEDIA_BTN_VIS        21
#define PRODUCT_UI_CC_MEDIA_GLYPH_SIZE     12

#define PRODUCT_UI_CONTROL_QUICK_HIT_PAD  2
#define PRODUCT_UI_CONTROL_DRAG_CAPTURE  8
#define PRODUCT_UI_CONTROL_DRAG_RANGE    160
#define PRODUCT_UI_CONTROL_OPEN_AT       450
#define PRODUCT_UI_CONTROL_CLOSE_AT      550
#define PRODUCT_UI_CONTROL_VELOCITY      450
/* JXY 480→320 scale: corner 18 * 2/3 = 12 (keep circles circular). */
#define PRODUCT_UI_CONTROL_RADIUS        12

typedef enum {
    PRODUCT_UI_COLOR_PAGE,
    PRODUCT_UI_COLOR_SURFACE,
    PRODUCT_UI_COLOR_SURFACE_ALT,
    PRODUCT_UI_COLOR_TEXT,
    PRODUCT_UI_COLOR_TEXT_MUTED,
    PRODUCT_UI_COLOR_ACCENT,
    PRODUCT_UI_COLOR_SUCCESS,
    PRODUCT_UI_COLOR_WARNING,
    PRODUCT_UI_COLOR_DANGER,
    PRODUCT_UI_COLOR_ACTION,
    PRODUCT_UI_COLOR_ACTION_TEXT,
    PRODUCT_UI_COLOR_ACTION_PRESSED,
    PRODUCT_UI_COLOR_ACTION_DANGER,
    PRODUCT_UI_COLOR_SUCCESS_TEXT,
    PRODUCT_UI_COLOR_WARNING_TEXT,
    PRODUCT_UI_COLOR_DANGER_TEXT,
    PRODUCT_UI_COLOR_BORDER,
    PRODUCT_UI_COLOR_SURFACE_MEDIA,
    PRODUCT_UI_COLOR_SURFACE_QUICK,
    PRODUCT_UI_COLOR_SURFACE_SLIDER,
    PRODUCT_UI_COLOR_SURFACE_PRESSED,
    PRODUCT_UI_COLOR_ON_SURFACE,
    PRODUCT_UI_COLOR_ON_SURFACE_VARIANT,
    PRODUCT_UI_COLOR_PRIMARY,
    PRODUCT_UI_COLOR_ON_PRIMARY,
    PRODUCT_UI_COLOR_SLIDER_FILL,
    PRODUCT_UI_COLOR_SLIDER_ICON,
    PRODUCT_UI_COLOR_CONTROL_BORDER,
    PRODUCT_UI_COLOR_PRIMARY_SOFT,
    PRODUCT_UI_COLOR_STATUS_BG,
    PRODUCT_UI_COLOR_ON_STATUS,
    PRODUCT_UI_COLOR_SIGNAL,
    PRODUCT_UI_COLOR_WARM,
    PRODUCT_UI_COLOR_CONTROL_SCRIM,
    PRODUCT_UI_COLOR_CONTROL_TEXT,
    PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED,
    PRODUCT_UI_COLOR_CONTROL_GLYPH,
    PRODUCT_UI_COLOR_CONTROL_PLAY,
    PRODUCT_UI_COLOR_CONTROL_PLAY_PRESSED,
    PRODUCT_UI_COLOR_CONTROL_PLAY_GLYPH,
    PRODUCT_UI_COLOR_CONTROL_ACTIVE_TINT,
    PRODUCT_UI_COLOR_CONTROL_BTN_PRESSED,
    /* Media card 24x24 flat cover plate (JXY #354A62). */
    PRODUCT_UI_COLOR_MEDIA_ART_BG,
    PRODUCT_UI_COLOR_COUNT
} product_ui_color_e;

typedef enum {
    PRODUCT_UI_STYLE_PAGE,
    PRODUCT_UI_STYLE_STATUS,
    PRODUCT_UI_STYLE_NAV,
    PRODUCT_UI_STYLE_TITLE,
    PRODUCT_UI_STYLE_BODY,
    PRODUCT_UI_STYLE_CAPTION,
    PRODUCT_UI_STYLE_SURFACE,
    PRODUCT_UI_STYLE_INTERACTIVE,
    PRODUCT_UI_STYLE_PRESSED,
    PRODUCT_UI_STYLE_DISABLED,
    PRODUCT_UI_STYLE_SECTION,
    PRODUCT_UI_STYLE_CONTROL_CARD,
    PRODUCT_UI_STYLE_CONTROL_PRESSED,
    PRODUCT_UI_STYLE_COUNT
} product_ui_style_e;

typedef enum {
    PRODUCT_UI_FONT_STATUS,
    PRODUCT_UI_FONT_TITLE,
    PRODUCT_UI_FONT_BODY,
    PRODUCT_UI_FONT_CAPTION,
    PRODUCT_UI_FONT_DISPLAY_NUMBER,
    PRODUCT_UI_FONT_COUNT
} product_ui_font_e;

int product_ui_theme_init(void);
lv_color_t product_ui_color(product_ui_color_e token);
const lv_style_t *product_ui_style(product_ui_style_e token);
const lv_font_t *product_ui_font(product_ui_font_e token);

#endif
#endif /* PRODUCT_UI_THEME_H */
