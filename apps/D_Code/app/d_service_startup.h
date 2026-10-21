/**
 * @file d_service_startup.h
 * @brief Ordered startup result for D_Code services.
 */

#ifndef D_SERVICE_STARTUP_H
#define D_SERVICE_STARTUP_H

#include <stdint.h>

#include "framework/d_error.h"

typedef struct {
    uint16_t started_count;
    uint16_t required_failure_count;
    uint16_t optional_failure_count;
    d_err_t first_required_error;
    d_err_t first_optional_error;
} d_service_start_result_t;

d_service_start_result_t d_service_start_all(void);

#endif /* D_SERVICE_STARTUP_H */
