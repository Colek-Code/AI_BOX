/** @file d_stdio_compat.h @brief 主机 C 库格式输出前向声明，避免扩大固件头文件依赖。 */
#ifndef D_STDIO_COMPAT_H
#define D_STDIO_COMPAT_H
#include <stddef.h>
#include <stdarg.h>
/**
 * @brief 主机 C 库 printf 的兼容前向声明，不在此实现格式化。
 * @param[in] format 主机格式化字符串，原内容不翻译。
 * @return 由主机 C 库返回已输出字符数或负错误值。
 */
int printf(const char *format, ...);
/**
 * @brief 主机 C 库 snprintf 的兼容前向声明。
 * @param[out] buffer 调用方缓冲，最多写入 capacity 字节。
 * @param[in] capacity 输出缓冲容量，包含终止字符空间。
 * @param[in] format 主机格式化字符串，原内容不翻译。
 * @return 所需字符数，不含终止符；格式化失败返回负值。
 */
int snprintf(char *buffer, size_t capacity, const char *format, ...);
/**
 * @brief 主机 C 库 vsnprintf 的兼容前向声明。
 * @param[out] buffer 调用方缓冲，最多写入 capacity 字节。
 * @param[in] capacity 输出缓冲容量，包含终止字符空间。
 * @param[in] format 主机格式化字符串，原内容不翻译。
 * @param[in] arguments 调用方持有的可变参数游标。
 * @return 所需字符数，不含终止符；格式化失败返回负值。
 */
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments);
#endif
