/**
 * @file d_cloud_resolve.h
 * @brief 校验 Qwen 鉴权的所属模块，并解析激活功能依赖。
 * @author D_Code
 * @date 2026-09-22
 */
#ifndef D_CLOUD_RESOLVE_H
#define D_CLOUD_RESOLVE_H

#if D_FEATURE_QWEN && (D_QWEN_AUTH_MODE != D_QWEN_AUTH_MODE_FULL_MANAGED) && \
    (D_QWEN_AUTH_MODE != D_QWEN_AUTH_MODE_SEMI_MANAGED)
#error "D_QWEN_AUTH_MODE 无效：请选择 FULL_MANAGED 或 SEMI_MANAGED"
#endif

#if D_FEATURE_QWEN && D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED
#define D_QWEN_ACTIVATION_ENABLE 1
#else
#define D_QWEN_ACTIVATION_ENABLE 0
#endif

/* 仅在存在实际消费者时引入对应硬件能力。 */
#if D_DEBUG_AUTH_CODE_READBACK || (D_FEATURE_QWEN && D_QWEN_DEVICE_NAME_USE_FACTORY) || (D_QWEN_ACTIVATION_ENABLE && \
    D_QWEN_ACTIVATION_SOURCE == D_QWEN_ACTIVATION_SOURCE_PROGRAMMER)
#define D_AUTH_CODE_READ_ENABLE 1
#if D_BOARD_TARGET != D_BOARD_COPY_GC9307_HYN
#error "Flash authentication-code reader is qualified only for the 16 MB GC9307 board"
#endif
#else
#define D_AUTH_CODE_READ_ENABLE 0
#endif

#endif /* D_CLOUD_RESOLVE_H */
