/**
 * @file d_stdio_compat.h
 * @brief 提供避免包含 stdio.h 的最小格式化输出声明；函数实现及返回约定归 SDK 所有。
 */

#ifndef D_STDIO_COMPAT_H
#define D_STDIO_COMPAT_H

#ifdef __has_include
#if __has_include("system/generic/printf.h")
#include "system/generic/printf.h"
#define D_STDIO_COMPAT_HAS_SDK_PRINTF 1
#endif
#endif

#ifndef D_STDIO_COMPAT_HAS_SDK_PRINTF
/**
 * @note 以下仅补充 SDK 格式化输出函数的 ABI 声明，不提供自研实现。
 * 缓冲区、格式串、可变参数及返回值须遵循当前 SDK 的实际合同；不据此承诺完整标准库兼容。
 */
extern int printf(const char *format, ...);
extern int snprintf(char *str, unsigned long size, const char *format, ...);
extern int vsnprintf(char *str, unsigned long size, const char *format,
                     __builtin_va_list ap);
#endif

#endif /* D_STDIO_COMPAT_H */
