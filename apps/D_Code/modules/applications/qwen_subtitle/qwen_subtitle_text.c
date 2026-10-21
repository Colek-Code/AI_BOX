/**
 * @file qwen_subtitle_text.c
 * @brief Qwen subtitle text cache, filtering, and sentence splitting.
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && ((D_UI_TARGET == D_UI_CIPHER_PANEL) || (D_UI_TARGET == D_UI_PRODUCT))

#include "modules/applications/qwen_subtitle/qwen_subtitle_sentence.h"
#include "modules/applications/qwen_subtitle/qwen_subtitle_text.h"
#include "framework/d_ui_font.h"
#include "framework/d_event_bus.h"
#include "system/includes.h"
#include <string.h>

typedef struct {
    const char *src;
    uint16_t src_len;
    uint16_t src_pos;
    uint32_t codepoint;
    uint8_t char_len;
} qwen_subtitle_utf8_reader_t;

typedef struct {
    char *dst;
    uint16_t dst_size;
    uint16_t dst_len;
    uint8_t is_clipped;
} qwen_subtitle_text_writer_t;

static char s_subtitle_partial_text[QWEN_SUBTITLE_TEXT_MAX];
static char s_subtitle_final_text[QWEN_SUBTITLE_TEXT_MAX];
static char s_subtitle_render_source[QWEN_SUBTITLE_TEXT_MAX];
static char s_subtitle_render_text[QWEN_SUBTITLE_TEXT_MAX];
static char s_subtitle_sentence_text[QWEN_SUBTITLE_SENTENCE_MAX]
                                   [QWEN_SUBTITLE_SENTENCE_TEXT_MAX];
static volatile uint8_t s_subtitle_dirty;
static uint8_t s_subtitle_has_partial;
static uint8_t s_subtitle_has_final;
static uint8_t s_subtitle_has_clip;
static uint8_t s_subtitle_render_clip;
static uint8_t s_subtitle_sentence_count;
static uint8_t s_subtitle_is_subscribed;
static d_err_t s_subtitle_init_result = D_ERR_NOT_READY;
static uint32_t s_subtitle_revision;
static const lv_font_t *s_subtitle_font;
static qwen_subtitle_state_t s_subtitle_state =
    QWEN_SUBTITLE_STATE_IDLE;
static qwen_subtitle_state_t s_subtitle_render_state =
    QWEN_SUBTITLE_STATE_IDLE;

static uint8_t qwen_subtitle_is_utf8_tail(uint8_t byte)
{
    return (uint8_t)((byte & 0xC0U) == 0x80U);
}

static void qwen_subtitle_reader_init(qwen_subtitle_utf8_reader_t *reader,
                                      const char *src, uint16_t src_len)
{
    reader->src = src;
    reader->src_len = src_len;
    reader->src_pos = 0U;
    reader->codepoint = 0U;
    reader->char_len = 0U;
}

static void qwen_subtitle_writer_init(qwen_subtitle_text_writer_t *writer,
                                      char *dst, uint16_t dst_size)
{
    writer->dst = dst;
    writer->dst_size = dst_size;
    writer->dst_len = 0U;
    writer->is_clipped = 0U;
    if ((dst != NULL) && (dst_size != 0U)) {
        dst[0] = '\0';
    }
}

static void qwen_subtitle_decode_invalid(qwen_subtitle_utf8_reader_t *reader)
{
    reader->codepoint = '?';
    reader->char_len = 1U;
}

static uint8_t qwen_subtitle_decode_two(qwen_subtitle_utf8_reader_t *reader,
                                        const uint8_t *bytes, uint8_t first)
{
    if ((first >= 0xC2U) && (first <= 0xDFU) &&
        ((uint16_t)(reader->src_pos + 1U) < reader->src_len) &&
        (qwen_subtitle_is_utf8_tail(bytes[reader->src_pos + 1U]) != 0U)) {
        reader->codepoint = (((uint32_t)first & 0x1FU) << 6) |
            ((uint32_t)bytes[reader->src_pos + 1U] & 0x3FU);
        reader->char_len = 2U;
        return 1U;
    }

    return 0U;
}

static uint8_t qwen_subtitle_decode_three(qwen_subtitle_utf8_reader_t *reader,
                                          const uint8_t *bytes, uint8_t first)
{
    if ((first >= 0xE0U) && (first <= 0xEFU) &&
        ((uint16_t)(reader->src_pos + 2U) < reader->src_len) &&
        (qwen_subtitle_is_utf8_tail(bytes[reader->src_pos + 1U]) != 0U) &&
        (qwen_subtitle_is_utf8_tail(bytes[reader->src_pos + 2U]) != 0U) &&
        !((first == 0xE0U) && (bytes[reader->src_pos + 1U] < 0xA0U)) &&
        !((first == 0xEDU) && (bytes[reader->src_pos + 1U] >= 0xA0U))) {
        reader->codepoint = (((uint32_t)first & 0x0FU) << 12) |
            (((uint32_t)bytes[reader->src_pos + 1U] & 0x3FU) << 6) |
            ((uint32_t)bytes[reader->src_pos + 2U] & 0x3FU);
        reader->char_len = 3U;
        return 1U;
    }

    return 0U;
}

static uint8_t qwen_subtitle_decode_four(qwen_subtitle_utf8_reader_t *reader,
                                         const uint8_t *bytes, uint8_t first)
{
    if ((first >= 0xF0U) && (first <= 0xF4U) &&
        ((uint16_t)(reader->src_pos + 3U) < reader->src_len) &&
        (qwen_subtitle_is_utf8_tail(bytes[reader->src_pos + 1U]) != 0U) &&
        (qwen_subtitle_is_utf8_tail(bytes[reader->src_pos + 2U]) != 0U) &&
        (qwen_subtitle_is_utf8_tail(bytes[reader->src_pos + 3U]) != 0U) &&
        !((first == 0xF0U) && (bytes[reader->src_pos + 1U] < 0x90U)) &&
        !((first == 0xF4U) && (bytes[reader->src_pos + 1U] >= 0x90U))) {
        reader->codepoint = (((uint32_t)first & 0x07U) << 18) |
            (((uint32_t)bytes[reader->src_pos + 1U] & 0x3FU) << 12) |
            (((uint32_t)bytes[reader->src_pos + 2U] & 0x3FU) << 6) |
            ((uint32_t)bytes[reader->src_pos + 3U] & 0x3FU);
        reader->char_len = 4U;
        return 1U;
    }

    return 0U;
}

static uint8_t qwen_subtitle_decode_utf8(qwen_subtitle_utf8_reader_t *reader)
{
    const uint8_t *bytes;
    uint8_t first;

    if ((reader == NULL) || (reader->src == NULL) ||
        (reader->src_pos >= reader->src_len)) {
        return 0U;
    }

    bytes = (const uint8_t *)reader->src;
    first = bytes[reader->src_pos];
    if (first < 0x80U) {
        reader->codepoint = first;
        reader->char_len = 1U;
        return 1U;
    }

    if ((qwen_subtitle_decode_two(reader, bytes, first) != 0U) ||
        (qwen_subtitle_decode_three(reader, bytes, first) != 0U) ||
        (qwen_subtitle_decode_four(reader, bytes, first) != 0U)) {
        return 1U;
    }

    qwen_subtitle_decode_invalid(reader);
    return 0U;
}

static uint8_t qwen_subtitle_append_ascii(qwen_subtitle_text_writer_t *writer,
                                          char value)
{
    if ((writer == NULL) || (writer->dst == NULL) ||
        (writer->dst_size == 0U) ||
        (writer->dst_len >= (writer->dst_size - 1U))) {
        if (writer != NULL) {
            writer->is_clipped = 1U;
        }
        return 0U;
    }

    writer->dst[writer->dst_len] = value;
    writer->dst_len++;
    writer->dst[writer->dst_len] = '\0';
    return 1U;
}

static uint8_t qwen_subtitle_append_utf8(qwen_subtitle_text_writer_t *writer,
                                         const char *src, uint8_t char_len)
{
    uint8_t index;

    if ((writer == NULL) || (writer->dst == NULL) || (src == NULL) ||
        ((uint16_t)(writer->dst_len + char_len) >= writer->dst_size)) {
        if (writer != NULL) {
            writer->is_clipped = 1U;
        }
        return 0U;
    }

    for (index = 0U; index < char_len; index++) {
        writer->dst[writer->dst_len] = src[index];
        writer->dst_len++;
    }
    writer->dst[writer->dst_len] = '\0';
    return 1U;
}

static uint8_t qwen_subtitle_is_combining_mark(uint32_t codepoint)
{
    if (((codepoint >= 0x0300U) && (codepoint <= 0x036FU)) ||
        ((codepoint >= 0x1AB0U) && (codepoint <= 0x1AFFU)) ||
        ((codepoint >= 0x1DC0U) && (codepoint <= 0x1DFFU)) ||
        ((codepoint >= 0x20D0U) && (codepoint <= 0x20FFU)) ||
        ((codepoint >= 0xFE20U) && (codepoint <= 0xFE2FU))) {
        return 1U;
    }

    return 0U;
}

static uint8_t qwen_subtitle_is_format_selector(uint32_t codepoint)
{
    if ((codepoint == 0x200DU) ||
        ((codepoint >= 0xFE00U) && (codepoint <= 0xFE0FU)) ||
        ((codepoint >= 0xE0100U) && (codepoint <= 0xE01EFU))) {
        return 1U;
    }

    return 0U;
}

static uint8_t qwen_subtitle_font_has_glyph(uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph_dsc;

    if ((codepoint == '\n') || (codepoint == '\r') || (codepoint == '\t')) {
        return 1U;
    }

    return (uint8_t)(lv_font_get_glyph_dsc(s_subtitle_font, &glyph_dsc,
                                           codepoint, 0U) ? 1U : 0U);
}

static uint8_t qwen_subtitle_append_missing_glyph(
        qwen_subtitle_text_writer_t *writer, uint32_t codepoint)
{
    if (qwen_subtitle_is_combining_mark(codepoint) != 0U) {
        return 1U;
    }

    if (codepoint == 0x1D17U) {
        return qwen_subtitle_append_ascii(writer, 'v');
    }

    if (codepoint == 0x0648U) {
        return qwen_subtitle_append_ascii(writer, 'w');
    }

    return qwen_subtitle_append_ascii(writer, '?');
}

static uint8_t qwen_subtitle_copy_event_text(char *dst, uint16_t dst_size,
                                             const d_event_t *event,
                                             uint8_t *has_changed)
{
    const char *src;
    uint16_t copy_len;
    uint16_t index;

    if ((dst == NULL) || (dst_size == 0U) || (event == NULL) ||
        (event->data == NULL) || (event->data_len == 0U)) {
        return 0U;
    }

    src = (const char *)event->data;
    copy_len = event->data_len;
    if (copy_len >= dst_size) {
        copy_len = (uint16_t)(dst_size - 1U);
    }

    for (index = 0U; index < copy_len; index++) {
        if (src[index] == '\0') {
            break;
        }
        if (dst[index] != src[index]) {
            *has_changed = 1U;
        }
        dst[index] = src[index];
    }
    if (dst[index] != '\0') {
        *has_changed = 1U;
    }
    dst[index] = '\0';
    if ((index == copy_len) && (event->data_len > copy_len) &&
        (src[index] != '\0')) {
        return 1U;
    }

    return 0U;
}

static uint8_t qwen_subtitle_append_display_char(
        qwen_subtitle_text_writer_t *writer,
        qwen_subtitle_utf8_reader_t *reader,
        uint8_t is_valid)
{
    if (is_valid == 0U) {
        return qwen_subtitle_append_ascii(writer, '?');
    }

    if (reader->codepoint == '\r') {
        return 1U;
    }
    if (reader->codepoint == '\t') {
        return qwen_subtitle_append_ascii(writer, ' ');
    }
    if (qwen_subtitle_is_format_selector(reader->codepoint) != 0U) {
        return 1U;
    }
    if (qwen_subtitle_font_has_glyph(reader->codepoint) != 0U) {
        return qwen_subtitle_append_utf8(writer, &reader->src[reader->src_pos],
                                         reader->char_len);
    }

    return qwen_subtitle_append_missing_glyph(writer, reader->codepoint);
}

static uint8_t qwen_subtitle_copy_display_text(char *dst, uint16_t dst_size,
                                               const char *src)
{
    qwen_subtitle_utf8_reader_t reader;
    qwen_subtitle_text_writer_t writer;
    uint8_t is_valid;

    if ((dst == NULL) || (dst_size == 0U)) {
        return 0U;
    }

    qwen_subtitle_writer_init(&writer, dst, dst_size);
    if (src == NULL) {
        return 0U;
    }

    qwen_subtitle_reader_init(&reader, src, (uint16_t)strlen(src));
    while ((reader.src_pos < reader.src_len) &&
           (reader.src[reader.src_pos] != '\0')) {
        if ((reader.src[reader.src_pos] == '*') &&
            ((uint16_t)(reader.src_pos + 1U) < reader.src_len) &&
            (reader.src[reader.src_pos + 1U] == '*')) {
            reader.src_pos = (uint16_t)(reader.src_pos + 2U);
            continue;
        }
        is_valid = qwen_subtitle_decode_utf8(&reader);
        if (qwen_subtitle_append_display_char(&writer, &reader,
                                               is_valid) == 0U) {
            break;
        }
        reader.src_pos = (uint16_t)(reader.src_pos + reader.char_len);
    }
    writer.dst[writer.dst_len] = '\0';
    return writer.is_clipped;
}

static void qwen_subtitle_event(const d_event_t *event, void *ctx)
{
    uint8_t is_clipped;
    uint8_t has_changed = 0U;

    (void)ctx;

    if ((event == NULL) || (event->data == NULL) || (event->data_len == 0U)) {
        return;
    }
    if (((const char *)event->data)[0] == '\0') {
        return;
    }

    OS_ENTER_CRITICAL();
    if (event->id == EVT_QWEN_LLM_PARTIAL_RESULT) {
        is_clipped = qwen_subtitle_copy_event_text(
            s_subtitle_partial_text, sizeof(s_subtitle_partial_text), event,
            &has_changed);
        if ((s_subtitle_partial_text[0] != '\0') &&
            (has_changed || s_subtitle_state != QWEN_SUBTITLE_STATE_STREAMING ||
             s_subtitle_has_clip != is_clipped)) {
            s_subtitle_has_partial = 1U;
            s_subtitle_has_clip = is_clipped;
            s_subtitle_state = QWEN_SUBTITLE_STATE_STREAMING;
            s_subtitle_dirty = 1U;
            s_subtitle_revision++;
        }
    } else if (event->id == EVT_QWEN_LLM_RESULT) {
        is_clipped = qwen_subtitle_copy_event_text(
            s_subtitle_final_text, sizeof(s_subtitle_final_text), event,
            &has_changed);
        if ((s_subtitle_final_text[0] != '\0') &&
            (has_changed || s_subtitle_state != QWEN_SUBTITLE_STATE_FINAL ||
             s_subtitle_has_clip != is_clipped)) {
            s_subtitle_has_final = 1U;
            s_subtitle_has_clip = is_clipped;
            s_subtitle_state = QWEN_SUBTITLE_STATE_FINAL;
            s_subtitle_dirty = 1U;
            s_subtitle_revision++;
        }
    }
    OS_EXIT_CRITICAL();
}

static void qwen_subtitle_select_render_source(void)
{
    if ((s_subtitle_state == QWEN_SUBTITLE_STATE_STREAMING) &&
        (s_subtitle_has_partial != 0U)) {
        strncpy(s_subtitle_render_source, s_subtitle_partial_text,
                sizeof(s_subtitle_render_source) - 1U);
    } else if (s_subtitle_has_final != 0U) {
        strncpy(s_subtitle_render_source, s_subtitle_final_text,
                sizeof(s_subtitle_render_source) - 1U);
    } else if (s_subtitle_has_partial != 0U) {
        strncpy(s_subtitle_render_source, s_subtitle_partial_text,
                sizeof(s_subtitle_render_source) - 1U);
    } else {
        s_subtitle_render_source[0] = '\0';
    }
    s_subtitle_render_source[sizeof(s_subtitle_render_source) - 1U] = '\0';
}

static void qwen_subtitle_fill_render(qwen_subtitle_render_t *render)
{
    if (render == NULL) {
        return;
    }

    render->sentences = s_subtitle_sentence_text;
    render->sentence_count = s_subtitle_sentence_count;
    render->is_clipped = s_subtitle_render_clip;
    render->state = s_subtitle_render_state;
    render->init_result = s_subtitle_init_result;
}

d_err_t qwen_subtitle_init(void)
{
#if D_FEATURE_QWEN
    if (s_subtitle_is_subscribed != 0U) {
        return D_OK;
    }

    if (d_event_subscribe(EVT_QWEN_LLM_PARTIAL_RESULT,
                          qwen_subtitle_event, NULL) != 0) {
        s_subtitle_init_result = D_ERR_IO;
        return s_subtitle_init_result;
    }
    if (d_event_subscribe(EVT_QWEN_LLM_RESULT,
                          qwen_subtitle_event, NULL) != 0) {
        d_event_unsubscribe(EVT_QWEN_LLM_PARTIAL_RESULT,
                            qwen_subtitle_event);
        s_subtitle_init_result = D_ERR_IO;
        return s_subtitle_init_result;
    }

    s_subtitle_is_subscribed = 1U;
    s_subtitle_init_result = D_OK;
#endif
    return s_subtitle_init_result;
}

uint8_t qwen_subtitle_prepare(uint8_t force, const lv_font_t *font,
                              qwen_subtitle_render_t *render)
{
    uint8_t should_render;

    if (render == NULL) {
        return 0U;
    }
    s_subtitle_font = (font != NULL) ? font : d_ui_font_get_default();
    OS_ENTER_CRITICAL();
    should_render = (uint8_t)((force != 0U) || (s_subtitle_dirty != 0U));
    if (should_render != 0U) {
        s_subtitle_dirty = 0U;
        s_subtitle_render_state = s_subtitle_state;
        s_subtitle_render_clip = s_subtitle_has_clip;
        render->revision = s_subtitle_revision;
        qwen_subtitle_select_render_source();
    }
    OS_EXIT_CRITICAL();

    if (should_render == 0U) {
        return 0U;
    }

    s_subtitle_render_clip |= qwen_subtitle_copy_display_text(
        s_subtitle_render_text, sizeof(s_subtitle_render_text),
        s_subtitle_render_source);
    s_subtitle_render_clip |= qwen_subtitle_split_sentences(
        s_subtitle_render_text, s_subtitle_sentence_text,
        &s_subtitle_sentence_count);
    qwen_subtitle_fill_render(render);
    return 1U;
}

const char *qwen_subtitle_status_text(qwen_subtitle_state_t state,
                                            uint8_t is_clipped)
{
    if (is_clipped != 0U) {
        return "CLIP";
    }
    if (state == QWEN_SUBTITLE_STATE_STREAMING) {
        return "LIVE";
    }
    if (state == QWEN_SUBTITLE_STATE_FINAL) {
        return "FINAL";
    }
    return "WAIT";
}

#endif /* LVGL Cipher or Product UI */
