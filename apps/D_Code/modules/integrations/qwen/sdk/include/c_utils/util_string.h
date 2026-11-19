/*
 * Copyright 2025 Alibaba Group Holding Ltd.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __UTIL_STRING_H__
#define __UTIL_STRING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"

#define C_UTIL_TIMESTAMP_MS_LEN         (13)

// 获取SDK版本号
const char* util_get_sdk_version(void);

//字符串copy，需要使用util_free释放指针
char *util_strdup(const char *s);

//同上
char *util_strndup(const char *s, size_t n);

char *http_escape(char *str_in);

/**
 * @brief 原地过滤字符串中的 emoji 字符
 *
 * 支持 UTF-8 编码，过滤如下 Unicode 范围：
 *   - 0x2600~0x27BF  Miscellaneous Symbols（含 ✨）
 *   - 0x2B00~0x2BFF  Miscellaneous Symbols and Arrows
 *   - 0x1F000~0x1FFFF Emoticons / Supplemental Symbols（含 😊）
 *
 * @param str 要过滤的字符串，原地修改
 */
void util_strip_emoji(char *str);

#ifdef __cplusplus
}
#endif

#endif
