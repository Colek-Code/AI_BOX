/** @file product_ui_ai_chat_view.c @brief Scrollable subtitles with stable owned text. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_ai_chat_view.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include <string.h>

#define PRODUCT_AI_FOLLOW_MARGIN 12
#define PRODUCT_AI_SUBTITLE_HEIGHT 132

static void product_ai_scroll(lv_event_t *event)
{
    product_ui_ai_chat_view_t *view = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (view == NULL || view->panel == NULL || view->programmatic) return;
    if (code == LV_EVENT_SCROLL_BEGIN) view->user_scrolling = 1U;
    if (code == LV_EVENT_SCROLL_END) view->user_scrolling = 0U;
    view->auto_follow = lv_obj_get_scroll_bottom(view->panel) <= PRODUCT_AI_FOLLOW_MARGIN;
}

int product_ui_ai_chat_view_create(product_ui_ai_chat_view_t *view, lv_obj_t *parent)
{
    if (view == NULL || parent == NULL) return -1;
    memset(view, 0, sizeof(*view));
    view->font = product_ui_font(PRODUCT_UI_FONT_BODY);
    view->auto_follow = 1U;
    view->panel = lv_obj_create(parent);
    if (view->panel == NULL) return -1;
    lv_obj_remove_style_all(view->panel);
    lv_obj_set_pos(view->panel, 0, 26);
    lv_obj_set_size(view->panel, LV_PCT(100), PRODUCT_AI_SUBTITLE_HEIGHT);
    lv_obj_set_style_pad_all(view->panel, 0, 0);
    lv_obj_set_style_pad_row(view->panel, PRODUCT_UI_SPACE_XS, 0);
    lv_obj_set_flex_flow(view->panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(view->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(view->panel, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(view->panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(view->panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(view->panel, product_ai_scroll, LV_EVENT_SCROLL_BEGIN, view);
    lv_obj_add_event_cb(view->panel, product_ai_scroll, LV_EVENT_SCROLL, view);
    lv_obj_add_event_cb(view->panel, product_ai_scroll, LV_EVENT_SCROLL_END, view);
    return 0;
}

static int product_ai_row_create(product_ui_ai_chat_view_t *view, uint8_t index)
{
    product_ui_ai_chat_row_t *row = &view->rows[index];
    lv_obj_t *label = lv_label_create(view->panel);
    if (label == NULL) return -1;
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, view->font, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_all(label, PRODUCT_UI_SPACE_XS, 0);
    lv_obj_set_style_radius(label, PRODUCT_UI_RADIUS_SM, 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLLABLE);
    row->current = 0xFFU;
    row->label = label;
    lv_label_set_text_static(label, row->text);
    return 0;
}

static void product_ai_row_bind(product_ui_ai_chat_row_t *row, const char *text,
                                 uint8_t current)
{
    lv_obj_remove_flag(row->label, LV_OBJ_FLAG_HIDDEN);
    if (strcmp(row->text, text) != 0) {
        strncpy(row->text, text, sizeof(row->text) - 1U);
        row->text[sizeof(row->text) - 1U] = '\0';
        /* Storage lives with the page; updates need no LVGL text allocation. */
        lv_label_set_text_static(row->label, row->text);
    }
    if (row->current != current) {
        lv_obj_set_style_text_color(row->label, product_ui_color(current ?
            PRODUCT_UI_COLOR_ACCENT : PRODUCT_UI_COLOR_TEXT), 0);
        lv_obj_set_style_bg_color(row->label,
            product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT), 0);
        lv_obj_set_style_bg_opa(row->label, current ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        row->current = current;
    }
}

int product_ui_ai_chat_view_render(product_ui_ai_chat_view_t *view,
                                   const qwen_subtitle_render_t *render)
{
    uint8_t index;
    int32_t bottom;
    if (view == NULL || view->panel == NULL || render == NULL ||
        render->sentences == NULL || render->sentence_count > QWEN_SUBTITLE_SENTENCE_MAX)
        return -1;
    view->programmatic = 1U;
    for (index = 0U; index < render->sentence_count; index++) {
        if (view->rows[index].label == NULL && product_ai_row_create(view, index) != 0) {
            view->programmatic = 0U;
            return -1;
        }
        product_ai_row_bind(&view->rows[index], render->sentences[index],
                           index + 1U == render->sentence_count);
    }
    for (; index < QWEN_SUBTITLE_SENTENCE_MAX; index++) {
        if (view->rows[index].label != NULL) {
            lv_obj_add_flag(view->rows[index].label, LV_OBJ_FLAG_HIDDEN);
            view->rows[index].text[0] = '\0';
            view->rows[index].current = 0xFFU;
        }
    }
    lv_obj_update_layout(view->panel);
    if (view->auto_follow && !view->user_scrolling) {
        bottom = lv_obj_get_scroll_y(view->panel) + lv_obj_get_scroll_bottom(view->panel);
        lv_obj_scroll_to_y(view->panel, bottom > 0 ? bottom : 0, LV_ANIM_OFF);
    }
    view->programmatic = 0U;
    return 0;
}
#endif
