/**
 * @file    qwen_wss.h
 * @brief   Qwen WSS 连接管理接口
 * @author  D_Code
 * @date    2026-05-19
 */

#ifndef __QWEN_WSS_H__
#define __QWEN_WSS_H__

#include "framework/d_error.h"
#include <stdint.h>

typedef void (*qwen_wss_disconnect_callback_t)(uint32_t generation);

void qwen_wss_set_disconnect_callback(qwen_wss_disconnect_callback_t callback);

/**
 * @brief 建立 Qwen WSS 连接并启动收发线程
 * @return D_OK 表示成功，其他错误码表示失败
 */
d_err_t qwen_wss_connect(void);

/**
 * @brief 断开 Qwen WSS 连接并释放运行时资源
 */
void qwen_wss_disconnect(void);
/** Worker-only teardown: success includes both transport threads having exited. */
d_err_t qwen_wss_disconnect_checked(void);

/**
 * @brief 查询当前 WSS 是否已连接
 * @return 1 表示已连接，0 表示未连接
 */
int qwen_wss_is_connected(void);
uint32_t qwen_wss_generation(void);

#endif
