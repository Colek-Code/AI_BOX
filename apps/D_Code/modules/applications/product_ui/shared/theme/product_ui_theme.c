/** @file product_ui_theme.c @brief Product UI 的具名颜色、字体、布局常量及共用样式。 */

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
    [PRODUCT_UI_COLOR_ACCENT] = PRODUCT_UI_PRIMARY_RGB,
    [PRODUCT_UI_COLOR_SUCCESS] = 0x26A66A,
    [PRODUCT_UI_COLOR_WARNING] = 0xF4B942,
    [PRODUCT_UI_COLOR_DANGER] = 0xE34E56,
    [PRODUCT_UI_COLOR_ACTION] = PRODUCT_UI_PRIMARY_RGB,
    [PRODUCT_UI_COLOR_ACTION_TEXT] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_ACTION_PRESSED] = PRODUCT_UI_PRIMARY_PRESSED_RGB,
    [PRODUCT_UI_COLOR_ACTION_DANGER] = 0xC52F3B,
    [PRODUCT_UI_COLOR_SUCCESS_TEXT] = 0x177A4B,
    [PRODUCT_UI_COLOR_WARNING_TEXT] = 0x8A5A00,
    [PRODUCT_UI_COLOR_DANGER_TEXT] = 0xC52F3B,
    [PRODUCT_UI_COLOR_BORDER] = 0xD7D8DE,
    /* 控制中心的不透明表面，SURFACE_* 仅供控制中心控件使用。 */
    [PRODUCT_UI_COLOR_SURFACE_MEDIA] = PRODUCT_UI_PALETTE_SURFACE_MEDIA,
    [PRODUCT_UI_COLOR_SURFACE_QUICK] = PRODUCT_UI_PALETTE_SURFACE_QUICK,
    [PRODUCT_UI_COLOR_SURFACE_SLIDER] = PRODUCT_UI_PALETTE_SURFACE_SLIDER,
    [PRODUCT_UI_COLOR_SURFACE_PRESSED] = PRODUCT_UI_PALETTE_SURFACE_PRESSED,
    /* 页面／列表表面文字继续遵循浅色调色约定。 */
    [PRODUCT_UI_COLOR_ON_SURFACE] = PRODUCT_UI_PALETTE_ON_SURFACE,
    [PRODUCT_UI_COLOR_ON_SURFACE_VARIANT] =
        PRODUCT_UI_PALETTE_ON_SURFACE_VARIANT,
    [PRODUCT_UI_COLOR_PRIMARY] = PRODUCT_UI_PRIMARY_RGB,
    [PRODUCT_UI_COLOR_ON_PRIMARY] = PRODUCT_UI_PALETTE_ON_PRIMARY,
    [PRODUCT_UI_COLOR_SLIDER_FILL] = 0xE2E4E8,
    [PRODUCT_UI_COLOR_SLIDER_ICON] = 0x292B35,
    [PRODUCT_UI_COLOR_CONTROL_BORDER] = PRODUCT_UI_PALETTE_CONTROL_BORDER,
    [PRODUCT_UI_COLOR_PRIMARY_SOFT] = 0xDDEBFF,
    [PRODUCT_UI_COLOR_STATUS_BG] = 0x10161A,
    [PRODUCT_UI_COLOR_ON_STATUS] = 0xF6F8F9,
    [PRODUCT_UI_COLOR_SIGNAL] = 0x22C7C9,
    [PRODUCT_UI_COLOR_WARM] = 0xFF6F61,
    /* 控制面板使用不透明背景，限制显示合成开销。 */
    [PRODUCT_UI_COLOR_CONTROL_SCRIM] = 0x101623,
    [PRODUCT_UI_COLOR_CONTROL_TEXT] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_CONTROL_TEXT_MUTED] = 0xA5A7AE,
    [PRODUCT_UI_COLOR_CONTROL_GLYPH] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_CONTROL_PLAY] = PRODUCT_UI_PALETTE_SURFACE_MEDIA,
    [PRODUCT_UI_COLOR_CONTROL_PLAY_PRESSED] = PRODUCT_UI_PALETTE_SURFACE_PRESSED,
    [PRODUCT_UI_COLOR_CONTROL_PLAY_GLYPH] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_CONTROL_ACTIVE_TINT] = PRODUCT_UI_PRIMARY_RGB,
    [PRODUCT_UI_COLOR_CONTROL_BTN_PRESSED] = 0x465268,
    [PRODUCT_UI_COLOR_MEDIA_ART_BG] = 0x354A62,
    /* 播放器使用与控制中心相同的中性表面色系。 */
    [PRODUCT_UI_COLOR_MUSIC_SURFACE] = PRODUCT_UI_PALETTE_SURFACE_MEDIA,
    [PRODUCT_UI_COLOR_ON_MUSIC_SURFACE] = 0xFFFFFF,
    [PRODUCT_UI_COLOR_ON_MUSIC_SURFACE_MUTED] = 0xA5A7AE,
    [PRODUCT_UI_COLOR_MUSIC_DISC] = 0x101623,
    [PRODUCT_UI_COLOR_MUSIC_DISC_RING] = 0x354A62,
    [PRODUCT_UI_COLOR_MUSIC_OUTLINE] = 0x465268,
    [PRODUCT_UI_COLOR_MUSIC_LIST_MUTED] = 0x63636B,
    [PRODUCT_UI_COLOR_MUSIC_LIST_GLYPH] = 0x63636B,
};

static lv_style_t s_styles[PRODUCT_UI_STYLE_COUNT];
static uint8_t s_is_initialized;

/* 仅在全部生成的 Noto Sans SC 对象参与构建时启用。 */
#if (D_UI_TARGET == D_UI_PRODUCT)
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_10);
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_12);
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_14);
LV_FONT_DECLARE(product_ui_font_noto_sans_sc_18);

/* 生成的 Noto 子集描述为只读，原回退仅含无中文的 montserrat；
 * 维护 RAM 副本并接到框架默认字体，使动态文本可用完整中文及表情回退链。 */
static lv_font_t s_font_noto_10;
static lv_font_t s_font_noto_12;
static lv_font_t s_font_noto_14;
static lv_font_t s_font_noto_18;
static uint8_t s_font_fallback_ready;

/**
 * @brief 复制只读 Noto 字体描述到 RAM，并一次性接到框架完整字体回退链。
 */
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

/**
 * @brief 借用具名字体，Product 配置下按需建立中文与表情回退。
 */
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

/**
 * @brief 设置标题、正文、说明和分组文字的颜色、字体及字距。
 */
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

/**
 * @brief 配置页面、面板与禁用样式的基础颜色、透明度和边框。
 */
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

/**
 * @brief 配置控制中心信息卡及按压态的圆角与不透明背景。
 */
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

/**
 * @brief 配置对话框按钮的主次、危险、禁用、焦点及选中状态样式。
 */
static void product_ui_init_dialog_styles(void)
{
    lv_style_t *base = &s_styles[PRODUCT_UI_STYLE_DIALOG_BUTTON];
    lv_color_t surface = product_ui_color(PRODUCT_UI_COLOR_SURFACE);
    lv_color_t secondary = product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT);
    lv_color_t danger = product_ui_color(PRODUCT_UI_COLOR_ACTION_DANGER);
    lv_style_set_radius(base, PRODUCT_UI_BUTTON_RADIUS);
    lv_style_set_bg_opa(base, LV_OPA_COVER);
    lv_style_set_opa(base, LV_OPA_COVER);
    lv_style_set_border_width(base, 0);
    lv_style_set_shadow_width(base, 0);
    lv_style_set_outline_width(base, 0);
    lv_style_set_pad_all(base, 0);
    lv_style_set_text_align(base, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_font(base, product_ui_font(PRODUCT_UI_FONT_BODY));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_PRIMARY], product_ui_color(PRODUCT_UI_COLOR_ACTION));
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_PRIMARY], product_ui_color(PRODUCT_UI_COLOR_ACTION_TEXT));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_SECONDARY], secondary);
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_SECONDARY], product_ui_color(PRODUCT_UI_COLOR_TEXT));
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_DIALOG_SECONDARY], 1);
    lv_style_set_border_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_SECONDARY], product_ui_color(PRODUCT_UI_COLOR_BORDER));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_DANGER], lv_color_mix(danger, surface, LV_OPA_10));
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_DANGER], danger);
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_DIALOG_DANGER], 1);
    lv_style_set_border_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_DANGER], lv_color_mix(danger, surface, LV_OPA_20));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_PRIMARY_PRESSED], product_ui_color(PRODUCT_UI_COLOR_ACTION_PRESSED));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_SECONDARY_PRESSED],
        lv_color_mix(product_ui_color(PRODUCT_UI_COLOR_TEXT), secondary, LV_OPA_10));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_DANGER_PRESSED], lv_color_mix(danger, surface, LV_OPA_20));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_DISABLED], secondary);
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_DISABLED], product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED));
    lv_style_set_border_width(&s_styles[PRODUCT_UI_STYLE_DIALOG_DISABLED], 0);
    lv_style_set_outline_color(&s_styles[PRODUCT_UI_STYLE_DIALOG_FOCUS], product_ui_color(PRODUCT_UI_COLOR_ACTION));
    lv_style_set_outline_width(&s_styles[PRODUCT_UI_STYLE_DIALOG_FOCUS], 2);
    lv_style_set_outline_pad(&s_styles[PRODUCT_UI_STYLE_DIALOG_FOCUS], 2);
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED], product_ui_color(PRODUCT_UI_COLOR_PRIMARY_SOFT));
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED], product_ui_color(PRODUCT_UI_COLOR_ACTION));
    lv_style_set_border_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED], product_ui_color(PRODUCT_UI_COLOR_ACTION));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED_PRESSED],
        lv_color_mix(product_ui_color(PRODUCT_UI_COLOR_ACTION), product_ui_color(PRODUCT_UI_COLOR_PRIMARY_SOFT), LV_OPA_10));
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED_PRESSED], product_ui_color(PRODUCT_UI_COLOR_ACTION));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED_DARK], product_ui_color(PRODUCT_UI_COLOR_ACTION));
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED_DARK], product_ui_color(PRODUCT_UI_COLOR_ACTION_TEXT));
    lv_style_set_bg_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED_DARK_PRESSED], product_ui_color(PRODUCT_UI_COLOR_ACTION_PRESSED));
    lv_style_set_text_color(&s_styles[PRODUCT_UI_STYLE_BUTTON_SELECTED_DARK_PRESSED], product_ui_color(PRODUCT_UI_COLOR_ACTION_TEXT));
}

/**
 * @brief 一次性初始化所有共用主题样式，并建立中文及表情字体回退链。
 */
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
    product_ui_init_dialog_styles();

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

/**
 * @brief 把具名颜色标记转换为 LVGL 颜色。
 */
lv_color_t product_ui_color(product_ui_color_e token)
{
    if ((token < 0) || (token >= PRODUCT_UI_COLOR_COUNT)) {
        token = PRODUCT_UI_COLOR_TEXT;
    }
    return lv_color_hex(s_color_values[token]);
}

/**
 * @brief 借用具名的共用样式。
 */
const lv_style_t *product_ui_style(product_ui_style_e token)
{
    if ((token < 0) || (token >= PRODUCT_UI_STYLE_COUNT)) {
        return NULL;
    }
    return &s_styles[token];
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
