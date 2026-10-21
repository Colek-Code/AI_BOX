/**
 * @file    d_error.h
 * @brief   D_Code 统一错误码定义
 */

#ifndef D_ERROR_H
#define D_ERROR_H

typedef enum {
    D_OK              =  0,     /* 成功 */
    D_ERR_INVALID_ARG = -1,     /* 参数无效 */
    D_ERR_NO_MEMORY   = -2,     /* 内存不足 */
    D_ERR_TIMEOUT     = -3,     /* 超时 */
    D_ERR_BUSY        = -4,     /* 繁忙 */
    D_ERR_NOT_READY   = -5,     /* 未就绪 */
    D_ERR_IO          = -6,     /* IO 错误 */
    D_ERR_NETWORK     = -7,     /* 网络错误 */
    D_ERR_NOT_FOUND   = -8,     /* 未找到 */
    D_ERR_FULL        = -9,     /* 已满 */
} d_err_t;

#endif /* D_ERROR_H */
