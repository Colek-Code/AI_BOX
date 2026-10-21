/**
 * @file cipher_panel_aichat.c
 * @brief Scrollable sentence subtitles for Cipher Panel AIChat.
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL)

#include "modules/applications/legacy_ui/cipher_panel/aichat/cipher_panel_aichat.h"
#include "modules/applications/qwen_subtitle/qwen_subtitle_text.h"
#include "modules/applications/legacy_ui/cipher_panel/pages/cipher_panel_pages.h"
#include "framework/d_ui_font.h"
#include "system/includes.h"
#include <string.h>

#define CIPHER_AICHAT_LOG_TAG         "[AICHAT] "
#define CIPHER_AICHAT_SENTENCE_MAX QWEN_SUBTITLE_SENTENCE_MAX
#define CIPHER_AICHAT_SENTENCE_TEXT_MAX QWEN_SUBTITLE_SENTENCE_TEXT_MAX
typedef qwen_subtitle_render_t cipher_panel_aichat_render_t;
#define CIPHER_AICHAT_AUTO_FOLLOW_PX  12
#define CIPHER_AICHAT_PANEL_X         14
#define CIPHER_AICHAT_PANEL_Y         58
#define CIPHER_AICHAT_PANEL_W         248
#define CIPHER_AICHAT_PANEL_H         258
#define CIPHER_AICHAT_ROW_W           228
#define CIPHER_AICHAT_LABEL_W         220

typedef struct {
    lv_obj_t *row;
    lv_obj_t *label;
    char text[CIPHER_AICHAT_SENTENCE_TEXT_MAX];
    uint8_t is_current;
} cipher_aichat_sentence_view_t;

static lv_obj_t *s_aichat_status_label = NULL;
static lv_obj_t *s_aichat_scroll_panel = NULL;
static cipher_aichat_sentence_view_t s_aichat_rows[CIPHER_AICHAT_SENTENCE_MAX];
static uint8_t s_aichat_auto_follow = 1U;
static uint8_t s_aichat_logged_sentence_count = 0xFFU;
static uint8_t s_aichat_logged_clip = 0xFFU;
static uint8_t s_aichat_logged_auto_follow = 0xFFU;

static void cipher_aichat_log_render_state(
        const cipher_panel_aichat_render_t *render)
{
    if (render == NULL) {
        return;
    }

    if ((s_aichat_logged_sentence_count != render->sentence_count) ||
        (s_aichat_logged_clip != render->is_clipped)) {
        s_aichat_logged_sentence_count = render->sentence_count;
        s_aichat_logged_clip = render->is_clipped;
        printf(CIPHER_AICHAT_LOG_TAG "rows=%u clip=%u follow=%u\n",
               (unsigned)render->sentence_count,
               (unsigned)render->is_clipped,
               (unsigned)s_aichat_auto_follow);
    }
}

static void cipher_aichat_set_auto_follow(uint8_t is_enabled)
{
    if (s_aichat_auto_follow == is_enabled) {
        return;
    }

    s_aichat_auto_follow = is_enabled;
    if (s_aichat_logged_auto_follow != is_enabled) {
        s_aichat_logged_auto_follow = is_enabled;
        printf(CIPHER_AICHAT_LOG_TAG "auto_follow=%u\n",
               (unsigned)is_enabled);
    }
}

static void cipher_aichat_scroll_event(lv_event_t *e)
{
    lv_obj_t *panel = lv_event_get_target(e);
    int32_t scroll_bottom;

    if (panel == NULL) {
        return;
    }

    scroll_bottom = lv_obj_get_scroll_bottom(panel);
    cipher_aichat_set_auto_follow(
        (scroll_bottom <= CIPHER_AICHAT_AUTO_FOLLOW_PX) ? 1U : 0U);
}

static lv_obj_t *cipher_aichat_create_row(uint8_t index)
{
    lv_obj_t *row;
    lv_obj_t *label;

    if ((s_aichat_scroll_panel == NULL) ||
        (index >= CIPHER_AICHAT_SENTENCE_MAX)) {
        return NULL;
    }

    row = lv_obj_create(s_aichat_scroll_panel);
    if (row == NULL) {
        return NULL;
    }
    lv_obj_set_width(row, CIPHER_AICHAT_ROW_W);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    s_aichat_rows[index].row = row;

    label = lv_label_create(row);
    if (label == NULL) {
        return row;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, CIPHER_AICHAT_LABEL_W);
    lv_obj_set_style_text_font(label, d_ui_font_get_default(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    s_aichat_rows[index].label = label;
    return row;
}

static void cipher_aichat_style_row(uint8_t index, uint8_t is_current)
{
    lv_obj_t *row;
    lv_obj_t *label;
    uint32_t text_color;

    if (index >= CIPHER_AICHAT_SENTENCE_MAX) {
        return;
    }

    row = s_aichat_rows[index].row;
    label = s_aichat_rows[index].label;
    if ((row == NULL) || (label == NULL)) {
        return;
    }
    if (s_aichat_rows[index].is_current == is_current) {
        return;
    }

    s_aichat_rows[index].is_current = is_current;
    text_color = (is_current != 0U) ? CIPHER_ACCENT_2 : CIPHER_TEXT;
    lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x082033), 0);
    lv_obj_set_style_bg_opa(
        row, (is_current != 0U) ? LV_OPA_70 : LV_OPA_TRANSP, 0);
}

static void cipher_aichat_hide_unused_rows(uint8_t from_index)
{
    uint8_t index;

    for (index = from_index; index < CIPHER_AICHAT_SENTENCE_MAX; index++) {
        if (s_aichat_rows[index].row != NULL) {
            lv_obj_add_flag(s_aichat_rows[index].row, LV_OBJ_FLAG_HIDDEN);
            s_aichat_rows[index].text[0] = '\0';
            s_aichat_rows[index].is_current = 0xFFU;
        }
    }
}

static void cipher_aichat_render_rows(
        const cipher_panel_aichat_render_t *render)
{
    uint8_t index;
    uint8_t is_current;

    if ((s_aichat_scroll_panel == NULL) || (render == NULL) ||
        (render->sentences == NULL)) {
        return;
    }

    for (index = 0U; index < render->sentence_count; index++) {
        if (s_aichat_rows[index].row == NULL) {
            (void)cipher_aichat_create_row(index);
        }
        if (s_aichat_rows[index].label != NULL) {
            lv_obj_remove_flag(s_aichat_rows[index].row, LV_OBJ_FLAG_HIDDEN);
            if (strcmp(s_aichat_rows[index].text,
                       render->sentences[index]) != 0) {
                strncpy(s_aichat_rows[index].text,
                        render->sentences[index],
                        sizeof(s_aichat_rows[index].text) - 1U);
                s_aichat_rows[index].text[
                    sizeof(s_aichat_rows[index].text) - 1U] = '\0';
                lv_label_set_text(s_aichat_rows[index].label,
                                  s_aichat_rows[index].text);
            }
            is_current = (index == (uint8_t)(render->sentence_count - 1U)) ?
                         1U : 0U;
            cipher_aichat_style_row(index, is_current);
        }
    }

    cipher_aichat_hide_unused_rows(render->sentence_count);
}

static void cipher_aichat_follow_latest(
        const cipher_panel_aichat_render_t *render)
{
    uint8_t latest_index;

    if ((s_aichat_auto_follow == 0U) || (render == NULL) ||
        (render->sentence_count == 0U)) {
        return;
    }

    latest_index = (uint8_t)(render->sentence_count - 1U);
    if (s_aichat_rows[latest_index].row != NULL) {
        lv_obj_update_layout(s_aichat_scroll_panel);
        lv_obj_scroll_to_view(s_aichat_rows[latest_index].row, LV_ANIM_OFF);
    }
}

static void cipher_aichat_refresh(uint8_t force)
{
    cipher_panel_aichat_render_t render;

    if ((s_aichat_status_label == NULL) || (s_aichat_scroll_panel == NULL)) {
        return;
    }

    if (qwen_subtitle_prepare(force, d_ui_font_get_default(), &render) == 0U) {
        return;
    }

    lv_label_set_text(s_aichat_status_label,
                      qwen_subtitle_status_text(render.state,
                                                      render.is_clipped));
    cipher_aichat_render_rows(&render);
    cipher_aichat_follow_latest(&render);
    cipher_aichat_log_render_state(&render);
}

static void cipher_aichat_reset_rows(void)
{
    uint8_t index;

    s_aichat_status_label = NULL;
    s_aichat_scroll_panel = NULL;
    for (index = 0U; index < CIPHER_AICHAT_SENTENCE_MAX; index++) {
        s_aichat_rows[index].row = NULL;
        s_aichat_rows[index].label = NULL;
        s_aichat_rows[index].text[0] = '\0';
        s_aichat_rows[index].is_current = 0xFFU;
    }
    s_aichat_auto_follow = 1U;
    s_aichat_logged_auto_follow = 0xFFU;
}

static lv_obj_t *cipher_aichat_create_panel(lv_obj_t *card)
{
    lv_obj_t *panel;

    panel = cipher_row(card, CIPHER_AICHAT_PANEL_X,
                       CIPHER_AICHAT_PANEL_Y,
                       CIPHER_AICHAT_PANEL_W,
                       CIPHER_AICHAT_PANEL_H);
    if (panel == NULL) {
        return NULL;
    }

    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_clip_corner(panel, 1, 0);
    lv_obj_set_style_pad_left(panel, 6, 0);
    lv_obj_set_style_pad_right(panel, 6, 0);
    lv_obj_set_style_pad_top(panel, 6, 0);
    lv_obj_set_style_pad_bottom(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(panel, cipher_aichat_scroll_event,
                        LV_EVENT_SCROLL, NULL);
    return panel;
}

void cipher_panel_aichat_init(void)
{
    (void)qwen_subtitle_init();
}

void cipher_panel_aichat_show(lv_obj_t *parent)
{
    lv_obj_t *card;

    cipher_aichat_reset_rows();
    cipher_page_header(parent, "AIChat");

    card = cipher_row(parent, 22, 104, 276, 340);
    if (card == NULL) {
        return;
    }

    cipher_rect(card, 12, 0, 90, 2, CIPHER_ACCENT, 0);
    cipher_rect(card, 106, 0, 42, 2, CIPHER_PURPLE, 0);
    cipher_text(card, "QWEN SUBTITLE", 18, 18, 160,
                &lv_font_montserrat_14, CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    s_aichat_status_label = cipher_text(card, "WAIT", 202, 18, 52,
                                        &lv_font_montserrat_12,
                                        CIPHER_PURPLE, LV_TEXT_ALIGN_CENTER);

    s_aichat_scroll_panel = cipher_aichat_create_panel(card);
    if (s_aichat_scroll_panel == NULL) {
        return;
    }

    cipher_aichat_refresh(1U);
}

void cipher_panel_aichat_update(void)
{
    cipher_aichat_refresh(0U);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_CIPHER_PANEL) */
