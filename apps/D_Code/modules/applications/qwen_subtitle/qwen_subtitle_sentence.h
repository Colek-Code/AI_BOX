/** @file qwen_subtitle_sentence.h @brief 有限字幕行的分句接口；输入来自已清洗的显示文本。 */
#ifndef __QWEN_SUBTITLE_SENTENCE_H__
#define __QWEN_SUBTITLE_SENTENCE_H__

#include "modules/applications/qwen_subtitle/qwen_subtitle_text.h"

#include <stdint.h>

/**
 * @brief 把已清洗的显示文字按标点、换行及单行容量切为有限句子。
 * @param[in] text 有限 NUL 结尾显示文本；空文本使用既有等待占位句。
 * @param[out] sentence_text 至少 SENTENCE_MAX 行、每行 SENTENCE_TEXT_MAX 字节的输出数组。
 * @param[out] sentence_count 本次有效行数，调用开始清零。
 * @return 达到行数等显示上限返回非零裁剪标志；否则为零。空输出参数也返回零，不是独立成功状态。
 * @note 输入应先经过文本清洗，本层只按首字节推定长度，不独立保证非法 UTF-8 已被修复；纯空白或标点行被过滤。
 */
uint8_t qwen_subtitle_split_sentences(
    const char *text,
    char sentence_text[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX],
    uint8_t *sentence_count);

#endif /* __QWEN_SUBTITLE_SENTENCE_H__ */
