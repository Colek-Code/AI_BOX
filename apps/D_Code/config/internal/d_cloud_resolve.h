/**
 * @file d_cloud_resolve.h
 * @brief Resolve the selected Qwen owner before platform credentials are included.
 * @author D_Code
 * @date 2026-09-22
 */
#ifndef D_CLOUD_RESOLVE_H
#define D_CLOUD_RESOLVE_H

#if D_FEATURE_QWEN
#if D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED
/* Qwen owns its Alibaba HTTP/WSS path; no bootstrap on the device platform. */
#define D_DEVICE_CLOUD_ENABLE 0
#elif D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED
#if !D_FEATURE_WEBSOCKET
#error "Qwen 半托管需要自有平台首配，请开启 D_FEATURE_WEBSOCKET"
#endif
#define D_DEVICE_CLOUD_ENABLE 1
#else
#error "D_QWEN_AUTH_MODE 无效：请选择 FULL_MANAGED 或 SEMI_MANAGED"
#endif
#else
/* Legacy applications can still use the device platform without Qwen. */
#define D_DEVICE_CLOUD_ENABLE D_FEATURE_WEBSOCKET
#endif

#endif /* D_CLOUD_RESOLVE_H */
