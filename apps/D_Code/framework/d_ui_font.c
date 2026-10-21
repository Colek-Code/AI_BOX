/**
 * @file d_ui_font.c
 * @brief Static LVGL fallback-chain construction.
 */

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "d_ui_font.h"

#include <string.h>

static lv_font_t s_default_font;
static lv_font_t s_cjk_font;
static lv_font_t s_emoji_font;
static uint8_t s_is_initialized;

d_err_t d_ui_font_init(const lv_font_t *fallback_tail)
{
    memcpy(&s_emoji_font, &lv_font_emoji_16, sizeof(s_emoji_font));
    s_emoji_font.fallback = fallback_tail;

    memcpy(&s_cjk_font, &lv_font_simsun_16_cjk, sizeof(s_cjk_font));
    s_cjk_font.fallback = &s_emoji_font;

    memcpy(&s_default_font, &lv_font_montserrat_14, sizeof(s_default_font));
    s_default_font.fallback = &s_cjk_font;
    s_is_initialized = 1U;
    return D_OK;
}

const lv_font_t *d_ui_font_get_default(void)
{
    if (s_is_initialized == 0U) {
        return &lv_font_montserrat_14;
    }
    return &s_default_font;
}

#endif /* D_LVGL_ENABLE */
