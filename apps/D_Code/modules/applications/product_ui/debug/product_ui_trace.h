/**
 * @file product_ui_trace.h
 * @brief Temporary bounded Product UI interaction diagnostics.
 */

#ifndef __PRODUCT_UI_TRACE_H__
#define __PRODUCT_UI_TRACE_H__

#include "config/app_config.h"

#ifdef D_PRODUCT_UI_INTERACTION_TRACE_ENABLE
#define PRODUCT_UI_TRACE(...) printf("[ProductUI-ACTION] " __VA_ARGS__)
#else
#define PRODUCT_UI_TRACE(...)
#endif

#endif /* __PRODUCT_UI_TRACE_H__ */
