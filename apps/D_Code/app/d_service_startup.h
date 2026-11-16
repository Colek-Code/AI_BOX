/**
 * @file d_service_startup.h
 * @brief 声明服务顺序启动入口及分级失败摘要，属于启动层。
 */

#ifndef D_SERVICE_STARTUP_H
#define D_SERVICE_STARTUP_H

#include <stdint.h>

#include "framework/d_error.h"

typedef struct {
    uint16_t started_count;          /**< 本次返回 D_OK 的初始化项数。 */
    uint16_t required_failure_count; /**< 必需项失败数量，不能据此假定整体就绪。 */
    uint16_t optional_failure_count; /**< 可选项失败数量，用于报告能力降级。 */
    d_err_t first_required_error;    /**< 第一个必需项错误；无失败时为 D_OK。 */
    d_err_t first_optional_error;    /**< 第一个可选项错误；无失败时为 D_OK。 */
} d_service_start_result_t;

/**
 * @brief 按静态清单依次初始化服务，并区分必需与可选失败。
 * @return 本次成功数量、分级失败数量及各级首个错误；无失败的首错字段为 D_OK。
 * @note 由启动线程调用；不会因为某个服务初始化失败而跳过清单中的后续项，也不回滚已启动的服务。
 * @note 此函数不保证所有可选能力就绪；调用方须结合必需失败及实际能力状态判断。
 */
d_service_start_result_t d_service_start_all(void);

#endif /* D_SERVICE_STARTUP_H */
