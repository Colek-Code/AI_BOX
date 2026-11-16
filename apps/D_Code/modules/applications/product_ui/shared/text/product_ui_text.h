/** @file product_ui_text.h @brief 模型与视图共用的有限 UTF-8 拷贝；不输出截断码点。 */

#ifndef __PRODUCT_UI_TEXT_H__
#define __PRODUCT_UI_TEXT_H__

#include <stdint.h>

/**
 * @brief 从 NUL 结尾文本拷贝完整 UTF-8 序列并终止输出。
 * @param[out] output 调用方输出缓冲区；NULL 时不写入。
 * @param[in] capacity 输出总容量，含末尾 NUL。
 * @param[in] text 借用的 NUL 结尾文本；最多检查 UINT16_MAX 字节。
 * @return 实际输出字节数，不含末尾 NUL；无输出空间或输入为空时为零。
 * @note 非法字节按单字节替换为问号；有效序列若空间不足则停止。用于显示清洗，不用于替换原始网络身份。
 */
uint16_t product_ui_utf8_copy(char *output, uint16_t capacity,
                              const char *text);
/**
 * @brief 在输入字节上限内拷贝完整 UTF-8 序列并终止输出。
 * @param[out] output 容量不少于 capacity 的调用方缓冲区。
 * @param[in] capacity 输出总容量，含末尾 NUL。
 * @param[in] text 本次调用借用的输入，可为 NULL。
 * @param[in] text_capacity 可读输入字节上限，允许在上限前遇到 NUL。
 * @return 输出字节数，不含末尾 NUL；容量为零或输出为空返回零。
 * @note 越过输入上限的残缺序列直接停止；非法首字节或续字节按单字节替换为问号。
 */
uint16_t product_ui_utf8_copy_n(char *output, uint16_t capacity,
                                const char *text, uint16_t text_capacity);

#endif /* __PRODUCT_UI_TEXT_H__ */
