/**
 * @file qwen_subtitle_sentence.h
 * @brief Fixed-buffer sentence splitting for Qwen subtitles.
 */
#ifndef __QWEN_SUBTITLE_SENTENCE_H__
#define __QWEN_SUBTITLE_SENTENCE_H__

#include "modules/applications/qwen_subtitle/qwen_subtitle_text.h"

#include <stdint.h>

uint8_t qwen_subtitle_split_sentences(
    const char *text,
    char sentence_text[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX],
    uint8_t *sentence_count);

#endif /* __QWEN_SUBTITLE_SENTENCE_H__ */
