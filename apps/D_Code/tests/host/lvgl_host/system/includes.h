/** @file includes.h @brief 测试所需的 SDK 类型、定时器及临界区兼容声明；实现由主机端口提供。 */
#ifndef PRODUCT_UI_HOST_SYSTEM_INCLUDES_H
#define PRODUCT_UI_HOST_SYSTEM_INCLUDES_H
#include <assert.h>
#include <stdint.h>
/**
 * @brief 主机 C 库 printf 的兼容前向声明，不在此实现格式化。
 * @param[in] format 主机格式化字符串，原内容不翻译。
 * @return 由主机 C 库返回已输出字符数或负错误值。
 */
int printf(const char *format, ...);
#ifdef PRODUCT_UI_ALARM_HOST_SDK_VM
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
/**
 * @brief 闹钟主机端口的 SDK 定时注册兼容声明，回调由主机时钟推进。
 * @param[in] name 须为 sys_timer，不作为任意线程创建名称。
 * @param[in] context SDK 兼容上下文，当前主机闹钟实现忽略。
 * @param[in] callback 保存至闹钟端口的定时回调，注册期间保持有效。
 * @param[in] milliseconds 须为 100 毫秒，其他周期被本固定夹具拒绝。
 * @return 正常返回主机标识 2；名称、周期不符或注入定时故障时为 0。
 */
uint16_t sys_timer_add_to_task(const char *name, void *context, void (*callback)(void *), uint32_t milliseconds);
/**
 * @brief 撤销所属主机端口保存的定时器回调，不操作设备。
 * @param[in] timer 定时器标识。
 */
void sys_timer_del(uint16_t timer);
#endif
/**
 * @brief 主机计时端口的 SDK 兼容声明。
 * @return 当前主机夹具的毫秒计时值。
 */
uint32_t timer_get_ms(void);
/**
 * @brief 锁住主机全局临界区互斥，允许真实工作线程之间同步。
 */
void product_ui_host_enter_critical(void);
/**
 * @brief 释放调用线程持有的主机全局临界区互斥。
 */
void product_ui_host_exit_critical(void);
#define OS_ENTER_CRITICAL() product_ui_host_enter_critical()
#define OS_EXIT_CRITICAL() product_ui_host_exit_critical()
#endif
