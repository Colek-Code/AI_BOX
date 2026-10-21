/**
 * @file product_ui_theme.c
 * @brief Product UI design token implementation.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/assets/generated/product_ui_palette.h"
#include "framework/d_ui_font.h"
#include <string.h>

static const uint32_t s_color_values[PRODUCT_UI_COLOR_COUNT] = {
    [PRODUCT_UI_COLOR_PAGE] = 0xF2F2F7,
    [PRODUCT_UI_COLOR_SURFACE] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_SURFACE_ALT] = 0xE8E8ED,
    [PRODUCT_UI_COLOR_TEXT] = 0x1C1C1E,
    [PRODUCT_UI_COLOR_TEXT_MUTED] = 0x63636B,
    [PRODUCT_UI_COLOR_ACCENT] = 0x1677FF,
    [PRODUCT_UI_COLOR_SUCCESS] = 0x26A66A,
    [PRODUCT_UI_COLOR_WARNING] = 0xF4B942,
    [PRODUCT_UI_COLOR_DANGER] = 0xE34E56,
    [PRODUCT_UI_COLOR_ACTION] = 0x1264D9,
    [PRODUCT_UI_COLOR_ACTION_TEXT] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_ACTION_PRESSED] = 0x0F52B5,
    [PRODUCT_UI_COLOR_ACTION_DANGER] = 0xC52F3B,
    [PRODUCT_UI_COLOR_SUCCESS_TEXT] = 0x177A4B,
    [PRODUCT_UI_COLOR_WARNING_TEXT] = 0x8A5A00,
    [PRODUCT_UI_COLOR_DANGER_TEXT] = 0xC52F3B,
    [PRODUCT_UI_COLOR_BORDER] = 0xD7D8DE,
    /* CC solid chrome (SURFACE_* only used by control-center widgets). */
    [PRODUCT_UI_COLOR_SURFACE_MEDIA] = PRODUCT_UI_PALETTE_SURFACE_MEDIA,
    [PRODUCT_UI_COLOR_SURFACE_QUICK] = PRODUCT_UI_PALETTE_SURFACE_QUICK,
    [PRODUCT_UI_COLOR_SURFACE_SLIDER] = PRODUCT_UI_PALETTE_SURFACE_SLIDER,
    [PRODUCT_UI_COLOR_SURFACE_PRESSED] = PRODUCT_UI_PALETTE_SURFACE_PRESSED,
    /* Keep page/list on-surface tokens on the light palette. */
    [PRODUCT_UI_COLOR_ON_SURFACE] = PRODUCT_UI_PALETTE_ON_SURFACE,
    [PRODUCT_UI_COLOR_ON_SURFACE_VARIANT] =
        PRODUCT_UI_PALETTE_ON_SURFACE_VARIANT,
    [PRODUCT_UI_COLOR_PRIMARY] = PRODUCT_UI_PALETTE_PRIMARY,
    [PRODUCT_UI_COLOR_ON_PRIMARY] = PRODUCT_UI_PALETTE_ON_PRIMARY,
    [PRODUCT_UI_COLOR_SLIDER_FILL] = 0xE2E4E8,
    [PRODUCT_UI_COLOR_SLIDER_ICON] = 0x292B35,
    [PRODUCT_UI_COLOR_CONTROL_BORDER] = PRODUCT_UI_PALETTE_CONTROL_BORDER,
    [PRODUCT_UI_COLOR_PRIMARY_SOFT] = 0xDDEBFF,
    [PRODUCT_UI_COLOR_STATUS_BG] = 0x10161A,
    [PRODUCT_UI_COLOR_ON_STATUS] = 0xF6F8F9,
    [PRODUCT_UI_COLOR_SIGNAL] = 0x22C7C9,
    [PRODUCT_UI_COLOR_WARM] = 0xFF6F61,
    /* Opaque control sheet keeps the display composition bounded. */
    [PRODUCT_UI_COLOR_CONTROL_SCRIM] = 0x101623,
    [PRODUCT_UI_COLOR_CONTROL_TEXT] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED] = 0xA5A7AE,
    [PRODUCT_UI_COLOR_CONTROL_GLYPH] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_CONTROL_PLAY] = PRODUCT_UI_PALETTE_SURFACE_MEDIA,
    [PRODUCT_UI_COLOR_CONTROL_PLAY_PRESSED] = PRODUCT_UI_PALETTE_SURFACE_PRESSED,
    [PRODUCT_UI_COLOR_CONTROL_PLAY_GLYPH] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_CONTROL_ACTIVE_TINT] = 0x1683FF,
    [PRODUCT_UI_COLOR_CONTROL_BTN_PRESSED] = 0x465268,
    [PRODUCT_UI_COLOR_MEDIA_ART_BG] = 0x354A62,
};

static lv_style_t s_styles[PRODUCT_UI_STYLE_COUNT];
static uint8_t s_is_initialized;

/* Enable only when all generated Noto Sans SC objects are build members. */
#if (D_UI_TARGET == D_UI_PRODUCT)
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_10);
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_12);
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_14);
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_18);

/*
 * Generated Noto subsets are const and only fall back to montserrat (no CJK).
 * Keep RAM copies and chain to the framework default so dynamic text can use
 * the complete CJK and emoji fallback chain.
 */
static lv_font_t s_font_noto_10;
static lv_font_t s_font_noto_12;
static lv_font_t s_font_noto_14;
static lv_font_t s_font_noto_18;
static uint8_t s_font_fallback_ready;

static void product_ui_font_bind_cjk_fallback(void)
{
    if (s_font_fallback_ready != 0U) {
        return;
    }
    memcpy(&s_font_noto_10, &product_ui_font_noto_sans_sc_10,
           sizeof(s_font_noto_10));
    memcpy(&s_font_noto_12, &product_ui_font_noto_sans_sc_12,
           sizeof(s_font_noto_12));
    memcpy(&s_font_noto_14, &product_ui_font_noto_sans_sc_14,
           sizeof(s_font_noto_14));
    memcpy(&s_font_noto_18, &product_ui_font_noto_sans_sc_18,
           sizeof(s_font_noto_18));
    s_font_noto_10.fallback = d_ui_font_get_default();
    s_font_noto_12.fallback = d_ui_font_get_default();
    s_font_noto_14.fallback = d_ui_font_get_default();
    s_font_noto_18.fallback = d_ui_font_get_default();
    s_font_fallback_ready = 1U;
}
#endif

const lv_font_t *product_ui_font(product_ui_font_e token)
{
#if (D_UI_TARGET == D_UI_PRODUCT)
    product_ui_font_bind_cjk_fallback();
#endif
    switch (token) {
    case PRODUCT_UI_FONT_STATUS:
#if (D_UI_TARGET == D_UI_PRODUCT)
        return &s_font_noto_12;
#else
        return &lv_font_montserrat_12;
#endif
    case PRODUCT_UI_FONT_DISPLAY_NUMBER:
        return &lv_font_montserrat_32;
    case PRODUCT_UI_FONT_TITLE:
#if (D_UI_TARGET == D_UI_PRODUCT)
        return &s_font_noto_18;
#else
        return d_ui_font_get_default();
#endif
    case PRODUCT_UI_FONT_BODY:
#if (D_UI_TARGET == D_UI_PRODUCT)
        return &s_font_noto_14;
#else
        return d_ui_font_get_default();
#endif
    case PRODUCT_UI_FONT_CAPTION:
#if (D_UI_TARGET == D_UI_PRODUCT)
        return &s_font_noto_12;
#else
        return d_ui_font_get_default();
#endif
    default:
        return NULL;
    }
}

static void product_ui_init_text_styles(void)
{
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_TITLE],
                            product_ui_color(PRODUCT_UI_COLOR_TEXT));
    lv_style_set_text_font(&s_styles[PRODUCT_UI_STYLE_TITLE],
                           product_ui_font(PRODUCT_UI_FONT_TITLE));
    lv_style_set_text_letter_space(&s_styles[PRODUCT_UI_STYLE_TITLE], 0);

    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_BODY],
                            product_ui_color(PRODUCT_UI_COLOR_TEXT));
    lv_style_set_text_font(&s_styles[PRODUCT_UI_STYLE_BODY],
                           product_ui_font(PRODUCT_UI_FONT_BODY));
    lv_style_set_text_letter_space(&s_styles[PRODUCT_UI_STYLE_BODY], 0);

    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_CAPTION],
                            product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED));
    lv_style_set_text_font(&s_styles[PRODUCT_UI_STYLE_CAPTION],
                           product_ui_font(PRODUCT_UI_FONT_CAPTION));
    lv_style_set_text_letter_space(&s_styles[PRODUCT_UI_STYLE_CAPTION], 0);

    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_SECTION],
                            product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED));
    lv_style_set_text_font(&s_styles[PRODUCT_UI_STYLE_SECTION],
                           product_ui_font(PRODUCT_UI_FONT_CAPTION));
    lv_style_set_text_letter_space(&s_styles[PRODUCT_UI_STYLE_SECTION], 0);
}

static void product_ui_init_surface_styles(void)
{
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_PAGE],
                          product_ui_color(PRODUCT_UI_COLOR_PAGE));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_PAGE], LV_OPA_COVER);
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_PAGE], 0);
    lv_style_set_pad_all(&s_styles[PRODUCT_UI_STYLE_PAGE], 0);

    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_SURFACE],
                          product_ui_color(PRODUCT_UI_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_SURFACE], LV_OPA_COVER);
    lv_style_set_border_color(&s_styles[PRODUCT_UI_STYLE_SURFACE],
                              product_ui_color(PRODUCT_UI_COLOR_BORDER));
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_SURFACE], 1);
    lv_style_set_radius(&s_styles[PRODUCT_UI_STYLE_SURFACE], PRODUCT_UI_RADIUS_LG);

    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_INTERACTIVE],
                          product_ui_color(PRODUCT_UI_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_INTERACTIVE], LV_OPA_COVER);
    lv_style_set_radius(&s_styles[PRODUCT_UI_STYLE_INTERACTIVE], PRODUCT_UI_RADIUS_MD);
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_INTERACTIVE], 0);

    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_PRESSED],
                          product_ui_color(PRODUCT_UI_COLOR_PRIMARY_SOFT));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_PRESSED], LV_OPA_COVER);
    lv_style_set_radius(&s_styles[PRODUCT_UI_STYLE_PRESSED], PRODUCT_UI_RADIUS_MD);

    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_DISABLED],
                            product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED));
    lv_style_set_image_recolor(&s_styles[PRODUCT_UI_STYLE_DISABLED],
                               product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED));
    lv_style_set_image_recolor_opa(&s_styles[PRODUCT_UI_STYLE_DISABLED],
                                   LV_OPA_COVER);
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_DISABLED], 0);
}

static void product_ui_init_control_styles(void)
{
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_CONTROL_CARD],
                          product_ui_color(PRODUCT_UI_COLOR_SURFACE_QUICK));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_CONTROL_CARD], LV_OPA_COVER);
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_CONTROL_CARD], 0);
    lv_style_set_radius(&s_styles[PRODUCT_UI_STYLE_CONTROL_CARD],
                        PRODUCT_UI_CONTROL_RADIUS);
    lv_style_set_shadow_width(&s_styles[PRODUCT_UI_STYLE_CONTROL_CARD], 0);

    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_CONTROL_PRESSED],
                          product_ui_color(PRODUCT_UI_COLOR_SURFACE_PRESSED));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_CONTROL_PRESSED], LV_OPA_COVER);
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_CONTROL_PRESSED], 0);
    lv_style_set_radius(&s_styles[PRODUCT_UI_STYLE_CONTROL_PRESSED],
                        PRODUCT_UI_CONTROL_RADIUS);
}

int product_ui_theme_init(void)
{
    uint8_t index;

    if (s_is_initialized) {
        return 0;
    }

#if (D_UI_TARGET == D_UI_PRODUCT)
    product_ui_font_bind_cjk_fallback();
#endif

    for (index = 0; index < PRODUCT_UI_STYLE_COUNT; index++) {
        lv_style_init(&s_styles[index]);
    }

    product_ui_init_surface_styles();
    product_ui_init_control_styles();
    product_ui_init_text_styles();

    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_STATUS],
                          product_ui_color(PRODUCT_UI_COLOR_STATUS_BG));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_STATUS], LV_OPA_COVER);
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_STATUS],
                            product_ui_color(PRODUCT_UI_COLOR_ON_STATUS));
    lv_style_set_text_font(&s_styles[PRODUCT_UI_STYLE_STATUS],
                           product_ui_font(PRODUCT_UI_FONT_STATUS));
    lv_style_set_pad_hor(&s_styles[PRODUCT_UI_STYLE_STATUS],
                         PRODUCT_UI_STATUS_PAD_H);
    lv_style_set_pad_ver(&s_styles[PRODUCT_UI_STYLE_STATUS], 0);

    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_NAV],
                          product_ui_color(PRODUCT_UI_COLOR_PAGE));
    lv_style_set_bg_opa(&s_styles[PRODUCT_UI_STYLE_NAV], LV_OPA_COVER);
    lv_style_set_pad_hor(&s_styles[PRODUCT_UI_STYLE_NAV], 0);
    lv_style_set_pad_ver(&s_styles[PRODUCT_UI_STYLE_NAV], 0);

    s_is_initialized = 1;
    return 0;
}

lv_color_t product_ui_color(product_ui_color_e token)
{
    if ((token < 0) || (token >= PRODUCT_UI_COLOR_COUNT)) {
        token = PRODUCT_UI_COLOR_TEXT;
    }
    return lv_color_hex(s_color_values[token]);
}

const lv_style_t *product_ui_style(product_ui_style_e token)
{
    if ((token < 0) || (token >= PRODUCT_UI_STYLE_COUNT)) {
        return NULL;
    }
    return &s_styles[token];
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
