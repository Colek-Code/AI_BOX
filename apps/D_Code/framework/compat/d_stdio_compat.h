/**
 * @file d_stdio_compat.h
 * @brief Minimal formatted-output declarations without including stdio.h.
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
extern int printf(const char *format, ...);
extern int snprintf(char *str, unsigned long size, const char *format, ...);
extern int vsnprintf(char *str, unsigned long size, const char *format,
                     __builtin_va_list ap);
#endif

#endif /* D_STDIO_COMPAT_H */
