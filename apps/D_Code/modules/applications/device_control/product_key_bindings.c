/** @file product_key_bindings.c
 * @brief 统一产品按键映射与消费策略；业务资源保持归属各自服务。
 * @author D_Code
 * @date 2026-11-12
 */
#include "config/app_config.h"
#include "product_key_bindings.h"
#include "modules/services/key/d_key_service.h"
#include "framework/d_log.h"
#include "event/key_event.h"
#include <stddef.h>
#if D_FEATURE_POWER_CONTROL
#include "modules/services/power/power_ctrl.h"
#endif
#if D_FEATURE_QWEN
#include "modules/integrations/qwen/qwen_service.h"
#endif

#define TAG "PRODUCT_KEYS"
#define PRODUCT_QWEN_ON_POWER (D_BOARD_BOX_ADKEY && D_FEATURE_QWEN && \
                              D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
#define PRODUCT_MUSIC_KEYS_ENABLED (D_BOARD_BOX_ADKEY && D_PRODUCT_MUSIC_ENABLE)
#define PRODUCT_TALK_MUSIC_ENABLED (PRODUCT_MUSIC_KEYS_ENABLED && \
                                    (!D_FEATURE_QWEN || PRODUCT_QWEN_ON_POWER))
#define PRODUCT_VOLUME_KEYS_ENABLED (D_BOARD_BOX_ADKEY && (D_FEATURE_QWEN || D_LOCAL_PROMPT_ENABLE))

#if PRODUCT_MUSIC_KEYS_ENABLED
#include "modules/services/local_music/music_service.h"
#endif
#if PRODUCT_VOLUME_KEYS_ENABLED
#include "modules/services/audio/d_device_volume.h"
#define PRODUCT_VOLUME_STEP_PERCENT 5
#endif

#if D_FEATURE_QWEN
#if !PRODUCT_QWEN_ON_POWER
static uint32_t s_qwen_token;
#endif

/**
 * @brief 将当前模式的会话或重配手势转交 Qwen。
 * @param[in] key SDK 按键值。
 * @param[in] action SDK 按键动作。
 * @param[in] context 借用上下文，当前未使用。
 * @return 键值或双工动作不匹配为 UNHANDLED。已转交但未受理的动作按 REJECTED 返回。
 * @note 按住说话模式的 UP 可以收尾；双工只转交单击和配置的重配手势。
 */
static int product_qwen_key(uint8_t key, uint8_t action, void *context)
{
    int result;
    (void)context;
#if D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX
    if (key != D_QWEN_DUPLEX_KEY) return D_KEY_UNHANDLED;
    if (action != KEY_EVENT_CLICK
#if D_FEATURE_BLE_PROVISION
        && !(key == D_QWEN_WIFI_RECONFIG_KEY && action == D_QWEN_WIFI_RECONFIG_ACTION)
#endif
        ) return D_KEY_UNHANDLED;
#else
    if (key != D_KEY_PTT) return D_KEY_UNHANDLED;
#endif
#if D_FEATURE_POWER_CONTROL
    if (action != KEY_EVENT_UP && power_ctrl_is_shutting_down()) return D_KEY_REJECTED;
#endif
    /* 注册回调不携带驱动类型；公开业务接口保留零值兼容参数。 */
    result = qwen_service_handle_key_event(0U, action, key);
    return result == D_KEY_UNHANDLED ? D_KEY_REJECTED : result;
}
#endif

#if D_FEATURE_POWER_CONTROL || PRODUCT_QWEN_ON_POWER
static uint32_t s_power_token;

/**
 * @brief 先由电源服务处理同一键的关机序列，再分派 BOX 双工短按。
 * @param[in] key SDK 按键值。
 * @param[in] action SDK 按键动作。
 * @param[in] context 借用上下文，当前未使用。
 * @return 长按序列由电源服务消费；单击与三击仅在电源服务未处理时交给 Qwen。
 */
static int product_power_key(uint8_t key, uint8_t action, void *context)
{
    int result = D_KEY_UNHANDLED;
    (void)context;
    if (key != D_POWER_CTRL_KEY) return D_KEY_UNHANDLED;
#if D_FEATURE_POWER_CONTROL
    result = power_ctrl_handle_key_action(action);
    if ((action == KEY_EVENT_LONG || action == KEY_EVENT_HOLD || action == KEY_EVENT_UP) &&
        result == D_KEY_UNHANDLED) return D_KEY_CONSUMED;
#endif
#if PRODUCT_QWEN_ON_POWER
    if (result == D_KEY_UNHANDLED) return product_qwen_key(key, action, context);
#endif
    return result;
}
#endif

#if PRODUCT_MUSIC_KEYS_ENABLED
/**
 * @brief 使用当前介质代次提交本地音乐命令。
 * @param[in] command 切歌或播放暂停命令。
 * @return 音乐服务受理为 CONSUMED，否则为 REJECTED。
 * @note 服务在受理时复核介质和音频占用，由工作任务按当前曲目和断点处理播放暂停。
 */
static int product_music_request(music_command_e command)
{
    music_snapshot_t snapshot;
    uint32_t request_id;

#if D_FEATURE_POWER_CONTROL
    if (power_ctrl_is_shutting_down()) return D_KEY_REJECTED;
#endif
    music_service_get_snapshot(&snapshot);
    return music_service_request(command, snapshot.media_generation, 0U, &request_id) == D_OK
        ? D_KEY_CONSUMED : D_KEY_REJECTED;
}
#endif

#if PRODUCT_TALK_MUSIC_ENABLED
static uint32_t s_talk_music_token;

/**
 * @brief TALK 单击切换本地音乐播放或暂停。
 * @param[in] key SDK 按键值。
 * @param[in] action SDK 按键动作。
 * @param[in] context 借用上下文，当前未使用。
 * @return 单击沿用音乐请求结果，其他手势为 UNHANDLED。
 */
static int product_talk_music_key(uint8_t key, uint8_t action, void *context)
{
    (void)context;
    if (key != D_KEY_PTT) return D_KEY_UNHANDLED;
    if (action == KEY_EVENT_CLICK) return product_music_request(MUSIC_PLAY_PAUSE);
    return D_KEY_UNHANDLED;
}
#endif

#if PRODUCT_MUSIC_KEYS_ENABLED || PRODUCT_VOLUME_KEYS_ENABLED
static uint32_t s_volume_up_token;
static uint32_t s_volume_down_token;

/**
 * @brief 区分上下键的短按切歌和长按音量步进。
 * @param[in] key SDK 音量加或减键值。
 * @param[in] action SDK 按键动作。
 * @param[in] context 借用上下文，当前未使用。
 * @return 已受理或完成长按收尾为 CONSUMED，业务拒绝为 REJECTED，未映射为 UNHANDLED。
 * @note 板级配置让每次短按独立出码；SDK 负责长按保持节拍，松手不新增动作。
 */
static int product_up_down_key(uint8_t key, uint8_t action, void *context)
{
    const uint8_t is_up = (key == D_KEY_VOL_UP);
    (void)context;
    if (!is_up && key != D_KEY_VOL_DOWN) return D_KEY_UNHANDLED;
    if (action == KEY_EVENT_UP) return D_KEY_CONSUMED;

#if PRODUCT_MUSIC_KEYS_ENABLED
    if (action == KEY_EVENT_CLICK) return product_music_request(is_up ? MUSIC_PREVIOUS : MUSIC_NEXT);
#endif
#if PRODUCT_VOLUME_KEYS_ENABLED
    if (action == KEY_EVENT_LONG || action == KEY_EVENT_HOLD) {
        const int8_t step = is_up ? PRODUCT_VOLUME_STEP_PERCENT : -PRODUCT_VOLUME_STEP_PERCENT;
#if D_FEATURE_POWER_CONTROL
        if (power_ctrl_is_shutting_down()) return D_KEY_REJECTED;
#endif
        return d_device_volume_request_adjust(step, NULL) == D_OK ? D_KEY_CONSUMED : D_KEY_REJECTED;
    }
#endif
    return D_KEY_UNHANDLED;
}
#endif

/**
 * @brief 装配产品按键并保留幂等旧绑定；完整合同见 product_key_bindings.h。
 */
d_err_t product_key_bindings_init(void)
{
#if D_FEATURE_POWER_CONTROL || D_FEATURE_QWEN || PRODUCT_MUSIC_KEYS_ENABLED || PRODUCT_VOLUME_KEYS_ENABLED
    const struct {
        uint8_t key;
        d_key_handler_t handler;
        uint32_t *token;
    } bindings[] = {
#if D_FEATURE_POWER_CONTROL || PRODUCT_QWEN_ON_POWER
        {D_POWER_CTRL_KEY, product_power_key, &s_power_token},
#endif
#if D_FEATURE_QWEN && !PRODUCT_QWEN_ON_POWER
        {D_KEY_PTT, product_qwen_key, &s_qwen_token},
#endif
#if PRODUCT_TALK_MUSIC_ENABLED
        {D_KEY_PTT, product_talk_music_key, &s_talk_music_token},
#endif
#if PRODUCT_MUSIC_KEYS_ENABLED || PRODUCT_VOLUME_KEYS_ENABLED
        {D_KEY_VOL_UP, product_up_down_key, &s_volume_up_token},
        {D_KEY_VOL_DOWN, product_up_down_key, &s_volume_down_token},
#endif
    };
    uint8_t acquired[sizeof bindings / sizeof bindings[0]] = {0U};
    size_t index;
    d_err_t result = D_OK;

    for (index = 0U; index < sizeof bindings / sizeof bindings[0]; index++) {
        uint32_t token;
        result = d_key_register(bindings[index].key, bindings[index].handler, NULL, &token);
        if (result != D_OK) break;
        acquired[index] = (token != *bindings[index].token);
        *bindings[index].token = token;
    }

    if (result != D_OK) {
        LOGE(TAG, "binding rejected: key=%u result=%d", (unsigned)bindings[index].key, result);
        /* 只回滚本次新取得的槽，已在此前成功装配的绑定仍有效。 */
        while (index > 0U) {
            index--;
            if (acquired[index] &&
                d_key_unregister(bindings[index].key, *bindings[index].token) == D_OK) {
                *bindings[index].token = 0U;
            }
        }
    }
    return result;
#else
    return D_OK;
#endif
}
