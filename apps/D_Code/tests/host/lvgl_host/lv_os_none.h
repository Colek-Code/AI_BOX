/** @file lv_os_none.h @brief 主机 LV_OS_NONE 的最小句柄类型，UI 锁标记不是目标 RTOS 同步对象。 */
#ifndef PRODUCT_UI_HOST_OS_NONE_H
#define PRODUCT_UI_HOST_OS_NONE_H

#include <stdint.h>
typedef uint8_t lv_thread_t;
typedef uint8_t lv_mutex_t;
typedef uint8_t lv_thread_sync_t;

#endif
