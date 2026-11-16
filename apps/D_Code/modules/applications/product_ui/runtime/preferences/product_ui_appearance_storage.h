/** @file product_ui_appearance_storage.h @brief 设备与主机共用的外观记录存储及单调时钟边界。 */
#ifndef PRODUCT_UI_APPEARANCE_STORAGE_H
#define PRODUCT_UI_APPEARANCE_STORAGE_H
#include <stdint.h>

/**
 * @brief 读取外观持久化记录，不执行 LVGL 操作。
 * @param[out] record 由调用方提供、容量不少于 size 的缓冲区。
 * @param[in] size 最大读取字节数。
 * @return 实际读取的字节数，或底层负错误值；完整性由上层校验。
 */
int product_ui_appearance_storage_read(uint8_t *record, uint16_t size);
/**
 * @brief 写入外观持久化记录，不执行 LVGL 操作。
 * @param[in] record 本次调用借用的记录缓冲区。
 * @param[in] size 本次写入字节数。
 * @return 实际写入的字节数，或底层负错误值；上层仍需读回确认。
 */
int product_ui_appearance_storage_write(const uint8_t *record, uint16_t size);
/**
 * @brief 读取外观请求、后台完成与超时观察共用的单调时钟。
 * @return 按 uint32_t 回绕的毫秒计数。
 */
uint32_t product_ui_appearance_clock_ms(void);
#endif
