/** @file product_ui_media_control.c @brief 媒体命令绑定与控制中心投影，能力及播放状态从真实服务快照取得。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/media/product_ui_media_control.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include <stddef.h>
#if D_PRODUCT_MUSIC_ENABLE
#include "modules/services/local_music/music_service.h"
#include "modules/applications/product_ui/features/media/product_ui_media_runtime.h"
#endif

#if D_PRODUCT_MUSIC_ENABLE
/**
 * @brief 把具名传输命令映射为基于当前媒体代次的音乐请求。
 * @param[in] command 上一首、下一首或播放暂停命令。
 * @param[in] payload 兼容回调载荷，当前忽略。
 * @param[in] size 兼容回调载荷长度，当前忽略。
 * @param[in] context 注册上下文，当前未使用。
 * @return 非法命令返回 INVALID_ARG，其余原样返回音乐受理结果。
 * @note 成功只是请求受理，控制中心仍须观察音乐服务状态。
 */
static int product_ui_media_command(uint16_t command, const void *payload,
                                    uint16_t size, void *context)
{
    music_snapshot_t state;
    music_command_e operation;
    uint32_t request;
    (void)payload; (void)size; (void)context;
    if (command == PRODUCT_UI_MEDIA_CMD_PREVIOUS) operation = MUSIC_PREVIOUS;
    else if (command == PRODUCT_UI_MEDIA_CMD_NEXT) operation = MUSIC_NEXT;
    else if (command == PRODUCT_UI_MEDIA_CMD_TOGGLE_PLAY_PAUSE) operation = MUSIC_TOGGLE;
    else return D_ERR_INVALID_ARG;
    music_service_get_snapshot(&state);
    return music_service_request(operation, state.media_generation, 0U, &request);
}
#endif

/**
 * @brief 在 LVGL 上下文刷新媒体及共享存储服务的状态和能力。
 */
void product_ui_media_control_sync(void)
{
#if D_PRODUCT_MUSIC_ENABLE
    music_snapshot_t music;
    uint32_t mask = 0U;
    music_service_get_snapshot(&music);
    music.ready = music.ready && product_ui_media_runtime_ready();
    if (music.ready && music.mounted && music.count && music.current_index != MUSIC_INDEX_NONE && !music.held)
        product_ui_media_publish_valid_mask(music.state == MUSIC_PLAYING, &mask);
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_MEDIA,
        music.ready ? PRODUCT_UI_SERVICE_READY : PRODUCT_UI_SERVICE_UNAVAILABLE, mask, music.error);
    /* 共享 SD 在线不代表图库自己的浏览队列已经填充。 */
    (void)product_ui_service_publish(PRODUCT_UI_SERVICE_STORAGE,
        music.mounted ? PRODUCT_UI_SERVICE_READY : PRODUCT_UI_SERVICE_UNAVAILABLE, 0U, music.error);
#endif
}

/**
 * @brief 本地音乐功能启用时绑定媒体命令处理器。
 */
int product_ui_media_control_try_register(void)
{
#if D_PRODUCT_MUSIC_ENABLE
    return product_ui_service_register_command(PRODUCT_UI_SERVICE_MEDIA, product_ui_media_command, NULL);
#else
    return 0;
#endif
}

/**
 * @brief 把服务就绪、回调和能力位映射成控制中心的操作与图标状态。
 */
void product_ui_media_cc_project(
    const product_ui_media_service_view_t *service,
    product_ui_media_cc_view_t *out)
{
    uint8_t ready;

    if (out == NULL) {
        return;
    }
    out->transport_enabled = 0U;
    out->show_pause_icon = 0U;
    out->is_unavailable = 1U;
    if (service == NULL) {
        return;
    }
    ready = (uint8_t)((service->service_ready != 0U) &&
                      (service->command_available != 0U));
    if (ready == 0U) {
        return;
    }
    out->is_unavailable = 0U;
    out->transport_enabled = (service->valid_mask & PRODUCT_UI_MEDIA_CAP_TRANSPORT) != 0U;
    out->show_pause_icon =
        (uint8_t)((service->valid_mask & PRODUCT_UI_MEDIA_STATE_PLAYING) != 0U);
}

/**
 * @brief 生成媒体传输能力及播放状态位组合。
 */
void product_ui_media_publish_valid_mask(
    uint8_t is_playing, uint32_t *valid_mask)
{
    if (valid_mask == NULL) {
        return;
    }
    *valid_mask = PRODUCT_UI_MEDIA_CAP_TRANSPORT;
    if (is_playing != 0U) {
        *valid_mask |= PRODUCT_UI_MEDIA_STATE_PLAYING;
    }
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
