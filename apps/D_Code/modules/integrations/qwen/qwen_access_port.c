/** @file qwen_access_port.c @brief 将公开配置菜单和已启用平台能力装配到接入库，不解释库内部凭据。
 * @author D_Code @date 2026-10-28 */
#include "config/app_config.h"
#if D_FEATURE_QWEN
#include "qwen_access.h"
#if D_QWEN_HTTP_BUF_SIZE != QWEN_ACCESS_HTTP_CAPACITY
#error "HTTP capacity is a library ABI setting; rebuild the library and matching header"
#endif
#include "framework/d_log.h"
#include <string.h>
#if D_AUTH_CODE_READ_ENABLE
#include "platform/storage/d_auth_code.h"
/**
 * @brief 只读平台原始认证记录，并映射为接入库记录结果。
 * @param[out] bytes 至少 QWEN_ACCESS_RECORD_MAX 字节的缓冲区，由库回调合同保证。
 * @param[out] length 载荷实际长度；非空有效调用先置零。
 * @return OK 为复制成功；EMPTY、CRC、LENGTH 分别映射空、校验和长度错误，其余失败为 READ。
 * @note 只记录地址、长度及结果，不打印记录正文；返回前清空临时载荷副本。
 */
static qwen_access_record_result_e qwen_access_platform_read(uint8_t *bytes, uint16_t *length)
{
    d_auth_code_value_t value;
    d_auth_code_read_info_t info;
    d_auth_code_result_e result;
    qwen_access_record_result_e mapped = QWEN_ACCESS_RECORD_READ;
    if (!bytes || !length) return mapped;
    *length = 0U;
    result = d_auth_code_read(&value, &info);
    LOGI("QWEN_AUTH", "factory record: address=0x%08x length=%u result=%d",
         (unsigned)info.address, (unsigned)value.length, (int)result);
    if (result == D_AUTH_CODE_OK) {
        if (value.length <= QWEN_ACCESS_RECORD_MAX) {
            memcpy(bytes, value.data, value.length); *length = value.length;
            mapped = QWEN_ACCESS_RECORD_OK;
        } else mapped = QWEN_ACCESS_RECORD_LENGTH;
    } else if (result == D_AUTH_CODE_EMPTY) mapped = QWEN_ACCESS_RECORD_EMPTY;
    else if (result == D_AUTH_CODE_CRC) mapped = QWEN_ACCESS_RECORD_CRC;
    else if (result == D_AUTH_CODE_LENGTH) mapped = QWEN_ACCESS_RECORD_LENGTH;
    memset(&value, 0, sizeof(value));
    return mapped;
}
#endif
#if D_FEATURE_AI_ALARM
#include "qwen_local_tool.h"
/**
 * @brief 在旧传输退出后清理工具事务及 SDK 请求槽归属。
 * @note 由传输重建的既有清理入口调用；不清除 VM 或认证缓存。
 */
static void qwen_access_reset_tools(void)
{
    qwen_local_tool_transport_reset();
    qwen_request_gate_reset();
}
#endif
#if D_QWEN_WSS_DIAG_ENABLE
#include "qwen_wire_diag.h"
#endif

/**
 * @brief 装配菜单选项与平台端口；完整合同见 qwen_access.h。
 */
d_err_t qwen_access_configure_product(void)
{
    static uint8_t configured;
    d_err_t result;
    const qwen_access_options_t options = {
        .abi = QWEN_ACCESS_ABI, .size = sizeof(qwen_access_options_t),
        .auth_mode = D_QWEN_AUTH_MODE, .activation_source = D_QWEN_ACTIVATION_SOURCE,
        .factory_test = D_QWEN_FACTORY_TEST_BUILD,
        .factory_name = D_QWEN_DEVICE_NAME_USE_FACTORY, .mac_name = D_QWEN_DEVICE_NAME_USE_BT_MAC,
        .dialog_mode = D_QWEN_DIALOG_MODE,
        .upstream_format = D_QWEN_US_AUDIO_FORMAT, .downstream_format = D_QWEN_DS_AUDIO_FORMAT,
        .opus_cbr = D_QWEN_DS_OPUS_ENABLE_CBR,
        .tls_required = D_QWEN_HTTP_TLS_VERIFY_MODE,
        .allow_unverified_diagnostic = D_QWEN_HTTP_ALLOW_UNVERIFIED_DIAGNOSTIC,
        .opus_frame_ms = D_QWEN_DS_OPUS_FRAME_MS, .opus_bitrate_kbps = D_QWEN_DS_OPUS_BIT_RATE_KBPS,
        .upstream_rate = D_QWEN_US_SAMPLE_RATE, .downstream_rate = D_QWEN_DS_SAMPLE_RATE,
        .recorder_bytes = D_QWEN_RECORDER_RB_SIZE, .http_timeout_ms = D_QWEN_HTTP_TIMEOUT_MS,
        .fixed_name = D_QWEN_DEVICE_NAME, .voice = D_QWEN_VOICE_ID
    };
    const qwen_access_hooks_t hooks = {
        .abi = QWEN_ACCESS_ABI, .size = sizeof(qwen_access_hooks_t),
#if D_AUTH_CODE_READ_ENABLE
        .read_record = qwen_access_platform_read,
#endif
#if D_FEATURE_AI_ALARM
        .receive = qwen_local_tool_receive, .tx_ready = qwen_local_tool_tx_ready,
        .tx_reserved = qwen_local_tool_tx_reserved, .tx_empty = qwen_local_tool_tx_empty,
        .failed = qwen_local_tool_failed, .tx_frame = qwen_local_tool_tx_frame,
        .tx_sent = qwen_local_tool_tx_sent, .transport_reset = qwen_access_reset_tools,
        .request_begin = qwen_request_begin, .request_submitted = qwen_request_submitted,
        .generate_begin = qwen_request_generate_begin, .generate_end = qwen_request_generate_end,
        .is_response = qwen_tool_is_response_frame,
#if D_QWEN_WSS_DIAG_ENABLE
        .tx_diagnose = qwen_local_tool_tx_diagnose,
#endif
#endif
#if D_QWEN_WSS_DIAG_ENABLE
        .diag_rx = qwen_wire_diag_rx, .diag_tx = qwen_wire_diag_tx,
#endif
    };
    if (configured) return D_OK;
    result = qwen_access_configure(&options, &hooks);
    if (result == D_OK) configured = 1U;
    return result;
}
#endif
