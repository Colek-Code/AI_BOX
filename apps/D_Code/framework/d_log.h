/**
 * @file    d_log.h
 * @brief   D_Code 统一日志系统
 * @note    统一日志输出，支持按级别过滤
 *
 * 用法：
 *   #define TAG "AI_SVC"
 *   LOGI(TAG, "AI回复: %s (emotion=%d)", reply, emotion);
 *   输出：[I][AI_SVC] AI回复: xxx (emotion=3)
 */

#ifndef D_LOG_H
#define D_LOG_H

#include "framework/compat/d_stdio_compat.h"

//*********************************************************************************//
//                              日志级别                                            //
//*********************************************************************************//

typedef enum {
    LOG_LEVEL_ERROR = 0,    /* 致命错误 */
    LOG_LEVEL_WARN,         /* 警告 */
    LOG_LEVEL_INFO,         /* 关键流程 */
    LOG_LEVEL_DEBUG,        /* 调试信息 */
    LOG_LEVEL_VERBOSE,      /* 详细追踪 */
} d_log_level_t;

//*********************************************************************************//
//                              全局日志级别                                        //
//*********************************************************************************//

/* 在 config/runtime_config.h 中配置，默认 DEBUG */
#ifndef D_LOG_LEVEL
#define D_LOG_LEVEL  LOG_LEVEL_DEBUG
#endif

//*********************************************************************************//
//                              日志宏                                              //
//*********************************************************************************//

#define LOGE(tag, fmt, ...)  do { if (D_LOG_LEVEL >= LOG_LEVEL_ERROR)   printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define LOGW(tag, fmt, ...)  do { if (D_LOG_LEVEL >= LOG_LEVEL_WARN)    printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define LOGI(tag, fmt, ...)  do { if (D_LOG_LEVEL >= LOG_LEVEL_INFO)    printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define LOGD(tag, fmt, ...)  do { if (D_LOG_LEVEL >= LOG_LEVEL_DEBUG)   printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define LOGV(tag, fmt, ...)  do { if (D_LOG_LEVEL >= LOG_LEVEL_VERBOSE) printf("[V][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)

#endif /* D_LOG_H */
