/** @file power_ctrl_appearance.h @brief 产品外观提供者的固定关机阶段接口。 */
#ifndef POWER_CTRL_APPEARANCE_H
#define POWER_CTRL_APPEARANCE_H
#include "framework/d_error.h"
#include <stdint.h>

/**
 * @brief 禁止外观新请求，为关机开始收尾。
 */
void power_ctrl_appearance_begin(void);
/**
 * @brief 限时等待外观提供者空闲。
 * @param[in] timeout_ms 最多等待毫秒数。
 * @return 提供者的实际等待结果；D_OK 表示其空闲条件满足。
 */
d_err_t power_ctrl_appearance_wait_idle(uint32_t timeout_ms);
/**
 * @brief 取消外观关机静默限制。
 */
void power_ctrl_appearance_cancel(void);
#endif
