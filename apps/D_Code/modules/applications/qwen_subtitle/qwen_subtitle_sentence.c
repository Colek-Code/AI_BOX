/** @file qwen_subtitle_sentence.c @brief AI 字幕的固定缓冲分句、纯标点过滤及容量裁剪。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && ((D_UI_TARGET == D_UI_CIPHER_PANEL) || (D_UI_TARGET == D_UI_PRODUCT))

#include "modules/applications/qwen_subtitle/qwen_subtitle_sentence.h"
#include <string.h>

#define QWEN_SUBTITLE_WAIT_TEXT  "Waiting for Qwen response"

typedef struct {
    char *dst;
    uint16_t dst_size;
    uint16_t dst_len;
    uint8_t is_clipped;
} qwen_subtitle_sentence_writer_t;

typedef struct {
    const char *text;
    uint16_t text_len;
    qwen_subtitle_sentence_writer_t *writer;
    char (*sentence_text)[QWEN_SUBTITLE_SENTENCE_TEXT_MAX];
    uint8_t *sentence_count;
    uint8_t *is_clipped;
} qwen_subtitle_sentence_split_t;

static void qwen_subtitle_sentence_flush(
        qwen_subtitle_sentence_writer_t *writer,
        char sentence_text[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX],
        uint8_t *sentence_count, uint8_t *is_clipped);

/**
 * @brief 绑定单行工作缓冲并重置长度与裁剪标志。
 * @param[out] writer 调用方保证非空的写入器。
 * @param[out] dst 借用的单行缓冲，可为空。
 * @param[in] dst_size 含 NUL 的缓冲容量。
 */
static void qwen_subtitle_sentence_writer_init(
        qwen_subtitle_sentence_writer_t *writer,
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

/**
 * @brief 把一个完整字符的字节追加到单行工作缓冲。
 * @param[in,out] writer 单行写入器。
 * @param[in] src 至少 byte_len 字节的借用字符。
 * @param[in] byte_len 字符字节数。
 * @return 成功返回 1，参数或剩余空间不足返回 0；纯容量不足由外层换行，不在此标记裁剪。
 */
static uint8_t qwen_subtitle_sentence_append_bytes(
        qwen_subtitle_sentence_writer_t *writer,
        const char *src, uint8_t byte_len)
{
    uint8_t index;

    if ((writer == NULL) || (writer->dst == NULL) || (src == NULL) ||
        ((uint16_t)(writer->dst_len + byte_len) >= writer->dst_size)) {
        if ((writer != NULL) &&
            ((writer->dst == NULL) || (src == NULL))) {
            writer->is_clipped = 1U;
        }
        return 0U;
    }

    for (index = 0U; index < byte_len; index++) {
        writer->dst[writer->dst_len] = src[index];
        writer->dst_len++;
    }
    writer->dst[writer->dst_len] = '\0';
    return 1U;
}

/**
 * @brief 尝试追加字符，单行满时先提交已有行再重试。
 * @param[in,out] split 只读分句描述，其引用的写入器、数组及计数被更新。
 * @param[in] src 借用字符字节。
 * @param[in] byte_len 完整字符的字节长度。
 * @return 字符追加成功返回 1；行数耗尽或重试失败返回 0 并标记裁剪。
 */
static uint8_t qwen_subtitle_sentence_append_wrapped(
        const qwen_subtitle_sentence_split_t *split,
        const char *src, uint8_t byte_len)
{
    if (qwen_subtitle_sentence_append_bytes(split->writer,
                                            src, byte_len) != 0U) {
        return 1U;
    }

    qwen_subtitle_sentence_flush(split->writer,
                                 split->sentence_text,
                                 split->sentence_count,
                                 split->is_clipped);
    if (*split->sentence_count >= QWEN_SUBTITLE_SENTENCE_MAX) {
        if (split->is_clipped != NULL) {
            *split->is_clipped = 1U;
        }
        return 0U;
    }

    if (qwen_subtitle_sentence_append_bytes(split->writer,
                                            src, byte_len) != 0U) {
        return 1U;
    }

    if (split->is_clipped != NULL) {
        *split->is_clipped = 1U;
    }
    return 0U;
}

/**
 * @brief 按首字节及剩余长度推定字符步长，不校验续字节或码点范围。
 * @param[in] text 已清洗的有限文字。
 * @param[in] text_len 文字可读长度。
 * @param[in] pos 当前字节位置。
 * @return 无可读位置返回零；有完整首字节长度时为 1 至 4，其他情况按单字节前进。
 */
static uint8_t qwen_subtitle_sentence_char_len(const char *text,
                                               uint16_t text_len,
                                               uint16_t pos)
{
    uint8_t first;

    if ((text == NULL) || (pos >= text_len)) {
        return 0U;
    }

    first = (uint8_t)text[pos];
    if (first < 0x80U) {
        return 1U;
    }
    if (((first & 0xE0U) == 0xC0U) && ((uint16_t)(pos + 1U) < text_len)) {
        return 2U;
    }
    if (((first & 0xF0U) == 0xE0U) && ((uint16_t)(pos + 2U) < text_len)) {
        return 3U;
    }
    if (((first & 0xF8U) == 0xF0U) && ((uint16_t)(pos + 3U) < text_len)) {
        return 4U;
    }

    return 1U;
}

/**
 * @brief 在边界内匹配一个指定的三字节 UTF-8 符号。
 * @param[in] text 借用的文字缓冲。
 * @param[in] text_len 可读字节长度。
 * @param[in] pos 待匹配位置。
 * @param[in] bytes 三个目标字节。
 * @return 完整匹配为 1，否则为 0。
 */
static uint8_t qwen_subtitle_sentence_match_utf8(const char *text,
                                                 uint16_t text_len,
                                                 uint16_t pos,
                                                 const uint8_t bytes[3])
{
    return (uint8_t)(((uint16_t)(pos + 2U) < text_len) &&
                     ((uint8_t)text[pos] == bytes[0]) &&
                     ((uint8_t)text[pos + 1U] == bytes[1]) &&
                     ((uint8_t)text[pos + 2U] == bytes[2]));
}

/**
 * @brief 识别实现支持的中英文句尾标点。
 * @param[in] text 借用文字，pos 处须可读。
 * @param[in] text_len 可读长度。
 * @param[in] pos 当前字符位置。
 * @return 当前位置是句尾标点返回 1，否则为 0。
 */
static uint8_t qwen_subtitle_sentence_is_end(const char *text,
                                              uint16_t text_len,
                                              uint16_t pos)
{
    static const uint8_t zh_period[3] = {0xE3U, 0x80U, 0x82U};
    static const uint8_t zh_exclaim[3] = {0xEFU, 0xBCU, 0x81U};
    static const uint8_t zh_question[3] = {0xEFU, 0xBCU, 0x9FU};
    static const uint8_t zh_semicolon[3] = {0xEFU, 0xBCU, 0x9BU};
    static const uint8_t ellipsis[3] = {0xE2U, 0x80U, 0xA6U};
    char value;

    value = text[pos];
    if ((value == '.') || (value == '!') || (value == '?') ||
        (value == ';')) {
        return 1U;
    }
    if (qwen_subtitle_sentence_match_utf8(text, text_len, pos,
                                          zh_period) != 0U) {
        return 1U;
    }
    if ((qwen_subtitle_sentence_match_utf8(text, text_len, pos,
                                           zh_exclaim) != 0U) ||
        (qwen_subtitle_sentence_match_utf8(text, text_len, pos,
                                           zh_question) != 0U) ||
        (qwen_subtitle_sentence_match_utf8(text, text_len, pos,
                                           zh_semicolon) != 0U) ||
        (qwen_subtitle_sentence_match_utf8(text, text_len, pos,
                                           ellipsis) != 0U)) {
        return 1U;
    }

    return 0U;
}

/**
 * @brief 判断当前句首数字后的点是否为列表编号点。
 * @param[in] writer 已经包含当前点的单行写入器。
 * @param[in] text 借用的原文。
 * @param[in] text_len 原文字节长度。
 * @param[in] pos 当前点的位置。
 * @return 是编号点返回 1；小数、连续点或非编号返回 0。
 */
static uint8_t qwen_subtitle_sentence_is_numbered_dot(
        const qwen_subtitle_sentence_writer_t *writer,
        const char *text, uint16_t text_len, uint16_t pos)
{
    const char *cursor;
    uint16_t next_pos;
    uint8_t has_digit;

    if ((text[pos] != '.') || (writer == NULL) || (writer->dst == NULL)) {
        return 0U;
    }

    next_pos = (uint16_t)(pos + 1U);
    if (next_pos >= text_len) {
        return 0U;
    }
    if (((text[next_pos] >= '0') && (text[next_pos] <= '9')) ||
        (text[next_pos] == '.')) {
        return 0U;
    }

    cursor = writer->dst;
    while ((*cursor == ' ') || (*cursor == '\t')) {
        cursor++;
    }
    has_digit = 0U;
    while ((*cursor >= '0') && (*cursor <= '9')) {
        has_digit = 1U;
        cursor++;
    }

    return (uint8_t)(((has_digit != 0U) && (*cursor == '.') &&
                      (cursor[1] == '\0')) ? 1U : 0U);
}

/**
 * @brief 检查当前 ASCII 点是否属于相邻连续点。
 * @param[in] text 借用原文，pos 处须可读。
 * @param[in] text_len 可读长度。
 * @param[in] pos 当前字符位置。
 * @return 当前为点且前后任一相邻位置也是点返回 1，否则为 0。
 */
static uint8_t qwen_subtitle_sentence_is_ellipsis_dot(const char *text,
                                                      uint16_t text_len,
                                                      uint16_t pos)
{
    if (text[pos] != '.') {
        return 0U;
    }

    return (uint8_t)(((pos > 0U) && (text[pos - 1U] == '.')) ||
                     (((uint16_t)(pos + 1U) < text_len) &&
                      (text[pos + 1U] == '.')));
}

/**
 * @brief 识别用于过滤纯标点行的空白及 ASCII 符号集合。
 * @param[in] value 待判断字符。
 * @return 属于实现列出的集合返回 1，否则为 0。
 */
static uint8_t qwen_subtitle_sentence_is_ascii_punct(char value)
{
    return (uint8_t)(((value == ' ') || (value == '\t') ||
                      (value == '.') || (value == '!') ||
                      (value == '?') || (value == ';') ||
                      (value == ':') || (value == ',') ||
                      (value == '*')) ? 1U : 0U);
}

/**
 * @brief 识别用于过滤纯标点行的三字节中文标点集合。
 * @param[in] text 借用文字缓冲。
 * @param[in] text_len 可读长度。
 * @param[in] pos 当前字符位置。
 * @param[in] char_len 已推定的字符长度。
 * @return 长度为三且匹配已列符号返回 1，否则为 0。
 */
static uint8_t qwen_subtitle_sentence_is_zh_punct(const char *text,
                                                  uint16_t text_len,
                                                  uint16_t pos,
                                                  uint8_t char_len)
{
    static const uint8_t zh_period[3] = {0xE3U, 0x80U, 0x82U};
    static const uint8_t zh_comma[3] = {0xEFU, 0xBCU, 0x8CU};
    static const uint8_t zh_ideographic_comma[3] = {0xE3U, 0x80U, 0x81U};
    static const uint8_t zh_exclaim[3] = {0xEFU, 0xBCU, 0x81U};
    static const uint8_t zh_question[3] = {0xEFU, 0xBCU, 0x9FU};
    static const uint8_t zh_semicolon[3] = {0xEFU, 0xBCU, 0x9BU};
    static const uint8_t zh_colon[3] = {0xEFU, 0xBCU, 0x9AU};
    static const uint8_t ellipsis[3] = {0xE2U, 0x80U, 0xA6U};

    if (char_len != 3U) {
        return 0U;
    }

    return (uint8_t)((qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, zh_period) != 0U) ||
                     (qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, zh_comma) != 0U) ||
                     (qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, zh_ideographic_comma) != 0U) ||
                     (qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, zh_exclaim) != 0U) ||
                     (qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, zh_question) != 0U) ||
                     (qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, zh_semicolon) != 0U) ||
                     (qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, zh_colon) != 0U) ||
                     (qwen_subtitle_sentence_match_utf8(
                         text, text_len, pos, ellipsis) != 0U));
}

/**
 * @brief 检查文字中是否有超出空白和既定标点集合的可见内容。
 * @param[in] text NUL 结尾的借用单行文字。
 * @return 存在内容返回 1；空输入或纯标点返回 0。
 */
static uint8_t qwen_subtitle_sentence_has_visible(const char *text)
{
    uint16_t text_len;
    uint16_t pos;
    uint8_t char_len;

    if (text == NULL) {
        return 0U;
    }

    text_len = (uint16_t)strlen(text);
    pos = 0U;
    while ((pos < text_len) && (text[pos] != '\0')) {
        char_len = qwen_subtitle_sentence_char_len(text, text_len, pos);
        if (char_len == 0U) {
            break;
        }
        if ((char_len == 1U) &&
            (qwen_subtitle_sentence_is_ascii_punct(text[pos]) != 0U)) {
            pos = (uint16_t)(pos + char_len);
            continue;
        }
        if (qwen_subtitle_sentence_is_zh_punct(
                text, text_len, pos, char_len) == 0U) {
            return 1U;
        }
        pos = (uint16_t)(pos + char_len);
    }

    return 0U;
}

/**
 * @brief 只保存有可见内容的行，达到固定行数时标记裁剪。
 * @param[out] sentence_text 固定尺寸的输出行数组。
 * @param[in,out] sentence_count 当前已保存行数，成功后加一。
 * @param[in] sentence NUL 结尾的借用工作行。
 * @param[in,out] is_clipped 可为空，行容量耗尽时置位。
 */
static void qwen_subtitle_sentence_store(
        char sentence_text[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX],
        uint8_t *sentence_count, const char *sentence, uint8_t *is_clipped)
{
    if ((sentence_text == NULL) || (sentence_count == NULL) ||
        (sentence == NULL) ||
        (qwen_subtitle_sentence_has_visible(sentence) == 0U)) {
        return;
    }

    if (*sentence_count >= QWEN_SUBTITLE_SENTENCE_MAX) {
        if (is_clipped != NULL) {
            *is_clipped = 1U;
        }
        return;
    }

    strncpy(sentence_text[*sentence_count], sentence,
            QWEN_SUBTITLE_SENTENCE_TEXT_MAX - 1U);
    sentence_text[*sentence_count][QWEN_SUBTITLE_SENTENCE_TEXT_MAX - 1U] = '\0';
    *sentence_count = (uint8_t)(*sentence_count + 1U);
}

/**
 * @brief 提交当前工作行、合并裁剪状态，并清空工作写入器供下一行使用。
 * @param[in,out] writer 单行工作状态。
 * @param[out] sentence_text 接收有效工作行的输出数组。
 * @param[in,out] sentence_count 已保存句子数。
 * @param[in,out] is_clipped 可为空，累计裁剪标志。
 */
static void qwen_subtitle_sentence_flush(
        qwen_subtitle_sentence_writer_t *writer,
        char sentence_text[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX],
        uint8_t *sentence_count, uint8_t *is_clipped)
{
    if ((writer == NULL) || (writer->dst == NULL)) {
        return;
    }

    writer->dst[writer->dst_len] = '\0';
    if ((is_clipped != NULL) && (writer->is_clipped != 0U)) {
        *is_clipped = 1U;
    }
    qwen_subtitle_sentence_store(sentence_text, sentence_count,
                                 writer->dst, is_clipped);
    qwen_subtitle_sentence_writer_init(writer, writer->dst, writer->dst_size);
}

/**
 * @brief 处理一个字符的换行、自动折行及句尾提交，保留编号和省略号。
 * @param[in,out] split 分句描述及其可变输出引用。
 * @param[in] pos 当前输入字节位置。
 * @return 继续处理时返回已消费字节数；无字符或输出无法继续时返回零。
 */
static uint8_t qwen_subtitle_sentence_process_char(
        const qwen_subtitle_sentence_split_t *split, uint16_t pos)
{
    uint8_t char_len;

    char_len = qwen_subtitle_sentence_char_len(split->text,
                                               split->text_len,
                                               pos);
    if (char_len == 0U) {
        return 0U;
    }

    if (split->text[pos] == '\n') {
        qwen_subtitle_sentence_flush(split->writer,
                                     split->sentence_text,
                                     split->sentence_count,
                                     split->is_clipped);
        return char_len;
    }

    if (qwen_subtitle_sentence_append_wrapped(split,
                                              &split->text[pos],
                                              char_len) == 0U) {
        return 0U;
    }
    if ((qwen_subtitle_sentence_is_end(split->text,
                                       split->text_len,
                                       pos) != 0U) &&
        (qwen_subtitle_sentence_is_numbered_dot(
            split->writer, split->text, split->text_len, pos) == 0U) &&
        (qwen_subtitle_sentence_is_ellipsis_dot(
            split->text, split->text_len, pos) == 0U)) {
        qwen_subtitle_sentence_flush(split->writer,
                                     split->sentence_text,
                                     split->sentence_count,
                                     split->is_clipped);
    }

    return char_len;
}

/**
 * @brief 提交末行，没有可见句子时保存既有等待占位句。
 * @param[in,out] split 分句描述及其工作写入器和输出引用。
 */
static void qwen_subtitle_sentence_finish(
        const qwen_subtitle_sentence_split_t *split)
{
    qwen_subtitle_sentence_flush(split->writer,
                                 split->sentence_text,
                                 split->sentence_count,
                                 split->is_clipped);
    if (*split->sentence_count == 0U) {
        qwen_subtitle_sentence_store(split->sentence_text,
                                     split->sentence_count,
                                     QWEN_SUBTITLE_WAIT_TEXT,
                                     split->is_clipped);
    }
}

/**
 * @brief 把已清洗的显示文字按标点、换行及单行容量切为有限句子。
 */
uint8_t qwen_subtitle_split_sentences(
    const char *text,
    char sentence_text[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX],
    uint8_t *sentence_count)
{
    qwen_subtitle_sentence_writer_t writer;
    qwen_subtitle_sentence_split_t split;
    char sentence_work[QWEN_SUBTITLE_SENTENCE_TEXT_MAX];
    uint16_t pos;
    uint8_t char_len;
    uint8_t is_clipped;

    if ((sentence_text == NULL) || (sentence_count == NULL)) {
        return 0U;
    }

    *sentence_count = 0U;
    is_clipped = 0U;
    memset(sentence_text, 0, QWEN_SUBTITLE_SENTENCE_MAX *
           QWEN_SUBTITLE_SENTENCE_TEXT_MAX);
    qwen_subtitle_sentence_writer_init(&writer, sentence_work,
                                       sizeof(sentence_work));
    if ((text == NULL) || (text[0] == '\0')) {
        qwen_subtitle_sentence_store(sentence_text, sentence_count,
                                     QWEN_SUBTITLE_WAIT_TEXT, &is_clipped);
        return is_clipped;
    }

    split.text = text;
    split.text_len = (uint16_t)strlen(text);
    split.writer = &writer;
    split.sentence_text = sentence_text;
    split.sentence_count = sentence_count;
    split.is_clipped = &is_clipped;

    pos = 0U;
    while ((pos < split.text_len) && (text[pos] != '\0')) {
        char_len = qwen_subtitle_sentence_process_char(&split, pos);
        if (char_len == 0U) {
            break;
        }
        pos = (uint16_t)(pos + char_len);
    }

    qwen_subtitle_sentence_finish(&split);
    return is_clipped;
}

#endif /* LVGL 的 Cipher 或 Product UI 条件。 */
