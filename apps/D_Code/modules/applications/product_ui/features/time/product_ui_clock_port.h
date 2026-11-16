/** @file product_ui_clock_port.h @brief 产品软件时钟的 SDK 与主机端口，无 RTC 伪造回退。 */
#ifndef PRODUCT_UI_CLOCK_PORT_H
#define PRODUCT_UI_CLOCK_PORT_H
#include "product_ui_time_domain.h"
/**
 * @brief 读取产品软件时钟共用的单调计数。
 * @return 按 uint32_t 回绕的毫秒数。
 */
uint32_t product_ui_clock_port_ms(void);
/**
 * @brief 读取经本次启动 NTP 确认并经过范围校验的 SDK 软件 UTC。
 * @param[out] utc_seconds 成功时写入 UTC 秒，失败时不修改。
 * @return 可用返回 NONE；空输出或无效来源为 SOURCE_INVALID，未确认返回 NOT_READY，功能关闭返回 NOT_SUPPORTED。
 * @note 首次 NTP 确认后，即使断网也不撤销本次启动的可信标志；读取的仍是 SDK 软件时钟，不证明断电保时。
 */
product_ui_time_unavailable_reason_e product_ui_clock_port_network(int64_t *utc_seconds);
/**
 * @brief 在独立 sys_timer 任务创建 100 毫秒周期回调。
 * @param[in] callback 借用的持久回调，调用时上下文为 NULL。
 * @return SDK 返回的定时器编号；零表示未建立。
 */
uint16_t product_ui_clock_port_start(void (*callback)(void *));
/**
 * @brief 删除非零时钟定时器编号。
 * @param[in] timer 此前创建的定时器编号，零不操作。
 */
void product_ui_clock_port_stop(uint16_t timer);
#endif
