/** @file sys_time.h @brief 主机夹具使用的 SDK 毫秒计时兼容声明。 */
#ifndef PRODUCT_UI_HOST_SYS_TIME_H
#define PRODUCT_UI_HOST_SYS_TIME_H
#include <stdint.h>
/**
 * @brief 主机计时端口的 SDK 兼容声明。
 * @return 当前主机夹具的毫秒计时值。
 */
uint32_t timer_get_ms(void);
#endif
