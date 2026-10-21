/**
 * @file    xiaole_wakeup.h
 * @brief   XiaoLe voice wakeup service
 * @author  D_Code
 * @date    2026-06-05
 */

#ifndef XIAOLE_WAKEUP_H
#define XIAOLE_WAKEUP_H

#include "framework/d_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

d_err_t xiaole_wakeup_init(void);
void xiaole_wakeup_deinit(void);
/** Start listening for the caller's session generation. DETECTED carries the
 * same uint32_t value; consumers must reject generations invalidated by stop,
 * maintenance or another session before accepting a new conversation. */
d_err_t xiaole_wakeup_start(uint32_t generation);
d_err_t xiaole_wakeup_stop(void);
/** Background caller only; confirms the queued stop has released the microphone. */
d_err_t xiaole_wakeup_stop_wait(uint32_t timeout_ms);
int xiaole_wakeup_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* XIAOLE_WAKEUP_H */
