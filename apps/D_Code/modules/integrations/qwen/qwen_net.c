#define BOOL_DEFINE_CONFLICT

#include "config/app_config.h"

#if D_FEATURE_QWEN

#ifndef CONFIG_PLATFORM_AC7911
#define CONFIG_PLATFORM_AC7911 1
#endif

#include <stdbool.h>
#include "qwen_net.h"
#include "framework/d_crypto.h"
#include "framework/d_log.h"
#include "framework/compat/d_stdio_compat.h"
#include "sys_common.h"
#include "syscfg_id.h"
#if D_DEVICE_CLOUD_ENABLE
#include "modules/services/provisioning/device_credentials.h"
#endif
#include "sdk/include/c_mmi.h"
#include "sdk/include/c_mmi_config.h"
#include "sdk/include/c_mmi_storage.h"
#include "sdk/include/lib_c_license.h"
#include "cJSON.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define TAG                     "QWEN_NET"
#define QWEN_HTTP_BASE_URL      D_QWEN_HTTP_BASE_URL
#define QWEN_HTTP_HOST          D_QWEN_HTTP_HOST
#define QWEN_HTTP_PORT          D_QWEN_HTTP_PORT
#define QWEN_REGISTER_API       D_QWEN_REGISTER_API
#define QWEN_TOKEN_API          D_QWEN_TOKEN_API
#define QWEN_REGISTER_REQ_SIZE  512
#define QWEN_TOKEN_REQ_SIZE     512
#define QWEN_HTTP_READ_TIMEOUT_MS  200U
#define QWEN_HTTP_REQ_BUF_EXTRA    256U
#define QWEN_HTTP_RAW_BUF_SIZE     (D_QWEN_HTTP_BUF_SIZE + 1024U)
#define QWEN_HTTP_AUTH_HEADER_SIZE 512U
#define QWEN_BT_MAC_LEN            6U
#define QWEN_DEVICE_NAME_MAX_LEN   32U
#define QWEN_DEVICE_NAME_PREFIX    "AC791N_"
#define QWEN_TOKEN_VALID_MS        55000U
#define QWEN_TOKEN_REFRESH_MARGIN_MS 10000U
#define QWEN_TOKEN_REFRESH_INTERVAL_MS (QWEN_TOKEN_VALID_MS - QWEN_TOKEN_REFRESH_MARGIN_MS)

#ifndef D_QWEN_DEVICE_NAME_USE_BT_MAC
#define D_QWEN_DEVICE_NAME_USE_BT_MAC 0
#endif

#if ((D_QWEN_AUTH_MODE != D_QWEN_AUTH_MODE_FULL_MANAGED) && \
     (D_QWEN_AUTH_MODE != D_QWEN_AUTH_MODE_SEMI_MANAGED))
#error "Invalid D_QWEN_AUTH_MODE"
#endif

static const char s_qwen_http_ca_pem[] =
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\r\n"
    "A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\r\n"
    "Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\r\n"
    "MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\r\n"
    "A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\r\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\r\n"
    "RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\r\n"
    "gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\r\n"
    "KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\r\n"
    "QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\r\n"
    "XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\r\n"
    "DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\r\n"
    "LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\r\n"
    "RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\r\n"
    "jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\r\n"
    "6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\r\n"
    "mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\r\n"
    "Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\r\n"
    "WD9f\r\n"
    "-----END CERTIFICATE-----\r\n";

typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca_cert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
} qwen_net_tls_ctx_t;

static uint8_t s_qwen_net_prepared;
static qwen_net_event_cb_t s_qwen_event_cb;
static const char s_qwen_empty_str[] = "";
static uint64_t s_qwen_token_refresh_deadline_ms;
static uint8_t s_qwen_net_cleanup_failed;
static uint8_t s_qwen_force_register;

static int qwen_net_is_placeholder(const char *value)
{
    if ((value == NULL) || (value[0] == '\0')) {
        return 1;
    }

    return (strstr(value, "your_") != NULL);
}

static const char *qwen_net_http_ca_pem(void)
{
#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
    return s_qwen_http_ca_pem;
#else
    return D_QWEN_HTTP_CA_PEM;
#endif
}

static int qwen_net_http_tls_verify_mode(void)
{
#if (D_QWEN_HTTP_TLS_VERIFY_MODE == D_QWEN_HTTP_TLS_VERIFY_REQUIRED)
    return MBEDTLS_SSL_VERIFY_REQUIRED;
#else
    return MBEDTLS_SSL_VERIFY_OPTIONAL;
#endif
}

static int qwen_net_http_has_valid_ca_pem(void)
{
#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
    return 1;
#else
    return (qwen_net_is_placeholder(D_QWEN_HTTP_CA_PEM) == 0);
#endif
}

#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
static const device_cred_t *qwen_net_runtime_cred(void)
{
    const device_cred_t *cred;

    if (cred_is_activated() == 0) {
        return NULL;
    }

    cred = cred_get();
    if ((cred == NULL) || (cred->runtime_provider != RUNTIME_PROVIDER_ALIYUN_MMI_RTOS)) {
        return NULL;
    }

    return cred;
}
#endif

int qwen_net_is_runtime_allowed(void)
{
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    const device_cred_t *cred;

    if (cred_is_activated() == 0) {
        return 0;
    }

    cred = cred_get();
    if (cred == NULL) {
        return 0;
    }

    return (cred->runtime_provider == RUNTIME_PROVIDER_ALIYUN_MMI_RTOS) ? 1 : 0;
#endif

    return 1;
}

static const char *qwen_net_app_id(void)
{
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    const device_cred_t *cred = qwen_net_runtime_cred();
    if ((cred != NULL) && (cred->runtime_control.runtime_app_id[0] != '\0')) {
        return cred->runtime_control.runtime_app_id;
    }
#endif
    return D_QWEN_APP_ID;
}

static const char *qwen_net_device_name_value(void)
{
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    const device_cred_t *cred = qwen_net_runtime_cred();
    if (cred != NULL) {
        if (cred->runtime_control.runtime_device_name[0] != '\0') {
            return cred->runtime_control.runtime_device_name;
        }
        if (cred->device_id[0] != '\0') {
            return cred->device_id;
        }
    }
#endif
    return D_QWEN_DEVICE_NAME;
}

static const char *qwen_net_http_host(void)
{
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    const device_cred_t *cred = qwen_net_runtime_cred();
    if ((cred != NULL) && (cred->runtime_control.control_host[0] != '\0')) {
        return cred->runtime_control.control_host;
    }
#endif
    return QWEN_HTTP_HOST;
}

static const char *qwen_net_http_port(void)
{
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    const device_cred_t *cred = qwen_net_runtime_cred();
    if ((cred != NULL) && (cred->runtime_control.control_port[0] != '\0')) {
        return cred->runtime_control.control_port;
    }
#endif
    return QWEN_HTTP_PORT;
}

static const char *qwen_net_register_api(void)
{
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    const device_cred_t *cred = qwen_net_runtime_cred();
    if ((cred != NULL) && (cred->runtime_control.control_register_path[0] != '\0')) {
        return cred->runtime_control.control_register_path;
    }
#endif
    return QWEN_REGISTER_API;
}

static const char *qwen_net_token_api(void)
{
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    const device_cred_t *cred = qwen_net_runtime_cred();
    if ((cred != NULL) && (cred->runtime_control.control_token_path[0] != '\0')) {
        return cred->runtime_control.control_token_path;
    }
#endif
    return QWEN_TOKEN_API;
}

static uint8_t qwen_net_registration_matches_identity(const char *expect_app_id,
                                                     const char *expect_device_name)
{
    char current_app_id[C_MMI_APP_ID_STR_LEN + 1U];
    char current_device_name[C_MMI_DN_STR_MAX + 1U];
    int32_t ret;

    if (expect_app_id == NULL || expect_device_name == NULL) return 0U;
    memset(current_app_id, 0, sizeof(current_app_id));
    ret = c_mmi_storage_get_app_id_str(current_app_id);
    if ((ret != UTIL_SUCCESS) || (strcmp(current_app_id, expect_app_id) != 0)) {
        return 0U;
    }

    memset(current_device_name, 0, sizeof(current_device_name));
    ret = c_mmi_storage_get_device_name(current_device_name);
    if ((ret != UTIL_SUCCESS) || (strcmp(current_device_name, expect_device_name) != 0)) {
        return 0U;
    }

    return 1U;
}

static const char *qwen_net_json_value_or_na(const cJSON *item)
{
    const char *value;

    value = cJSON_GetStringValue(item);
    if (value != NULL) {
        return value;
    }

    return "N/A";
}

static void qwen_net_log_outer_rsp(const char *stage, const char *rsp)
{
    cJSON *root;

    if ((stage == NULL) || (rsp == NULL)) {
        return;
    }

    root = cJSON_Parse(rsp);
    if (root == NULL) {
        LOGW(TAG, "%s: raw=%s", stage, rsp);
        return;
    }

    LOGW(TAG, "%s: code=%s message=%s requestId=%s raw=%s",
         stage,
         qwen_net_json_value_or_na(cJSON_GetObjectItem(root, "code")),
         qwen_net_json_value_or_na(cJSON_GetObjectItem(root, "message")),
         qwen_net_json_value_or_na(cJSON_GetObjectItem(root, "requestId")),
         rsp);
    cJSON_Delete(root);
}

static uint64_t qwen_net_parse_u64(const char *text)
{
    uint64_t parsed_value;

    if (text == NULL) {
        return 0U;
    }

    parsed_value = 0U;
    while ((*text >= '0') && (*text <= '9')) {
        parsed_value = (parsed_value * 10U) + (uint64_t)(*text - '0');
        text++;
    }

    return parsed_value;
}

static uint64_t qwen_net_get_response_time_ms(const char *rsp_data)
{
    cJSON *root;
    cJSON *response_time_item;
    const char *response_time_str;
    uint64_t response_time_ms;

    if (rsp_data == NULL) {
        return 0U;
    }

    root = cJSON_Parse(rsp_data);
    if (root == NULL) {
        return 0U;
    }

    response_time_item = cJSON_GetObjectItem(root, "responseTime");
    response_time_str = cJSON_GetStringValue(response_time_item);
    response_time_ms = qwen_net_parse_u64(response_time_str);
    cJSON_Delete(root);
    return response_time_ms;
}

static void qwen_net_update_token_refresh_deadline(const char *rsp_data)
{
    uint64_t deadline_base_ms;

    deadline_base_ms = qwen_net_get_response_time_ms(rsp_data);
    if (deadline_base_ms == 0U) {
        deadline_base_ms = d_get_timestamp_ms();
    }

    s_qwen_token_refresh_deadline_ms =
        deadline_base_ms + (uint64_t)QWEN_TOKEN_REFRESH_INTERVAL_MS;
    LOGI(TAG, "token refresh deadline=%llu",
         (unsigned long long)s_qwen_token_refresh_deadline_ms);
}

static d_err_t qwen_net_check_config(void)
{
    if (qwen_net_is_placeholder(qwen_net_app_id()) != 0) {
        return D_ERR_NOT_READY;
    }

    if (qwen_net_is_placeholder(D_QWEN_APP_SECRET) != 0) {
        return D_ERR_NOT_READY;
    }

    if (qwen_net_is_placeholder(qwen_net_http_host()) != 0) {
        return D_ERR_NOT_READY;
    }

    if (qwen_net_is_placeholder(qwen_net_http_port()) != 0) {
        return D_ERR_NOT_READY;
    }

    if (qwen_net_is_placeholder(qwen_net_register_api()) != 0) {
        return D_ERR_NOT_READY;
    }

    if (qwen_net_is_placeholder(qwen_net_token_api()) != 0) {
        return D_ERR_NOT_READY;
    }

#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    if ((D_QWEN_HTTP_TLS_VERIFY_MODE == D_QWEN_HTTP_TLS_VERIFY_REQUIRED) &&
        (qwen_net_is_placeholder(D_QWEN_HTTP_CA_PEM) != 0)) {
        return D_ERR_NOT_READY;
    }
#endif

#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
    if (qwen_net_is_placeholder(D_QWEN_API_KEY) != 0) {
        return D_ERR_NOT_READY;
    }
#endif

    if (qwen_net_is_placeholder(qwen_net_device_name_value()) != 0) {
        return D_ERR_NOT_READY;
    }

    return D_OK;
}

static d_err_t qwen_net_map_sdk_err(int32_t ret)
{
    if (ret == 0) {
        return D_OK;
    }

    if (ret == UTIL_ERR_INVALID_PARAM) {
        return D_ERR_INVALID_ARG;
    }

    if (ret == UTIL_ERR_BUSY) {
        return D_ERR_BUSY;
    }

    if ((ret == UTIL_ERR_NO_READY) || (ret == UTIL_ERR_NO_INIT)) {
        return D_ERR_NOT_READY;
    }

    return D_ERR_IO;
}

static d_err_t qwen_net_check_license_ready(void)
{
    if (c_license_is_enable() != 0U) {
        return D_OK;
    }

    LOGE(TAG, "License mode disabled, check libc_license.a link");
    return D_ERR_IO;
}

#if (D_QWEN_DEVICE_NAME_USE_BT_MAC != 0)
static uint8_t qwen_net_is_valid_bt_mac(const uint8_t *mac_addr)
{
    uint8_t index;
    uint8_t has_non_zero;
    uint8_t has_non_ff;

    if (mac_addr == NULL) {
        return 0U;
    }

    has_non_zero = 0U;
    has_non_ff = 0U;
    for (index = 0U; index < QWEN_BT_MAC_LEN; index++) {
        if (mac_addr[index] != 0x00U) {
            has_non_zero = 1U;
        }
        if (mac_addr[index] != 0xFFU) {
            has_non_ff = 1U;
        }
    }

    if ((has_non_zero == 0U) || (has_non_ff == 0U)) {
        return 0U;
    }

    return 1U;
}
#endif

static d_err_t qwen_net_build_device_name(char *device_name, uint32_t device_name_size)
{
#if (D_QWEN_DEVICE_NAME_USE_BT_MAC != 0)
    uint8_t mac_addr[QWEN_BT_MAC_LEN];
    int read_len;
#endif
    const char *runtime_device_name;
    int write_len;

    if ((device_name == NULL) || (device_name_size < (QWEN_DEVICE_NAME_MAX_LEN + 1U))) {
        return D_ERR_INVALID_ARG;
    }

    runtime_device_name = qwen_net_device_name_value();

#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    if (qwen_net_runtime_cred() != NULL) {
        write_len = snprintf(device_name, device_name_size, "%s", runtime_device_name);
        if ((write_len <= 0) || ((uint32_t)write_len >= device_name_size)) {
            return D_ERR_INVALID_ARG;
        }
        LOGI(TAG, "device name runtime: %s", device_name);
        return D_OK;
    }
#endif

#if (D_QWEN_DEVICE_NAME_USE_BT_MAC != 0)
    memset(mac_addr, 0, sizeof(mac_addr));
    read_len = syscfg_read(CFG_BT_MAC_ADDR, mac_addr, sizeof(mac_addr));
    if ((read_len == (int)sizeof(mac_addr)) && (qwen_net_is_valid_bt_mac(mac_addr) != 0U)) {
        write_len = snprintf(device_name, device_name_size, "%s%02X%02X%02X%02X%02X%02X",
                             QWEN_DEVICE_NAME_PREFIX,
                             mac_addr[0],
                             mac_addr[1],
                             mac_addr[2],
                             mac_addr[3],
                             mac_addr[4],
                             mac_addr[5]);
        if ((write_len <= 0) || ((uint32_t)write_len >= device_name_size)) {
            return D_ERR_INVALID_ARG;
        }
        LOGI(TAG, "device name from bt mac: %s", device_name);
        return D_OK;
    }
    LOGW(TAG, "bt mac unavailable: read=%d fallback=%s", read_len, runtime_device_name);
#endif

    write_len = snprintf(device_name, device_name_size, "%s", runtime_device_name);
    if ((write_len <= 0) || ((uint32_t)write_len >= device_name_size)) {
        return D_ERR_INVALID_ARG;
    }
    LOGI(TAG, "device name fixed: %s", device_name);
    return D_OK;
}

static d_err_t qwen_net_apply_credentials(void)
{
    char device_name[QWEN_DEVICE_NAME_MAX_LEN + 1U];
    d_err_t err;
    int32_t ret;
    uint8_t is_registered;
    uint8_t need_reset;

    err = qwen_net_check_license_ready();
    if (err != D_OK) {
        return err;
    }

    err = qwen_net_build_device_name(device_name, sizeof(device_name));
    if (err != D_OK) return err;
    is_registered = c_license_device_is_registered();
    /* A mode switch can change either owner of the SDK's persisted identity. */
    need_reset = is_registered && !qwen_net_registration_matches_identity(qwen_net_app_id(), device_name);
    s_qwen_force_register = need_reset;
    LOGI(TAG, "credential preload: license=%u registered=%u",
         (unsigned)c_license_is_enable(), (unsigned)is_registered);
    if ((is_registered == 0U) || (need_reset != 0U)) {
        LOGI(TAG, "%s", (need_reset != 0U) ?
             "configured identity changed, reset app/device credentials" :
             "device not registered, preload app/device credentials");
        ret = c_mmi_storage_reset();
        err = qwen_net_map_sdk_err(ret);
        if (err != D_OK) {
            return err;
        }

        ret = c_mmi_storage_set_app_id_str((char *)qwen_net_app_id());
        err = qwen_net_map_sdk_err(ret);
        if (err != D_OK) {
            return err;
        }

        ret = c_license_set_app_secret_str((char *)D_QWEN_APP_SECRET);
        err = qwen_net_map_sdk_err(ret);
        if (err != D_OK) {
            return err;
        }

        if (qwen_net_is_placeholder(D_QWEN_WORKSPACE_ID) == 0) {
            LOGI(TAG, "workspace id configured");
            ret = c_mmi_storage_set_ws_id((char *)D_QWEN_WORKSPACE_ID);
            if (ret == UTIL_ERR_UNSUPPORT) {
                LOGW(TAG, "workspace id storage unsupported, skip");
            } else {
                err = qwen_net_map_sdk_err(ret);
                if (err != D_OK) {
                    return err;
                }
            }
        }

        ret = c_mmi_set_device_name(device_name);
        err = qwen_net_map_sdk_err(ret);
        if (err != D_OK) {
            return err;
        }

        ret = c_mmi_storage_save();
        err = qwen_net_map_sdk_err(ret);
        if (err != D_OK) {
            return err;
        }
    } else {
#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
        LOGI(TAG, "device already registered, refresh api key only");
#else
        LOGI(TAG, "device already registered");
#endif
    }

#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
    ret = c_mmi_storage_set_api_key((char *)D_QWEN_API_KEY);
    err = qwen_net_map_sdk_err(ret);
    if (err == D_OK) {
        LOGI(TAG, "api key refreshed");
    }
    return err;
#else
    return D_OK;
#endif
}

static const char *qwen_net_downstream_format_name(void)
{
#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
    return "raw_opus";
#else
    return "pcm";
#endif
}

static const char *qwen_net_upstream_format_name(void)
{
#if (D_QWEN_US_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
    return "raw_opus";
#else
    return "pcm";
#endif
}

static const char *qwen_net_dialog_mode_name(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    return "duplex";
#else
    return "push2talk";
#endif
}

static uint8_t qwen_net_dialog_work_mode(void)
{
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    return C_MMI_MODE_DUPLEX;
#else
    return C_MMI_MODE_PUSH2TALK;
#endif
}

static void qwen_net_apply_downstream_config(mmi_user_config_t *config)
{
    if (config == NULL) {
        return;
    }

#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
    config->downstream_mode = C_MMI_STREAM_MODE_OPUS_RAW;
    config->enable_cbr = D_QWEN_DS_OPUS_ENABLE_CBR;
    config->frame_size = D_QWEN_DS_OPUS_FRAME_MS;
    config->bit_rate = D_QWEN_DS_OPUS_BIT_RATE_KBPS;
#else
    config->downstream_mode = C_MMI_STREAM_MODE_PCM;
#endif
}

static d_err_t qwen_net_apply_user_config(void)
{
    mmi_user_config_t config = C_MMI_CONFIG_DEFAULT();
    d_err_t err;

    config.evt_cb = (c_mmi_event_callback)s_qwen_event_cb;
    config.work_mode = qwen_net_dialog_work_mode();
    config.text_mode = C_MMI_TEXT_MODE_BOTH;
#if (D_QWEN_US_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
    config.upstream_mode = C_MMI_STREAM_MODE_OPUS_RAW;
#else
    config.upstream_mode = C_MMI_STREAM_MODE_PCM;
#endif
    qwen_net_apply_downstream_config(&config);
    config.recorder_rb_size = D_QWEN_RECORDER_RB_SIZE;
    config.player_rb_size = 1024U * 1024U;
    config.us_sample_rate = D_QWEN_US_SAMPLE_RATE;
    config.ds_sample_rate = D_QWEN_DS_SAMPLE_RATE;
    strncpy(config.voice_id, D_QWEN_VOICE_ID, sizeof(config.voice_id) - 1U);
    strncpy(config.story_voice_id, D_QWEN_VOICE_ID, sizeof(config.story_voice_id) - 1U);

    err = qwen_net_map_sdk_err(c_mmi_config(&config));
    if (err == D_OK) {
        LOGI(TAG, "config applied: mode=%u dialog=%s us=%u ds=%u us_fmt=%s ds_fmt=%s voice=%s",
             (unsigned)config.work_mode,
             qwen_net_dialog_mode_name(),
             (unsigned)config.us_sample_rate,
             (unsigned)config.ds_sample_rate,
             qwen_net_upstream_format_name(),
             qwen_net_downstream_format_name(),
             config.voice_id);
        LOGI(TAG, "recorder_rb=%u player_rb=%u",
             (unsigned)config.recorder_rb_size,
             (unsigned)config.player_rb_size);
#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
        LOGI(TAG, "opus downlink: cbr=%u frame=%u bitrate=%u pkt=%u",
             (unsigned)config.enable_cbr,
             (unsigned)config.frame_size,
             (unsigned)config.bit_rate,
             (unsigned)D_QWEN_DS_OPUS_PKT_LEN);
#endif
        c_mmi_config_print();
    }
    return err;
}

static void qwen_net_log_tls_error(const char *stage, int ret)
{
    LOGE(TAG, "%s failed: ret=%d", stage, ret);
}

static void qwen_net_log_http_meta(int status_code, uint32_t header_len,
                                   uint8_t is_chunked, int content_len)
{
    LOGI(TAG, "HTTP meta: status=%d header_len=%u chunked=%u content_len=%d",
         status_code,
         (unsigned)header_len,
         (unsigned)is_chunked,
         content_len);
}

static void qwen_net_log_tls_connected(qwen_net_tls_ctx_t *tls,
                                       uint32_t verify_flags,
                                       uint32_t handshake_retry)
{
    const char *cipher_suite;

    cipher_suite = mbedtls_ssl_get_ciphersuite(&tls->ssl);
    LOGI(TAG, "TLS handshake done: retries=%u verify=0x%x cipher=%s",
         (unsigned)handshake_retry,
         (unsigned)verify_flags,
         (cipher_suite != NULL) ? cipher_suite : "N/A");
}

static uint32_t qwen_net_elapsed_ms(uint32_t start_ms)
{
    return (uint32_t)(timer_get_ms() - start_ms);
}

static d_err_t qwen_net_build_http_auth_header(char *header, uint32_t header_size)
{
    d_err_t err;

    if ((header == NULL) || (header_size == 0U)) {
        return D_ERR_INVALID_ARG;
    }

    header[0] = '\0';
#if (D_DEVICE_CLOUD_ENABLE) && (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_SEMI_MANAGED)
    {
        const device_cred_t *cred;
        char ts[32];
        char nonce[33];
        char sign[65];
        char canonical[512];
        const char *parts[4];
        int canonical_len;
        int write_len;

        cred = qwen_net_runtime_cred();
        if (cred == NULL) {
            return D_OK;
        }

        if ((cred->device_id[0] == '\0') || (cred->access_key[0] == '\0') ||
            (cred->access_secret[0] == '\0')) {
            return D_ERR_NOT_READY;
        }

        snprintf(ts, sizeof(ts), "%llu", d_get_timestamp_ms());
        d_generate_nonce(nonce, sizeof(nonce));
        parts[0] = cred->device_id;
        parts[1] = cred->access_key;
        parts[2] = ts;
        parts[3] = nonce;
        canonical_len = d_build_canonical(parts, 4, canonical, sizeof(canonical));
        if (canonical_len < 0) {
            return D_ERR_INVALID_ARG;
        }

        err = d_hmac_sha256((const unsigned char *)cred->access_secret,
                            (int)strlen(cred->access_secret),
                            (const unsigned char *)canonical,
                            canonical_len,
                            sign);
        if (err != D_OK) {
            return err;
        }

        write_len = snprintf(header, header_size,
                             "X-Device-Id: %s\r\n"
                             "X-Access-Key: %s\r\n"
                             "X-Timestamp: %s\r\n"
                             "X-Nonce: %s\r\n"
                             "X-Sign: %s\r\n"
                             "X-Firmware-Version: %s\r\n",
                             cred->device_id,
                             cred->access_key,
                             ts,
                             nonce,
                             sign,
                             D_FIRMWARE_VERSION);
        if ((write_len <= 0) || ((uint32_t)write_len >= header_size)) {
            return D_ERR_NO_MEMORY;
        }
    }
#endif

    return D_OK;
}

static const char *qwen_net_memcasestr(const char *buf, uint32_t buf_len, const char *pattern)
{
    uint32_t i;
    uint32_t j;
    uint32_t pattern_len;

    if ((buf == NULL) || (pattern == NULL)) {
        return NULL;
    }

    pattern_len = (uint32_t)strlen(pattern);
    if ((pattern_len == 0U) || (pattern_len > buf_len)) {
        return NULL;
    }

    for (i = 0U; i + pattern_len <= buf_len; i++) {
        for (j = 0U; j < pattern_len; j++) {
            if (tolower((unsigned char)buf[i + j]) != tolower((unsigned char)pattern[j])) {
                break;
            }
        }
        if (j == pattern_len) {
            return buf + i;
        }
    }

    return NULL;
}

static int qwen_net_parse_content_length(const char *header, uint32_t header_len)
{
    const char *field;

    field = qwen_net_memcasestr(header, header_len, "Content-Length:");
    if (field == NULL) {
        return -1;
    }

    field += strlen("Content-Length:");
    while ((((uint32_t)(field - header)) < header_len) && ((*field == ' ') || (*field == '\t'))) {
        field++;
    }
    return atoi(field);
}

static int qwen_net_is_chunked_rsp(const char *header, uint32_t header_len)
{
    return (qwen_net_memcasestr(header, header_len, "Transfer-Encoding: chunked") != NULL);
}

static int qwen_net_parse_chunk_size(const char *line, uint32_t line_len, uint32_t *chunk_size)
{
    char size_str[24];
    char *end_ptr;
    unsigned long value;

    if ((line == NULL) || (chunk_size == NULL) || (line_len == 0U) || (line_len >= sizeof(size_str))) {
        return -1;
    }

    memcpy(size_str, line, line_len);
    size_str[line_len] = '\0';
    value = strtoul(size_str, &end_ptr, 16);
    if ((end_ptr == size_str) || ((*end_ptr != '\0') && (*end_ptr != ';'))) {
        return -1;
    }

    *chunk_size = (uint32_t)value;
    return 0;
}

static int qwen_net_is_chunked_complete(const char *body)
{
    const char *line_end;
    const char *chunk_data;
    uint32_t chunk_size;

    while (1) {
        line_end = strstr(body, "\r\n");
        if (line_end == NULL) {
            return 0;
        }
        if (qwen_net_parse_chunk_size(body, (uint32_t)(line_end - body), &chunk_size) != 0) {
            return -1;
        }

        chunk_data = line_end + 2;
        if (chunk_size == 0U) {
            return ((strlen(chunk_data) >= 2U) && (chunk_data[0] == '\r') && (chunk_data[1] == '\n')) ? 1 : 0;
        }
        if (strlen(chunk_data) < ((size_t)chunk_size + 2U)) {
            return 0;
        }
        if ((chunk_data[chunk_size] != '\r') || (chunk_data[chunk_size + 1U] != '\n')) {
            return -1;
        }
        body = chunk_data + chunk_size + 2U;
    }
}

static int qwen_net_is_rsp_complete(const char *raw)
{
    const char *header_end;
    const char *body;
    uint32_t header_len;
    int content_len;

    header_end = strstr(raw, "\r\n\r\n");
    if (header_end == NULL) {
        return 0;
    }

    header_len = (uint32_t)(header_end - raw);
    body = header_end + 4;
    if (qwen_net_is_chunked_rsp(raw, header_len) != 0) {
        return qwen_net_is_chunked_complete(body);
    }

    content_len = qwen_net_parse_content_length(raw, header_len);
    if (content_len < 0) {
        return 0;
    }
    return (strlen(body) >= (size_t)content_len) ? 1 : 0;
}

static int qwen_net_decode_chunked_body(const char *chunked, char *out, uint32_t out_size)
{
    const char *line_end;
    const char *chunk_data;
    uint32_t chunk_size;
    uint32_t total_len;

    total_len = 0U;
    while (1) {
        line_end = strstr(chunked, "\r\n");
        if (line_end == NULL) {
            return -1;
        }
        if (qwen_net_parse_chunk_size(chunked, (uint32_t)(line_end - chunked), &chunk_size) != 0) {
            return -1;
        }

        chunk_data = line_end + 2;
        if (chunk_size == 0U) {
            if ((strlen(chunk_data) < 2U) || (chunk_data[0] != '\r') || (chunk_data[1] != '\n')) {
                return -1;
            }
            if (total_len >= out_size) {
                return -1;
            }
            out[total_len] = '\0';
            return (int)total_len;
        }
        if ((strlen(chunk_data) < ((size_t)chunk_size + 2U)) || (total_len + chunk_size + 1U > out_size)) {
            return -1;
        }
        if ((chunk_data[chunk_size] != '\r') || (chunk_data[chunk_size + 1U] != '\n')) {
            return -1;
        }

        memcpy(out + total_len, chunk_data, chunk_size);
        total_len += chunk_size;
        chunked = chunk_data + chunk_size + 2U;
    }
}

static int qwen_net_extract_rsp_body(const char *raw, char *out, uint32_t out_size)
{
    const char *header_end;
    const char *body;
    uint32_t header_len;
    int body_len;
    int content_len;
    int is_chunked;
    int status_code;

    if ((raw == NULL) || (out == NULL) || (out_size < 2U)) {
        return -1;
    }

    header_end = strstr(raw, "\r\n\r\n");
    if (header_end == NULL) {
        return -1;
    }

    status_code = 0;
    sscanf(raw, "HTTP/%*u.%*u %d", &status_code);
    LOGI(TAG, "HTTP status=%d", status_code);

    header_len = (uint32_t)(header_end - raw);
    body = header_end + 4;
    content_len = qwen_net_parse_content_length(raw, header_len);
    is_chunked = qwen_net_is_chunked_rsp(raw, header_len);
    qwen_net_log_http_meta(status_code, header_len, (uint8_t)(is_chunked != 0), content_len);
    if (status_code != 200) {
        LOGW(TAG, "HTTP non-success status=%d", status_code);
    }
    if (is_chunked != 0) {
        return qwen_net_decode_chunked_body(body, out, out_size);
    }

    body_len = content_len;
    if (body_len < 0) {
        body_len = (int)strlen(body);
    }
    if (((uint32_t)body_len >= out_size) || (strlen(body) < (size_t)body_len)) {
        return -1;
    }

    memcpy(out, body, (uint32_t)body_len);
    out[body_len] = '\0';
    return body_len;
}

static void qwen_net_tls_init(qwen_net_tls_ctx_t *tls)
{
    mbedtls_net_init(&tls->net);
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_x509_crt_init(&tls->ca_cert);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_ctr_drbg_init(&tls->ctr_drbg);
}

static void qwen_net_tls_deinit(qwen_net_tls_ctx_t *tls)
{
    mbedtls_ssl_close_notify(&tls->ssl);
    mbedtls_net_free(&tls->net);
    mbedtls_x509_crt_free(&tls->ca_cert);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_ctr_drbg_free(&tls->ctr_drbg);
    mbedtls_entropy_free(&tls->entropy);
}

static int qwen_net_tls_connect(qwen_net_tls_ctx_t *tls)
{
    static const char *pers = "qwen_http";
    const char *ca_pem;
    int has_valid_ca_pem;
    const char *http_host;
    const char *http_port;
    uint32_t handshake_retry;
    uint32_t verify_flags;
    int ret;

    http_host = qwen_net_http_host();
    http_port = qwen_net_http_port();
    LOGI(TAG, "TLS connect start: host=%s port=%s", http_host, http_port);
    ret = mbedtls_ctr_drbg_seed(&tls->ctr_drbg, mbedtls_entropy_func, &tls->entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        qwen_net_log_tls_error("mbedtls_ctr_drbg_seed", ret);
        return -1;
    }
    ca_pem = qwen_net_http_ca_pem();
    has_valid_ca_pem = qwen_net_http_has_valid_ca_pem();
#if (D_QWEN_HTTP_TLS_VERIFY_MODE == D_QWEN_HTTP_TLS_VERIFY_REQUIRED)
    if (!has_valid_ca_pem) {
        LOGE(TAG, "TLS trusted CA is missing; request not sent");
        return -1;
    }
#else
    LOGW(TAG, "DIAGNOSTIC: HTTPS peer verification is not enforced");
#endif
    if (has_valid_ca_pem != 0) {
        ret = mbedtls_x509_crt_parse(&tls->ca_cert, (const unsigned char *)ca_pem,
                                     strlen(ca_pem) + 1U);
        if (ret != 0) {
            qwen_net_log_tls_error("mbedtls_x509_crt_parse", ret);
            return -1;
        }
    }
    ret = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        qwen_net_log_tls_error("mbedtls_ssl_config_defaults", ret);
        return -1;
    }

    mbedtls_ssl_conf_authmode(&tls->conf, qwen_net_http_tls_verify_mode());
    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctr_drbg);
    if (has_valid_ca_pem != 0) {
        mbedtls_ssl_conf_ca_chain(&tls->conf, &tls->ca_cert, NULL);
    }
    mbedtls_ssl_conf_read_timeout(&tls->conf, D_QWEN_HTTP_TIMEOUT_MS);
    ret = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
    if (ret != 0) {
        qwen_net_log_tls_error("mbedtls_ssl_setup", ret);
        return -1;
    }
    ret = mbedtls_ssl_set_hostname(&tls->ssl, http_host);
    if (ret != 0) {
        qwen_net_log_tls_error("mbedtls_ssl_set_hostname", ret);
        return -1;
    }

    mbedtls_net_set_timeout(&tls->net, D_QWEN_HTTP_TIMEOUT_MS, D_QWEN_HTTP_TIMEOUT_MS);
    mbedtls_ssl_set_bio(&tls->ssl, &tls->net, mbedtls_net_send, mbedtls_net_recv,
                        mbedtls_net_recv_timeout);
    ret = mbedtls_net_connect(&tls->net, http_host, http_port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        qwen_net_log_tls_error("mbedtls_net_connect", ret);
        return -1;
    }

    LOGI(TAG, "TCP connect done");
    handshake_retry = 0U;
    while ((ret = mbedtls_ssl_handshake(&tls->ssl)) != 0) {
        if ((ret != MBEDTLS_ERR_SSL_WANT_READ) && (ret != MBEDTLS_ERR_SSL_WANT_WRITE)) {
            qwen_net_log_tls_error("mbedtls_ssl_handshake", ret);
            return -1;
        }
        handshake_retry++;
    }

    verify_flags = mbedtls_ssl_get_verify_result(&tls->ssl);
    if (verify_flags != 0U) {
#if (D_QWEN_HTTP_TLS_VERIFY_MODE == D_QWEN_HTTP_TLS_VERIFY_REQUIRED)
        LOGE(TAG, "TLS verify failed: flags=0x%x", (unsigned)verify_flags);
        return -1;
#else
        LOGW(TAG, "TLS verify flags=0x%x", (unsigned)verify_flags);
#endif
    }
    qwen_net_log_tls_connected(tls, verify_flags, handshake_retry);

    mbedtls_net_set_timeout(&tls->net, D_QWEN_HTTP_TIMEOUT_MS, QWEN_HTTP_READ_TIMEOUT_MS);
    mbedtls_ssl_conf_read_timeout(&tls->conf, QWEN_HTTP_READ_TIMEOUT_MS);
    return 0;
}

static int qwen_net_tls_write_all(qwen_net_tls_ctx_t *tls, const char *data, uint32_t data_len)
{
    uint32_t start_ms;
    uint32_t offset;
    int ret;

    start_ms = timer_get_ms();
    offset = 0U;
    LOGI(TAG, "HTTP write start: total=%u", (unsigned)data_len);
    while (offset < data_len) {
        ret = mbedtls_ssl_write(&tls->ssl, (const unsigned char *)data + offset, data_len - offset);
        if (ret > 0) {
            offset += (uint32_t)ret;
            continue;
        }
        if ((ret == MBEDTLS_ERR_SSL_WANT_READ) || (ret == MBEDTLS_ERR_SSL_WANT_WRITE) ||
            (ret == MBEDTLS_ERR_SSL_TIMEOUT)) {
            if (qwen_net_elapsed_ms(start_ms) < D_QWEN_HTTP_TIMEOUT_MS) {
                continue;
            }
            break;
        }
        qwen_net_log_tls_error("mbedtls_ssl_write", ret);
        return -1;
    }
    if (offset == data_len) {
        LOGI(TAG, "HTTP write done: total=%u elapsed=%u",
             (unsigned)offset,
             (unsigned)qwen_net_elapsed_ms(start_ms));
        return 0;
    }

    LOGE(TAG, "HTTP write timeout: sent=%u total=%u elapsed=%u",
         (unsigned)offset,
         (unsigned)data_len,
         (unsigned)qwen_net_elapsed_ms(start_ms));
    return -1;
}

static int qwen_net_tls_read_all(qwen_net_tls_ctx_t *tls, char *raw, uint32_t raw_size)
{
    uint32_t start_ms;
    uint32_t read_count;
    uint32_t raw_len;
    int ret;
    int done;

    start_ms = timer_get_ms();
    read_count = 0U;
    raw_len = 0U;
    raw[0] = '\0';
    LOGI(TAG, "HTTP read start: timeout=%u", (unsigned)D_QWEN_HTTP_TIMEOUT_MS);
    while (qwen_net_elapsed_ms(start_ms) < D_QWEN_HTTP_TIMEOUT_MS) {
        if (raw_len + 1U >= raw_size) {
            LOGE(TAG, "HTTP response too large");
            return -1;
        }

        ret = mbedtls_ssl_read(&tls->ssl, (unsigned char *)raw + raw_len, raw_size - raw_len - 1U);
        if (ret > 0) {
            read_count++;
            raw_len += (uint32_t)ret;
            raw[raw_len] = '\0';
            LOGI(TAG, "HTTP read chunk[%u]=%d total=%u",
                 (unsigned)read_count,
                 ret,
                 (unsigned)raw_len);
            done = qwen_net_is_rsp_complete(raw);
            if (done < 0) {
                LOGE(TAG, "HTTP response invalid while reading");
                return -1;
            }
            if (done != 0) {
                LOGI(TAG, "HTTP read complete: total=%u reads=%u elapsed=%u",
                     (unsigned)raw_len,
                     (unsigned)read_count,
                     (unsigned)qwen_net_elapsed_ms(start_ms));
                return (int)raw_len;
            }
            continue;
        }
        if ((ret == MBEDTLS_ERR_SSL_WANT_READ) || (ret == MBEDTLS_ERR_SSL_WANT_WRITE) ||
            (ret == MBEDTLS_ERR_SSL_TIMEOUT)) {
            done = qwen_net_is_rsp_complete(raw);
            if (done < 0) {
                LOGE(TAG, "HTTP response invalid after timeout poll");
                return -1;
            }
            if (done != 0) {
                LOGI(TAG, "HTTP read complete after timeout poll: total=%u reads=%u elapsed=%u",
                     (unsigned)raw_len,
                     (unsigned)read_count,
                     (unsigned)qwen_net_elapsed_ms(start_ms));
                return (int)raw_len;
            }
            continue;
        }
        if ((ret == 0) || (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)) {
            LOGI(TAG, "HTTP peer closed: total=%u reads=%u",
                 (unsigned)raw_len,
                 (unsigned)read_count);
            return (raw_len > 0U) ? (int)raw_len : -1;
        }

        qwen_net_log_tls_error("mbedtls_ssl_read", ret);
        return -1;
    }

    LOGE(TAG, "HTTP read timeout: total=%u reads=%u elapsed=%u",
         (unsigned)raw_len,
         (unsigned)read_count,
         (unsigned)qwen_net_elapsed_ms(start_ms));
    return (raw_len > 0U) ? (int)raw_len : -1;
}

static char *qwen_net_http_post(const char *path, const char *body)
{
    qwen_net_tls_ctx_t tls;
    char extra_header[QWEN_HTTP_AUTH_HEADER_SIZE];
    const char *http_host;
    char *req;
    char *raw_rsp;
    char *rsp;
    uint32_t req_size;
    int raw_len;
    int rsp_len;
    d_err_t err;

    if ((path == NULL) || (body == NULL)) {
        return NULL;
    }

    http_host = qwen_net_http_host();
    req = NULL;
    raw_rsp = NULL;
    rsp = NULL;
    qwen_net_tls_init(&tls);
    err = qwen_net_build_http_auth_header(extra_header, sizeof(extra_header));
    if (err != D_OK) {
        LOGE(TAG, "HTTP auth header build failed: %d", err);
        goto exit;
    }
    req_size = (uint32_t)strlen(path) + (uint32_t)strlen(body) +
               (uint32_t)strlen(http_host) + (uint32_t)strlen(extra_header) +
               QWEN_HTTP_REQ_BUF_EXTRA + QWEN_HTTP_AUTH_HEADER_SIZE;
    req = (char *)malloc(req_size);
    raw_rsp = (char *)malloc(QWEN_HTTP_RAW_BUF_SIZE);
    if ((req == NULL) || (raw_rsp == NULL)) {
        LOGE(TAG, "HTTP alloc failed");
        goto exit;
    }

    snprintf(req, req_size,
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Accept: */*\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %u\r\n"
             "%s"
             "Connection: close\r\n\r\n"
             "%s",
             path, http_host, (unsigned)strlen(body), extra_header, body);
    LOGI(TAG, "HTTPS POST %s body_len=%u timeout=%d",
         path,
         (unsigned)strlen(body),
         D_QWEN_HTTP_TIMEOUT_MS);
    if (qwen_net_tls_connect(&tls) != 0) {
        goto exit;
    }
    if (qwen_net_tls_write_all(&tls, req, (uint32_t)strlen(req)) != 0) {
        goto exit;
    }

    raw_len = qwen_net_tls_read_all(&tls, raw_rsp, QWEN_HTTP_RAW_BUF_SIZE);
    if (raw_len <= 0) {
        LOGE(TAG, "HTTPS POST failed: path=%s raw_len=%d", path, raw_len);
        goto exit;
    }

    rsp = (char *)malloc(D_QWEN_HTTP_BUF_SIZE);
    if (rsp == NULL) {
        LOGE(TAG, "HTTP response alloc failed");
        goto exit;
    }
    rsp_len = qwen_net_extract_rsp_body(raw_rsp, rsp, D_QWEN_HTTP_BUF_SIZE);
    if (rsp_len < 0) {
        LOGE(TAG, "HTTP body parse failed: path=%s raw=%s", path, raw_rsp);
        free(rsp);
        rsp = NULL;
        goto exit;
    }

    LOGI(TAG, "HTTPS POST done: path=%s raw_len=%d body_len=%d", path, raw_len, rsp_len);

exit:
    qwen_net_tls_deinit(&tls);
    if (req != NULL) {
        free(req);
    }
    if (raw_rsp != NULL) {
        free(raw_rsp);
    }
    return rsp;
}

static d_err_t qwen_net_extract_data(const char *rsp, char *out, uint32_t out_size)
{
    cJSON *root;
    cJSON *data;
    char *data_str;
    uint32_t rsp_len;

    if ((rsp == NULL) || (out == NULL) || (out_size < 2U)) {
        return D_ERR_INVALID_ARG;
    }

    root = cJSON_Parse(rsp);
    if (root == NULL) {
        LOGE(TAG, "HTTP response parse failed");
        return D_ERR_IO;
    }

    data = cJSON_GetObjectItem(root, "data");
    if (data == NULL) {
        rsp_len = (uint32_t)strlen(rsp);
        if (rsp_len >= out_size) {
            LOGE(TAG, "HTTP raw response too large: len=%u out=%u",
                 (unsigned)rsp_len, (unsigned)out_size);
            cJSON_Delete(root);
            return D_ERR_NO_MEMORY;
        }
        strcpy(out, rsp);
        cJSON_Delete(root);
        return D_OK;
    }

    data_str = cJSON_PrintUnformatted(data);
    if (data_str == NULL) {
        LOGE(TAG, "HTTP data stringify failed");
        cJSON_Delete(root);
        return D_ERR_NO_MEMORY;
    }

    if ((uint32_t)strlen(data_str) >= out_size) {
        LOGE(TAG, "HTTP data too large: len=%u out=%u",
             (unsigned)strlen(data_str), (unsigned)out_size);
        free(data_str);
        cJSON_Delete(root);
        return D_ERR_NO_MEMORY;
    }

    strcpy(out, data_str);
    free(data_str);
    cJSON_Delete(root);
    return D_OK;
}

static d_err_t qwen_net_do_register(void)
{
    char req[QWEN_REGISTER_REQ_SIZE];
    char rsp_data[D_QWEN_HTTP_BUF_SIZE];
    char time_ms[32];
    char *rsp;
    int32_t ret;
    d_err_t err;

    LOGI(TAG, "device register start");
    snprintf(time_ms, sizeof(time_ms), "%llu", d_get_timestamp_ms());
    ret = c_license_gen_register_str(req, sizeof(req), time_ms);
    err = qwen_net_map_sdk_err(ret);
    if (err != D_OK) {
        LOGE(TAG, "gen register req failed: %d", err);
        return err;
    }

    LOGI(TAG, "register req ready len=%u", (unsigned)strlen(req));

    rsp = qwen_net_http_post(qwen_net_register_api(), req);
    if (rsp == NULL) {
        return D_ERR_IO;
    }

    err = qwen_net_extract_data(rsp, rsp_data, sizeof(rsp_data));
    if (err != D_OK) {
        qwen_net_log_outer_rsp("register outer rsp", rsp);
        free(rsp);
        LOGE(TAG, "extract register data failed: %d", err);
        return err;
    }

    LOGI(TAG, "register data extracted len=%u", (unsigned)strlen(rsp_data));

    ret = c_license_analyze_register_rsp(rsp_data);
    err = qwen_net_map_sdk_err(ret);
    if (err != D_OK) {
        qwen_net_log_outer_rsp("register outer rsp", rsp);
        LOGE(TAG, "analyze register rsp failed: %d data=%s", err, rsp_data);
        free(rsp);
        return err;
    }

    free(rsp);

    err = qwen_net_map_sdk_err(c_mmi_storage_save());
    if (err == D_OK) {
        LOGI(TAG, "device register done");
    } else {
        LOGE(TAG, "register save failed: %d", err);
    }
    return err;
}

static d_err_t qwen_net_do_get_token(void)
{
    char req[QWEN_TOKEN_REQ_SIZE];
    char rsp_data[D_QWEN_HTTP_BUF_SIZE];
    char time_ms[32];
#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
    char api_key[C_MMI_API_KEY_LEN + 1U];
#endif
    char *rsp;
    int32_t ret;
    d_err_t err;

    LOGI(TAG, "get token start");
#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
    memset(api_key, 0, sizeof(api_key));
#endif
    snprintf(time_ms, sizeof(time_ms), "%llu", d_get_timestamp_ms());
#if (D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED)
    ret = c_mmi_storage_get_api_key(api_key);
    if (ret == UTIL_SUCCESS) {
        ret = c_license_gen_get_token_str(req, sizeof(req), time_ms, api_key);
    } else {
        LOGW(TAG, "read api key failed: %d, fallback to NULL", ret);
        ret = c_license_gen_get_token_str(req, sizeof(req), time_ms, NULL);
    }
#else
    ret = c_license_gen_get_token_str(req, sizeof(req), time_ms, NULL);
#endif

    err = qwen_net_map_sdk_err(ret);
    if (err != D_OK) {
        LOGE(TAG, "gen token req failed: %d", err);
        return err;
    }

    LOGI(TAG, "token req ready len=%u", (unsigned)strlen(req));

    rsp = qwen_net_http_post(qwen_net_token_api(), req);
    if (rsp == NULL) {
        return D_ERR_IO;
    }

    err = qwen_net_extract_data(rsp, rsp_data, sizeof(rsp_data));
    if (err != D_OK) {
        qwen_net_log_outer_rsp("token outer rsp", rsp);
        free(rsp);
        LOGE(TAG, "extract token data failed: %d", err);
        return err;
    }

    LOGI(TAG, "token data extracted len=%u", (unsigned)strlen(rsp_data));
    err = qwen_net_map_sdk_err(c_license_analyze_get_token_rsp(rsp_data));
    if (err == D_OK) {
        qwen_net_update_token_refresh_deadline(rsp_data);
        free(rsp);
        LOGI(TAG, "get token done");
    } else {
        qwen_net_log_outer_rsp("token outer rsp", rsp);
        free(rsp);
        LOGE(TAG, "analyze token rsp failed: %d", err);
    }
    return err;
}

d_err_t qwen_net_prepare(qwen_net_event_cb_t event_cb)
{
    d_err_t err;
    uint8_t sdk_inited;

    if (s_qwen_net_cleanup_failed != 0U) return D_ERR_IO;
    if (event_cb == NULL) {
        return D_ERR_INVALID_ARG;
    }

    err = qwen_net_check_config();
    if (err != D_OK) {
        LOGW(TAG, "Qwen 配置未完成");
        return err;
    }

    s_qwen_event_cb = event_cb;
    if (s_qwen_net_prepared != 0U) {
        LOGI(TAG, "already prepared");
        return D_OK;
    }

    sdk_inited = 0U;

    err = qwen_net_map_sdk_err(c_mmi_sdk_init());
    if (err != D_OK) {
        return err;
    }

    sdk_inited = 1U;
    LOGI(TAG, "sdk init done, license=%u", (unsigned)c_license_is_enable());
    err = qwen_net_check_license_ready();
    if (err != D_OK) {
        goto exit;
    }

    err = qwen_net_apply_credentials();
    if (err != D_OK) {
        goto exit;
    }

    err = qwen_net_apply_user_config();
    if (err != D_OK) {
        goto exit;
    }

    s_qwen_net_prepared = 1U;
    LOGI(TAG, "prepare done");
    return D_OK;

exit:
    if (sdk_inited != 0U) {
        s_qwen_net_prepared = 1U;
        if (qwen_net_deinit_checked() != D_OK) return D_ERR_IO;
    }
    s_qwen_event_cb = NULL;
    return err;
}

d_err_t qwen_net_sync_auth(void)
{
    d_err_t err;
    uint8_t is_registered;
    uint8_t token_valid;

    if ((s_qwen_net_prepared == 0U) || s_qwen_net_cleanup_failed) {
        return D_ERR_NOT_READY;
    }

    err = qwen_net_check_license_ready();
    if (err != D_OK) {
        return err;
    }

    is_registered = c_license_device_is_registered();
    if (s_qwen_force_register != 0U) {
        is_registered = 0U;
    }
    token_valid = c_license_is_token_expire((int64_t)d_get_timestamp_ms());
    LOGI(TAG, "sync auth: registered=%u token_valid=%u",
         (unsigned)is_registered, (unsigned)token_valid);

    if (is_registered == 0U) {
        err = qwen_net_do_register();
        if (err != D_OK) {
            return err;
        }
        s_qwen_force_register = 0U;
    }

    return qwen_net_do_get_token();
}

void qwen_net_deinit(void)
{
    (void)qwen_net_deinit_checked();
}

d_err_t qwen_net_deinit_checked(void)
{
    d_err_t result;

    if (s_qwen_net_cleanup_failed) return D_ERR_IO;
    if (s_qwen_net_prepared == 0U) return D_OK;
    result = qwen_net_map_sdk_err(c_mmi_deinit());
    if (result != D_OK) {
        s_qwen_net_cleanup_failed = 1U;
        LOGE(TAG, "SDK cleanup failed: %d; restart required", result);
        return result;
    }
    s_qwen_net_prepared = 0U;
    s_qwen_event_cb = NULL;
    s_qwen_token_refresh_deadline_ms = 0U;
    s_qwen_force_register = 0U;
    return D_OK;
}

int qwen_net_is_registered(void)
{
    if ((s_qwen_net_prepared == 0U) || s_qwen_net_cleanup_failed || (c_license_is_enable() == 0U)) {
        return 0;
    }

    return (int)c_license_device_is_registered();
}

int qwen_net_is_token_valid(void)
{
    if ((s_qwen_net_prepared == 0U) || s_qwen_net_cleanup_failed || (c_license_is_enable() == 0U)) {
        return 0;
    }

    return (int)c_license_is_token_expire((int64_t)d_get_timestamp_ms());
}

int qwen_net_is_token_refresh_needed(void)
{
    uint64_t now_ms;
    uint8_t is_token_valid;

    if ((s_qwen_net_prepared == 0U) || s_qwen_net_cleanup_failed || (c_license_is_enable() == 0U)) {
        return 1;
    }

    now_ms = d_get_timestamp_ms();
    is_token_valid = c_license_is_token_expire((int64_t)now_ms);
    if (is_token_valid == 0U) {
        return 1;
    }

    if (s_qwen_token_refresh_deadline_ms == 0U) {
        return 1;
    }

    return (now_ms >= s_qwen_token_refresh_deadline_ms) ? 1 : 0;
}

const char *qwen_net_get_wss_host(void)
{
    char *value = c_mmi_get_wss_host();
    return (value != NULL) ? value : s_qwen_empty_str;
}

const char *qwen_net_get_wss_port(void)
{
    char *value = c_mmi_get_wss_port();
    return (value != NULL) ? value : s_qwen_empty_str;
}

const char *qwen_net_get_wss_api(void)
{
    char *value = c_mmi_get_wss_api();
    return (value != NULL) ? value : s_qwen_empty_str;
}

const char *qwen_net_get_wss_header(void)
{
    char *value = c_mmi_get_wss_header();
    return (value != NULL) ? value : s_qwen_empty_str;
}

#endif
