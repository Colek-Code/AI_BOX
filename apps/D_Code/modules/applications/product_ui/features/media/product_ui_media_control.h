/** @file product_ui_media_control.h @brief 媒体命令绑定与控制中心投影，能力及播放状态从真实服务快照取得。 */

#ifndef PRODUCT_UI_MEDIA_CONTROL_H
#define PRODUCT_UI_MEDIA_CONTROL_H

#include "config/app_config.h"
#include <stdint.h>

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

/**
 * @brief 本地音乐功能启用时绑定媒体命令处理器。
 * @return 注册成功或功能关闭而跳过返回 0；注册失败返回非零。
 * @note 绑定不代表音乐运行时或媒体卡已经就绪，实际 UI 可用性由后续快照投影决定。
 */
int product_ui_media_control_try_register(void);
/**
 * @brief 在 LVGL 上下文刷新媒体及共享存储服务的状态和能力。
 * @note 不依赖页面或控制中心是否打开；存储在线不代表图库拥有可浏览队列。
 */
void product_ui_media_control_sync(void);

typedef struct {
    uint8_t service_ready;
    uint8_t command_available;
    uint32_t valid_mask;
} product_ui_media_service_view_t;

typedef struct {
    uint8_t transport_enabled;
    uint8_t show_pause_icon;
    uint8_t is_unavailable;
} product_ui_media_cc_view_t;

/**
 * @brief 把服务就绪、回调和能力位映射成控制中心的操作与图标状态。
 * @param[in] service 借用的服务投影；NULL 视为不可用。
 * @param[out] out 接收控制中心值投影；NULL 时忽略。
 * @note READY 加回调只解除通用不可用状态，传输使能仍看 CAP_TRANSPORT；暂停图标只看 STATE_PLAYING。
 */
void product_ui_media_cc_project(
    const product_ui_media_service_view_t *service,
    product_ui_media_cc_view_t *out);
/**
 * @brief 生成媒体传输能力及播放状态位组合。
 * @param[in] is_playing 非零表示当前正在播放，暂停不撤销传输能力。
 * @param[out] valid_mask 接收新掩码，NULL 时忽略。
 */
void product_ui_media_publish_valid_mask(
    uint8_t is_playing, uint32_t *valid_mask);

#endif
#endif /* PRODUCT_UI_MEDIA_CONTROL_H */
