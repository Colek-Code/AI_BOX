/** @file product_ui_pomodoro_port.h @brief 番茄钟的设备／主机存储与调度端口，不拥有 LVGL 对象。 */
#ifndef PRODUCT_UI_POMODORO_PORT_H
#define PRODUCT_UI_POMODORO_PORT_H
#include <stdint.h>
#define PRODUCT_UI_POMODORO_VM_A 27U
#define PRODUCT_UI_POMODORO_VM_B 28U
/**
 * @brief 读取番茄钟共用单调计数。
 * @return 按 uint32_t 回绕的毫秒数。
 */
uint32_t product_ui_pomodoro_clock_ms(void);
/**
 * @brief 读取可信北京时间所属的本地日期键。
 * @return 有效时返回 YYYYMMDD，无可信时间时返回零。
 */
uint32_t product_ui_pomodoro_today(void);
/**
 * @brief 按 A/B 槽读取完整番茄钟记录并规范化缺失／版本状态。
 * @param[in] slot 0 为 A 槽、1 为 B 槽。
 * @param[out] record 调用方完整记录缓冲，失败时内容不可使用。
 * @param[in] size 必须为 PRODUCT_UI_POMODORO_RECORD_SIZE。
 * @return 实际读长或负错误；NOT_FOUND 须有缺失证明，SDK 长度不符映射为 NOT_READY 保护记录。
 */
int product_ui_pomodoro_storage_read(uint8_t slot, uint8_t *record, uint16_t size);
/**
 * @brief 写入一个完整番茄钟 A/B 记录。
 * @param[in] slot 0 为 A 槽、1 为 B 槽。
 * @param[in] record 调用期间借用的完整记录。
 * @param[in] size 必须为 PRODUCT_UI_POMODORO_RECORD_SIZE。
 * @return 实际写入长度或负错误，参数无效返回 INVALID_ARG。
 * @note 服务仍须完整读回确认。
 */
int product_ui_pomodoro_storage_write(uint8_t slot, const uint8_t *record, uint16_t size);
/**
 * @brief 在独立 sys_timer 任务建立 100 毫秒周期。
 * @param[in] callback 借用的持久回调，收到 NULL 上下文。
 * @return 定时器编号，零表示创建失败。
 */
uint16_t product_ui_pomodoro_timer_start(void (*callback)(void *));
/**
 * @brief 停止非零番茄钟定时器。
 * @param[in] timer 此前创建的定时器编号。
 */
void product_ui_pomodoro_timer_stop(uint16_t timer);
#endif
