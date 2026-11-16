/** @file qwen_factory.c @brief 杰理工厂工具用户选项适配，不执行云端 I/O。 */
#include "config/app_config.h"
#if D_QWEN_FACTORY_TEST_BUILD
#include "qwen_factory.h"
#include "qwen_activation.h"
#include "qwen_access.h"
#include "product_main.h"
#include "framework/d_log.h"
#include <string.h>

/**
 * @brief 提供工厂工具要求的产品端口初始化回调。
 * @note 该固件仅暴露用户选项，不需要样例硬件端口状态，当前回调无操作。
 */
void product_port_init(void)
{
}

/**
 * @brief 只接收一个 text 类型的 qwenActivationCode 选项并写入回读。
 * @param[in] options 原厂 JSON 数组，调用期间借用。
 * @return 参数不符返回 ERR_PARAMS；受检存盘成功返回 ERR_NULL；其余返回 ERR_DEV_FAULT。
 * @note 原厂传输从 msg_handle_task 调用，非中断上下文；该配置不链接样例 product_port.c。
 */
u8 product_write_options(json_object *options)
{
    json_object *entry, *key, *value, *type;
    d_err_t result;
    if (!options || json_object_get_type(options) != json_type_array ||
        json_object_array_length(options) != 1U) return ERR_PARAMS;
    entry = json_object_array_get_idx(options, 0);
    if (!entry || json_object_get_type(entry) != json_type_object) return ERR_PARAMS;
    key = json_object_object_get(entry, "key");
    value = json_object_object_get(entry, "value");
    type = json_object_object_get(entry, "type");
    if (!key || !value || !type || json_object_get_type(key) != json_type_string ||
        json_object_get_type(value) != json_type_string || json_object_get_type(type) != json_type_string ||
        strcmp(json_object_get_string(key), "qwenActivationCode") != 0 ||
        strcmp(json_object_get_string(type), "text") != 0) return ERR_PARAMS;
    if (!qwen_auth_code_valid(json_object_get_string(value))) return ERR_PARAMS;
    result = qwen_activation_factory_write(json_object_get_string(value));
    LOGI("QWEN_FACTORY", "activation code write/readback result=%d", result);
    return result == D_OK ? ERR_NULL : ERR_DEV_FAULT;
}

/**
 * @brief 初始化工厂固件激活码写入服务；完整合同见 qwen_factory.h。
 */
void qwen_factory_start(void)
{
    LOGI("QWEN_FACTORY", "factory image: USB CDC, activation code only");
    if (qwen_access_configure_product() != D_OK || qwen_activation_init() != D_OK) {
        LOGE("QWEN_FACTORY", "credential storage unavailable");
        return;
    }
    (void)product_main();
}
#endif
