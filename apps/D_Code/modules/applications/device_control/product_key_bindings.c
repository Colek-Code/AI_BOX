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

#if D_FEATURE_POWER_CONTROL
static uint32_t s_power_token;

/**
 * @brief 把配置的电源键动作转交电源服务，并保留系统长按序列消费策略。
 * @param[in] key SDK 按键值。
 * @param[in] action SDK 按键动作。
 * @param[in] context 注册时传入的借用上下文，当前未使用。
 * @return 按键不匹配为 UNHANDLED；LONG、HOLD、UP 即使业务未处理也被消费，其他动作沿用业务返回。
 * @note CLICK、TRIPLE 只触达电源服务已有跟踪复位，不新增产品动作。
 */
static int product_power_key(uint8_t key, uint8_t action, void *context)
{
    int result;
    (void)context;
    if (key != D_POWER_CTRL_KEY) return D_KEY_UNHANDLED;
    /* CLICK/TRIPLE 仍转给所有者已有的跟踪复位，不新增产品动作；
     * LONG/HOLD/UP 属于系统电源键序列。 */
    result = power_ctrl_handle_key_action(action);
    if ((action == KEY_EVENT_LONG || action == KEY_EVENT_HOLD || action == KEY_EVENT_UP) &&
        result == D_KEY_UNHANDLED) return D_KEY_CONSUMED;
    return result;
}
#endif

#if D_FEATURE_QWEN
static uint32_t s_qwen_token;

/**
 * @brief 将符合当前模式的说话键动作转交 Qwen，保留关机及松手边界。
 * @param[in] key SDK 按键值。
 * @param[in] action SDK 按键动作。
 * @param[in] context 借用上下文，当前未使用。
 * @return 按键或双工动作不匹配为 UNHANDLED；关机中非 UP 动作被拒绝；转交后业务未处理也按 REJECTED 返回。
 * @note UP 可释放已经取得的按住说话序列，不能被改成 CLICK。注册回调没有驱动类型参数，兼容参数沿用零值，不伪造 SDK 事件。
 */
static int product_qwen_key(uint8_t key, uint8_t action, void *context)
{
    int result;
    (void)context;
    if (key != D_KEY_PTT) return D_KEY_UNHANDLED;
#if D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX
    if (action != KEY_EVENT_CLICK
#if D_FEATURE_BLE_PROVISION
        && !(key == D_QWEN_WIFI_RECONFIG_KEY && action == D_QWEN_WIFI_RECONFIG_ACTION)
#endif
        ) return D_KEY_UNHANDLED;
#endif
#if D_FEATURE_POWER_CONTROL
    /* UP 可归还已持有的按住说话序列，不能转换成 CLICK。 */
    if (action != KEY_EVENT_UP && power_ctrl_is_shutting_down()) return D_KEY_REJECTED;
#endif
    /* 注册回调不携带驱动类型；公开业务接口当前忽略该兼容参数，
     * 继续传零，不在产品层伪造 SDK 事件。 */
    result = qwen_service_handle_key_event(0U, action, key);
    return result == D_KEY_UNHANDLED ? D_KEY_REJECTED : result;
}
#endif

/**
 * @brief 装配产品按键并保留幂等旧绑定；完整合同见 product_key_bindings.h。
 */
d_err_t product_key_bindings_init(void)
{
    d_err_t result = D_OK;
#if D_FEATURE_POWER_CONTROL
    const uint32_t previous_power = s_power_token;
    result = d_key_register(D_POWER_CTRL_KEY, product_power_key, NULL, &s_power_token);
    if (result != D_OK) {
        /* 注册失败会清零输出，此处仍须保留原已持有的凭据。 */
        s_power_token = previous_power;
        LOGE(TAG, "power binding rejected: key=%u result=%d", (unsigned)D_POWER_CTRL_KEY, result);
        return result;
    }
#endif
#if D_FEATURE_QWEN
    const uint32_t previous_qwen = s_qwen_token;
    result = d_key_register(D_KEY_PTT, product_qwen_key, NULL, &s_qwen_token);
    if (result != D_OK) {
        s_qwen_token = previous_qwen;
#if D_FEATURE_POWER_CONTROL
        /* 只回滚本次启动调用新取得的绑定；其他项失败不能作为
         * 删除此前幂等旧绑定的理由。 */
        if (s_power_token != previous_power &&
            d_key_unregister(D_POWER_CTRL_KEY, s_power_token) == D_OK) s_power_token = 0U;
#endif
        LOGE(TAG, "Qwen binding rejected: key=%u result=%d", (unsigned)D_KEY_PTT, result);
        return result;
    }
#endif
    return result;
}
