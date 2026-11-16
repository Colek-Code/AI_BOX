/** @file product_ui_trace.h @brief 由编译开关控制的临时交互日志宏，不改变业务动作。 */

#ifndef __PRODUCT_UI_TRACE_H__
#define __PRODUCT_UI_TRACE_H__

#include "config/app_config.h"

/** 仅在宏被定义时展开交互 printf；关闭分支为空，不评估可变参数。 */
#ifdef D_PRODUCT_UI_INTERACTION_TRACE_ENABLE
#define PRODUCT_UI_TRACE(...) printf("[ProductUI-ACTION] " __VA_ARGS__)
#else
#define PRODUCT_UI_TRACE(...)
#endif

#endif /* __PRODUCT_UI_TRACE_H__ */
