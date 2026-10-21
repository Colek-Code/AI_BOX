/**
 * @file    provision_service.c
 * @brief   设备首配服务实现 — HTTP 四步流程
 * @note    状态机：init → claim → poll → finalize
 *          成功 → cred_save_runtime() + EVT_PROVISION_OK
 *          失败 → EVT_PROVISION_FAIL
 */

#include "config/app_config.h"

#if D_DEVICE_CLOUD_ENABLE

#include "provision_service.h"
#include "device_credentials.h"
#include "framework/d_crypto.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "framework/d_error.h"
#include "framework/d_event_payloads.h"
#include "framework/compat/d_stdio_compat.h"
#include "http/http_cli.h"
#include "system/includes.h"

#include <stdlib.h>
#include <string.h>

#define TAG "PROV"

//*********************************************************************************//
//                         Platform error codes                                     //
//*********************************************************************************//

/* Bootstrap / Init */
#define D_PROV_ERR_SIGN_PARAMS_INCOMPLETE   38001
#define D_PROV_ERR_TIMESTAMP_INVALID        38002
#define D_PROV_ERR_TIMESTAMP_EXPIRED        38003
#define D_PROV_ERR_NONCE_REPLAY             38004
#define D_PROV_ERR_NOT_PROVISIONED          38005
#define D_PROV_ERR_DEVICE_DISABLED          38006
#define D_PROV_ERR_ALREADY_ACTIVATED        38007
#define D_PROV_ERR_STATUS_DENY_INIT         38008
#define D_PROV_ERR_PRODUCT_NOT_EXIST        38009
#define D_PROV_ERR_PRODUCT_KEY_MISMATCH     38010
#define D_PROV_ERR_DEVICE_SN_MISMATCH       38011
#define D_PROV_ERR_PRODUCT_DISABLED         38012
#define D_PROV_ERR_VENDOR_NOT_EXIST         38013
#define D_PROV_ERR_VENDOR_DISABLED          38014
#define D_PROV_ERR_CREDENTIAL_NOT_EXIST     38015
#define D_PROV_ERR_TRUST_ANCHOR_NOT_EXIST   38016
#define D_PROV_ERR_SECRET_MISSING           38017
#define D_PROV_ERR_TRUST_MODE_UNSUPPORTED   38018
#define D_PROV_ERR_INIT_SIGN_INVALID        38019
/* Claim */
#define D_PROV_ERR_TENANT_REQUIRED          38020
#define D_PROV_ERR_TENANT_MISMATCH          38021
#define D_PROV_ERR_SESSION_ID_REQUIRED      38022
#define D_PROV_ERR_SESSION_NOT_EXIST        38023
#define D_PROV_ERR_SESSION_EXPIRED          38024
#define D_PROV_ERR_SESSION_NOT_CLAIMABLE    38025
#define D_PROV_ERR_TICKET_REQUIRED          38026
#define D_PROV_ERR_TICKET_NOT_EXIST         38027
#define D_PROV_ERR_TICKET_DEVICE_MISMATCH   38028
#define D_PROV_ERR_TICKET_PRODUCT_MISMATCH  38029
#define D_PROV_ERR_TICKET_NOT_AVAILABLE     38030
#define D_PROV_ERR_TICKET_STATUS_INVALID    38031
#define D_PROV_ERR_TICKET_TENANT_INVALID    38032
#define D_PROV_ERR_CLAIMED_BY_OTHER         38033
#define D_PROV_ERR_SESSION_CLAIMED_OTHER    38034
#define D_PROV_ERR_NO_LICENSE               38035
#define D_PROV_ERR_LICENSE_EXHAUSTED        38036
/* Finalize */
#define D_PROV_ERR_FINALIZE_SIGN_INVALID    38037
#define D_PROV_ERR_NOT_CLAIMED              38038
#define D_PROV_ERR_SESSION_NO_TENANT        38039
#define D_PROV_ERR_LICENSE_NOT_EXIST        38040
#define D_PROV_ERR_RUNTIME_KEY_GEN_FAIL     38041
/* WS Handshake */
#define D_WS_ERR_PARAMS_INCOMPLETE          38101
#define D_WS_ERR_DEVICE_NOT_EXIST           38102
#define D_WS_ERR_DEVICE_NOT_ACTIVATED       38103
#define D_WS_ERR_DEVICE_DISABLED            38104
#define D_WS_ERR_RUNTIME_CRED_NOT_EXIST     38105
#define D_WS_ERR_TIMESTAMP_INVALID          38106
#define D_WS_ERR_TIMESTAMP_EXPIRED          38107
#define D_WS_ERR_NONCE_REPLAY               38108
#define D_WS_ERR_SECRET_MISSING             38109
#define D_WS_ERR_SIGN_INVALID               38110

//*********************************************************************************//
//                         Error-code diagnostics                                   //
//*********************************************************************************//

/* Map platform error codes to short English hints for log readability.
 * 将平台错误码映射为简短英文提示，方便日志快速定位。 */
static const char *prov_err_hint(int code)
{
    switch (code) {
    /* ── Bootstrap / Init ── */
    case D_PROV_ERR_SIGN_PARAMS_INCOMPLETE:  return "sign params incomplete (check deviceId/timestamp/nonce)"; /* 签名参数缺失 */
    case D_PROV_ERR_TIMESTAMP_INVALID:       return "timestamp format invalid";                                 /* 时间戳格式错误 */
    case D_PROV_ERR_TIMESTAMP_EXPIRED:       return "timestamp expired (check NTP sync)";                       /* 时间戳过期，检查NTP */
    case D_PROV_ERR_NONCE_REPLAY:            return "nonce replay detected (refresh random seed)";              /* nonce重放 */
    case D_PROV_ERR_NOT_PROVISIONED:         return "device not pre-provisioned on platform";                   /* 设备未在平台建档 */
    case D_PROV_ERR_DEVICE_DISABLED:         return "device is disabled";                                       /* 设备已禁用 */
    case D_PROV_ERR_ALREADY_ACTIVATED:       return "device already activated, skip provisioning -> WS";        /* 设备已激活 */
    case D_PROV_ERR_STATUS_DENY_INIT:        return "device status does not allow bootstrap init";              /* 设备状态不允许init */
    case D_PROV_ERR_PRODUCT_NOT_EXIST:       return "product does not exist (check productKey)";                /* 产品不存在 */
    case D_PROV_ERR_PRODUCT_KEY_MISMATCH:    return "productKey mismatch (check config/deployment_config.h)"; /* productKey不匹配 */
    case D_PROV_ERR_DEVICE_SN_MISMATCH:      return "deviceSn mismatch (check config/deployment_config.h)";   /* deviceSn不匹配 */
    case D_PROV_ERR_PRODUCT_DISABLED:        return "product is disabled";                                      /* 产品已禁用 */
    case D_PROV_ERR_VENDOR_NOT_EXIST:        return "vendor does not exist";                                    /* 厂商不存在 */
    case D_PROV_ERR_VENDOR_DISABLED:         return "vendor is disabled";                                       /* 厂商已禁用 */
    case D_PROV_ERR_CREDENTIAL_NOT_EXIST:    return "device credential not exist (check factorySecret)";        /* 初始化凭证不存在 */
    case D_PROV_ERR_TRUST_ANCHOR_NOT_EXIST:  return "vendor trust anchor not exist";                            /* 厂商信任锚不存在 */
    case D_PROV_ERR_SECRET_MISSING:          return "credential secret is missing";                             /* 凭证secret缺失 */
    case D_PROV_ERR_TRUST_MODE_UNSUPPORTED:  return "unsupported vendor trust mode";                            /* 不支持的信任模式 */
    case D_PROV_ERR_INIT_SIGN_INVALID:       return "init signature invalid (verify factorySecret)";            /* init验签失败 */
    /* ── Claim ── */
    case D_PROV_ERR_TENANT_REQUIRED:         return "tenantId is required";                                     /* 未传tenantId */
    case D_PROV_ERR_TENANT_MISMATCH:         return "tenantId does not match claimTicket";                      /* 租户与票据不一致 */
    case D_PROV_ERR_SESSION_ID_REQUIRED:     return "bootstrapId or pairingCode is required";                   /* 未传会话标识 */
    case D_PROV_ERR_SESSION_NOT_EXIST:       return "bootstrap session not exist (re-init)";                    /* 会话不存在 */
    case D_PROV_ERR_SESSION_EXPIRED:         return "bootstrap session expired 10min (re-init)";                /* 会话过期 */
    case D_PROV_ERR_SESSION_NOT_CLAIMABLE:   return "session state does not allow claim";                       /* 会话状态不允许认领 */
    case D_PROV_ERR_TICKET_REQUIRED:         return "claimTicket is required";                                  /* 未传claimTicket */
    case D_PROV_ERR_TICKET_NOT_EXIST:        return "claimTicket not exist (check backend)";                    /* 票据不存在 */
    case D_PROV_ERR_TICKET_DEVICE_MISMATCH:  return "claimTicket does not match device";                        /* 票据与设备不匹配 */
    case D_PROV_ERR_TICKET_PRODUCT_MISMATCH: return "claimTicket does not match product";                       /* 票据与产品不匹配 */
    case D_PROV_ERR_TICKET_NOT_AVAILABLE:    return "claimTicket is not available";                             /* 票据不可用 */
    case D_PROV_ERR_TICKET_STATUS_INVALID:   return "claimTicket status invalid (used or revoked)";             /* 票据状态无效 */
    case D_PROV_ERR_TICKET_TENANT_INVALID:   return "claimTicket tenant is invalid";                            /* 票据租户无效 */
    case D_PROV_ERR_CLAIMED_BY_OTHER:        return "device already claimed by another tenant";                  /* 被其他租户认领 */
    case D_PROV_ERR_SESSION_CLAIMED_OTHER:   return "session already claimed by another tenant";                 /* 会话被其他租户认领 */
    case D_PROV_ERR_NO_LICENSE:              return "no active device license (contact admin)";                  /* 无有效license */
    case D_PROV_ERR_LICENSE_EXHAUSTED:       return "device license quota exhausted";                           /* license配额耗尽 */
    /* ── Finalize ── */
    case D_PROV_ERR_FINALIZE_SIGN_INVALID:   return "finalize signature invalid (verify factorySecret)";        /* finalize验签失败 */
    case D_PROV_ERR_NOT_CLAIMED:             return "device not claimed yet (run claim first)";                  /* 未完成认领 */
    case D_PROV_ERR_SESSION_NO_TENANT:       return "session missing claim tenant";                              /* 会话缺少认领租户 */
    case D_PROV_ERR_LICENSE_NOT_EXIST:       return "device license not exist";                                  /* license不存在 */
    case D_PROV_ERR_RUNTIME_KEY_GEN_FAIL:    return "server failed to generate runtime key (check platform log)"; /* 服务端生成Key失败 */
    /* ── WS Handshake ── */
    case D_WS_ERR_PARAMS_INCOMPLETE:         return "WS handshake params incomplete";                           /* WS握手参数缺失 */
    case D_WS_ERR_DEVICE_NOT_EXIST:          return "WS device not exist";                                      /* WS设备不存在 */
    case D_WS_ERR_DEVICE_NOT_ACTIVATED:      return "WS device not activated (complete provisioning first)";    /* WS设备未激活 */
    case D_WS_ERR_DEVICE_DISABLED:           return "WS device is disabled";                                    /* WS设备已禁用 */
    case D_WS_ERR_RUNTIME_CRED_NOT_EXIST:    return "WS runtime credential expired (re-provision)";             /* WS运行时凭证失效 */
    case D_WS_ERR_TIMESTAMP_INVALID:         return "WS timestamp format invalid";                              /* WS时间戳格式错误 */
    case D_WS_ERR_TIMESTAMP_EXPIRED:         return "WS timestamp expired (check NTP)";                         /* WS时间戳过期 */
    case D_WS_ERR_NONCE_REPLAY:              return "WS nonce replay detected";                                  /* WS nonce重放 */
    case D_WS_ERR_SECRET_MISSING:            return "WS runtime secret is missing";                             /* WS运行时密钥缺失 */
    case D_WS_ERR_SIGN_INVALID:              return "WS HMAC signature invalid (verify accessSecret)";          /* WS验签失败 */
    default:                                 return "unknown error code";                                        /* 未知错误码 */
    }
}

/* 前向声明：简易 JSON 提取（定义在后方） */
static int json_extract_str(const char *json, const char *key, char *out, int out_size);
static int json_extract_int(const char *json, const char *key, int *out);

/* Log a failed API call: error code + platform msg + diagnostic hint.
 * 统一打印API失败日志：错误码 + 平台msg + 诊断提示。 */
static void prov_log_api_error(const char *step, const char *resp_buf)
{
    int  code = 0;
    char msg[128] = {0};
    json_extract_int(resp_buf, "code", &code);
    json_extract_str(resp_buf, "msg",  msg, sizeof(msg));
    LOGE(TAG, "%s failed [%d] %s | %s", step, code, msg, prov_err_hint(code));
}


//*********************************************************************************//
//                              HTTP utils / paths                                  //
//*********************************************************************************//

/* HTTP timeout and polling config */

#define PROV_HTTP_TIMEOUT_MS    10000
#define PROV_POLL_INTERVAL_MS   2000
#define PROV_POLL_MAX_TIMES     30       /* [B5] 2s * 30 = 60s 轮询上限（原15次30s偏短） */
#define PROV_RESP_BUF_SIZE      2048



//*********************************************************************************//
//                              URL paths                                           //
//*********************************************************************************//

#define PATH_BOOTSTRAP_INIT     "/device/open/bootstrap/init"
#define PATH_CLAIM              "/device/open/claim"
#define PATH_CLAIM_STATUS       "/device/open/claim/status"
#define PATH_BOOTSTRAP_FINALIZE "/device/open/bootstrap/finalize"

//*********************************************************************************//
//                              Internal state                                      //
//*********************************************************************************//


static volatile int s_busy = 0;
static volatile int s_ntp_ready = 0;
static char s_resp_buf[PROV_RESP_BUF_SIZE];
static int  s_resp_len = 0;

/* bootstrap session */
static char s_bootstrap_id[128];

//*********************************************************************************//
//                              简易 JSON 提取                                     //
//*********************************************************************************//

/* 从 JSON 中提取字符串值（不支持嵌套、转义等复杂场景） */
static int json_extract_str(const char *json, const char *key, char *out, int out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;

    const char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return -1;

    /* 跳过空白 */
    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    if (*p == '"') {
        p++;
        int i = 0;
        while (*p && *p != '"' && i < out_size - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return i;
    }
    return -1;
}

/* 从 JSON 中提取整数值 */
static int json_extract_int(const char *json, const char *key, int *out)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;

    const char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return -1;

    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t') p++;

    *out = atoi(p);
    return 0;
}

static runtime_provider_e prov_parse_runtime_provider(const char *json)
{
    char runtime_provider[32] = {0};

    if (json_extract_str(json, "runtimeProvider", runtime_provider, sizeof(runtime_provider)) > 0) {
        if (strcmp(runtime_provider, "ALIYUN_MMI_RTOS") == 0) {
            return RUNTIME_PROVIDER_ALIYUN_MMI_RTOS;
        }
        if (strcmp(runtime_provider, "PLATFORM_WS") == 0) {
            return RUNTIME_PROVIDER_PLATFORM_WS;
        }
    }

    if ((json != NULL) &&
        ((strstr(json, "\"registerPath\"") != NULL) ||
         (strstr(json, "\"getTokenPath\"") != NULL))) {
        return RUNTIME_PROVIDER_ALIYUN_MMI_RTOS;
    }

    return RUNTIME_PROVIDER_PLATFORM_WS;
}

static int prov_parse_control_host_port(const char *url, char *host, int host_size,
                                        char *port, int port_size)
{
    const char *host_start;
    const char *host_end;
    const char *port_start;
    const char *port_end;
    int host_len;
    int port_len;

    if ((url == NULL) || (url[0] == '\0') || (host == NULL) || (port == NULL) ||
        (host_size <= 1) || (port_size <= 1)) {
        return -1;
    }

    memset(host, 0, host_size);
    memset(port, 0, port_size);
    host_start = url;
    if (strncmp(host_start, "https://", 8) == 0) {
        host_start += 8;
        strncpy(port, "443", port_size - 1);
    } else if (strncmp(host_start, "http://", 7) == 0) {
        host_start += 7;
        strncpy(port, "80", port_size - 1);
    } else {
        strncpy(port, "443", port_size - 1);
    }
    port[port_size - 1] = '\0';

    host_end = host_start;
    while ((*host_end != '\0') && (*host_end != '/') && (*host_end != ':')) {
        host_end++;
    }

    host_len = (int)(host_end - host_start);
    if ((host_len <= 0) || (host_len >= host_size)) {
        return -1;
    }

    memcpy(host, host_start, (size_t)host_len);
    host[host_len] = '\0';

    if (*host_end != ':') {
        return 0;
    }

    port_start = host_end + 1;
    port_end = port_start;
    while ((*port_end != '\0') && (*port_end != '/')) {
        port_end++;
    }

    port_len = (int)(port_end - port_start);
    if ((port_len <= 0) || (port_len >= port_size)) {
        return -1;
    }

    memcpy(port, port_start, (size_t)port_len);
    port[port_len] = '\0';
    return 0;
}

static int json_escape_str(const char *src, char *dst, int dst_size)
{
    int i = 0;
    int j = 0;

    if (!src || !dst || dst_size <= 0) {
        return -1;
    }

    while (src[i] && j < dst_size - 2) {
        char c = src[i++];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r') {
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (c == '\t') {
            dst[j++] = '\\';
            dst[j++] = 't';
        } else {
            dst[j++] = c;
        }
    }

    dst[j] = '\0';
    return j;
}

/* 检查 JSON 响应是否成功：平台以 code==200 表示成功 */
static int json_check_success(const char *json)
{
    int code = -1;
    if (json_extract_int(json, "code", &code) == 0) {
        return (code == 200);
    }
    /* 兜底：若 code 字段不存在则视为失败 */
    return 0;
}

//*********************************************************************************//
//                              HTTP 工具                                           //
//*********************************************************************************//

static int prov_http_resp_cb(void *ctx, void *buf, unsigned int size, void *priv, httpin_status status)
{
    switch (status) {
    case HTTPIN_HEADER:
        s_resp_len = 0;
        break;
    case HTTPIN_PROGRESS:
        if (s_resp_len + (int)size < PROV_RESP_BUF_SIZE - 1) {
            memcpy(s_resp_buf + s_resp_len, buf, size);
            s_resp_len += size;
            s_resp_buf[s_resp_len] = '\0';
        }
        break;
    case HTTPIN_FINISHED:
        break;
    case HTTPIN_ERROR:
    case HTTPIN_ABORT:
        return -1;
    default:
        break;
    }
    return 0;
}

static int prov_http_post(const char *path, const char *body)
{
    httpcli_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    char url[256];
    snprintf(url, sizeof(url), "%s%s", D_CLOUD_BASE_URL, path);

    int body_len = (int)strlen(body);
    char header[512];
    snprintf(header, sizeof(header),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n\r\n",
             path,
             /* 动态跳过协议前缀，兼容 http:// 和 https:// */
             strncmp(D_CLOUD_BASE_URL, "https://", 8) == 0
                 ? D_CLOUD_BASE_URL + 8
                 : D_CLOUD_BASE_URL + 7,
             body_len);

    s_resp_len = 0;
    memset(s_resp_buf, 0, sizeof(s_resp_buf));

    ctx.url = url;
    ctx.user_http_header = header;
    ctx.post_data = body;
    ctx.data_len = body_len;
    ctx.timeout_millsec = PROV_HTTP_TIMEOUT_MS;
    ctx.cb = prov_http_resp_cb;

    httpin_error err = httpcli_post(&ctx);
    if (err != HERROR_OK) {
        LOGE(TAG, "HTTP POST %s 失败: %d", path, err);
        return -1;
    }

    LOGD(TAG, "HTTP 响应 (%d bytes): %.200s", s_resp_len, s_resp_buf);
    return s_resp_len;
}

static int prov_http_get(const char *path, const char *query)
{
    httpcli_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    char url[512];
    snprintf(url, sizeof(url), "%s%s?%s", D_CLOUD_BASE_URL, path, query);

    s_resp_len = 0;
    memset(s_resp_buf, 0, sizeof(s_resp_buf));

    ctx.url = url;
    ctx.timeout_millsec = PROV_HTTP_TIMEOUT_MS;
    ctx.cb = prov_http_resp_cb;

    httpin_error err = httpcli_get(&ctx);
    if (err != HERROR_OK) {
        LOGE(TAG, "HTTP GET %s 失败: %d", path, err);
        return -1;
    }

    LOGD(TAG, "HTTP 响应 (%d bytes): %.200s", s_resp_len, s_resp_buf);
    return s_resp_len;
}

//*********************************************************************************//
//                              签名构建                                            //
//*********************************************************************************//

static d_err_t build_init_sign(const device_cred_t *cred,
                               const char *timestamp, const char *nonce,
                               char *sign_hex, size_t sign_capacity)
{
    if (!sign_hex || sign_capacity < 65U) return D_ERR_INVALID_ARG;
    char canonical[512];
    const char *parts[] = {
        cred->device_id, cred->product_key, cred->device_sn, timestamp, nonce
    };
    int len = d_build_canonical(parts, 5, canonical, sizeof(canonical));
    if (len < 0) return D_ERR_INVALID_ARG;

#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
    return d_rsa_sha256_sign(cred->device_pkey_pem,
                             (const unsigned char *)canonical, len, sign_hex, sign_capacity);
#else
    return d_hmac_sha256((const unsigned char *)cred->factory_secret,
                         (int)strlen(cred->factory_secret),
                         (const unsigned char *)canonical, len, sign_hex);
#endif
}

static d_err_t build_finalize_sign(const device_cred_t *cred,
                                   const char *timestamp, const char *nonce,
                                   char *sign_hex, size_t sign_capacity)
{
    if (!sign_hex || sign_capacity < 65U) return D_ERR_INVALID_ARG;
    char canonical[512];
    const char *parts[] = {
        cred->device_id, s_bootstrap_id, timestamp, nonce
    };
    int len = d_build_canonical(parts, 4, canonical, sizeof(canonical));
    if (len < 0) return D_ERR_INVALID_ARG;

#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
    return d_rsa_sha256_sign(cred->device_pkey_pem,
                             (const unsigned char *)canonical, len, sign_hex, sign_capacity);
#else
    return d_hmac_sha256((const unsigned char *)cred->factory_secret,
                         (int)strlen(cred->factory_secret),
                         (const unsigned char *)canonical, len, sign_hex);
#endif
}

//*********************************************************************************//
//                              四步首配流程                                         //
//*********************************************************************************//

/* Step 1: bootstrap/init */
static int prov_step_init(const device_cred_t *cred)
{
    char ts[32], nonce[33], sign[513];
    snprintf(ts, sizeof(ts), "%llu", d_get_timestamp_ms());
    d_generate_nonce(nonce, sizeof(nonce));

    d_err_t err = build_init_sign(cred, ts, nonce, sign, sizeof(sign));
    if (err != D_OK) {
        LOGE(TAG, "init 签名失败");
        return -1;
    }

    char body[2048];
#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
    char cert_json[1024];
    if (cred->device_cert_pem[0] == '\0') {
        LOGE(TAG, "device_cert 模式缺少 deviceCertPem");
        return -1;
    }
    json_escape_str(cred->device_cert_pem, cert_json, sizeof(cert_json));
#endif
    snprintf(body, sizeof(body),
             "{\"deviceId\":\"%s\",\"productKey\":\"%s\",\"deviceSn\":\"%s\","
             "\"firmwareVersion\":\"%s\",\"timestamp\":\"%s\",\"nonce\":\"%s\","
             "\"sign\":\"%s\""
#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
             ",\"deviceCertPem\":\"%s\""
             ",\"signAlgorithm\":\"%s\""
#endif
             "}",
             cred->device_id, cred->product_key, cred->device_sn,
             D_FIRMWARE_VERSION, ts, nonce, sign
#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
             , cert_json, D_SIGN_ALGORITHM
#endif
             );

    int ret = prov_http_post(PATH_BOOTSTRAP_INIT, body);
    if (ret <= 0) return -1;

    if (!json_check_success(s_resp_buf)) {
        /* 使用平台错误码判断已激活状态（38007 = device already activated） */
        int err_code = 0;
        json_extract_int(s_resp_buf, "code", &err_code);
        if (err_code == D_PROV_ERR_ALREADY_ACTIVATED) {
            if (cred_is_activated()) {
                LOGI(TAG, "设备已激活，跳过首配 (code=%d)", err_code);
                return 1;
            }
            LOGE(TAG, "设备已激活但本地缺少运行时凭证 (code=%d)", err_code);
            return 2;
        }
        prov_log_api_error("init", s_resp_buf);
        return -1;
    }

    /* 提取 bootstrapId */
    if (json_extract_str(s_resp_buf, "bootstrapId", s_bootstrap_id, sizeof(s_bootstrap_id)) <= 0) {
        LOGE(TAG, "init 响应缺少 bootstrapId");
        return -1;
    }

    LOGI(TAG, "Step1 init 成功: bootstrapId=%s", s_bootstrap_id);
    return 0;
}

/* Step 2: claim */
static int prov_step_claim(const device_cred_t *cred)
{
    char body[512];
    snprintf(body, sizeof(body),
             "{\"deviceId\":\"%s\",\"bootstrapId\":\"%s\","
             "\"claimTicket\":\"%s\",\"tenantId\":\"%s\"}",
             cred->device_id, s_bootstrap_id,
             cred->claim_ticket, cred->tenant_id);

    int ret = prov_http_post(PATH_CLAIM, body);
    if (ret <= 0) return -1;

    if (!json_check_success(s_resp_buf)) {
        prov_log_api_error("claim", s_resp_buf);
        return -1;
    }

    /* 检查 claim 响应中的 claimStatus：若已直接返回 "claimed"，无需轮询 */
    char cs[32] = {0};
    json_extract_str(s_resp_buf, "claimStatus", cs, sizeof(cs));
    if (strcmp(cs, "claimed") == 0 || strcmp(cs, "CLAIMED") == 0) {
        LOGI(TAG, "Step2 claim 成功 (已直接确认，无需轮询)");
        return 1;   /* 1 = 无需轮询 */
    }

    LOGI(TAG, "Step2 claim 成功 (需轮询确认)");
    return 0;   /* 0 = 需轮询 */
}

/* Step 2.5: poll claim status（仅当 prov_step_claim 返回 0 时调用） */
static int prov_step_poll_status(const device_cred_t *cred)
{
    char query[256];
    snprintf(query, sizeof(query), "deviceId=%s&bootstrapId=%s&claimTicket=%s",
             cred->device_id, s_bootstrap_id, cred->claim_ticket);

    int i;
    int consecutive_fails = 0;          /* 连续失败计数，连续 3 次则放弃 */
    for (i = 0; i < PROV_POLL_MAX_TIMES; i++) {
        int ret = prov_http_get(PATH_CLAIM_STATUS, query);
        if (ret <= 0) {
            consecutive_fails++;
            LOGE(TAG, "claim/status GET 失败 (ret=%d, 连续 %d 次)", ret, consecutive_fails);
            if (consecutive_fails >= 3) {
                LOGE(TAG, "claim/status 连续失败 %d 次，放弃轮询", consecutive_fails);
                return -1;
            }
            os_time_dly(PROV_POLL_INTERVAL_MS / 10);
            continue;
        }
        consecutive_fails = 0;  /* 成功请求，重置计数 */

        char status_str[32] = {0};
        if (json_extract_str(s_resp_buf, "claimStatus", status_str, sizeof(status_str)) <= 0) {
            json_extract_str(s_resp_buf, "status", status_str, sizeof(status_str));
        }

        if (strcmp(status_str, "claimed") == 0 || strcmp(status_str, "CLAIMED") == 0) {
            LOGI(TAG, "Step2.5 claim 已确认 (poll %d)", i + 1);
            return 0;
        }

        LOGD(TAG, "claim status: %s, 等待... (%d/%d)", status_str, i + 1, PROV_POLL_MAX_TIMES);
        os_time_dly(PROV_POLL_INTERVAL_MS / 10);
    }

    LOGW(TAG, "claim status 轮询超时");
    return -1;
}

/* Step 3: bootstrap/finalize */
static int prov_step_finalize(const device_cred_t *cred)
{
    char ts[32], nonce[33], sign[513];
    snprintf(ts, sizeof(ts), "%llu", d_get_timestamp_ms());
    d_generate_nonce(nonce, sizeof(nonce));

    d_err_t err = build_finalize_sign(cred, ts, nonce, sign, sizeof(sign));
    if (err != D_OK) {
        LOGE(TAG, "finalize 签名失败");
        return -1;
    }

    char body[2048];
#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
    char cert_json[1024];
    if (cred->device_cert_pem[0] == '\0') {
        LOGE(TAG, "device_cert 模式缺少 deviceCertPem");
        return -1;
    }
    json_escape_str(cred->device_cert_pem, cert_json, sizeof(cert_json));
#endif
    snprintf(body, sizeof(body),
             "{\"deviceId\":\"%s\",\"bootstrapId\":\"%s\","
             "\"timestamp\":\"%s\",\"nonce\":\"%s\",\"sign\":\"%s\""
#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
             ",\"deviceCertPem\":\"%s\""
             ",\"signAlgorithm\":\"%s\""
#endif
             "}",
             cred->device_id, s_bootstrap_id, ts, nonce, sign
#if (D_PROVISION_AUTH_MODE == D_PROVISION_AUTH_DEVICE_CERT)
             , cert_json, D_SIGN_ALGORITHM
#endif
             );

    int ret = prov_http_post(PATH_BOOTSTRAP_FINALIZE, body);
    if (ret <= 0) return -1;

    if (!json_check_success(s_resp_buf)) {
        prov_log_api_error("finalize", s_resp_buf);
        return -1;
    }

    /* 提取运行时凭证 */
    char access_key[CRED_STR_SHORT]    = {0};
    char access_secret[CRED_STR_SHORT]  = {0};
    char ws_endpoint[CRED_STR_URL]      = {0};
    char default_agent_id[32]           = {0};
    char control_host_raw[CRED_STR_URL] = {0};
    char control_host[CRED_STR_URL]     = {0};
    char control_port[CRED_STR_PORT]    = {0};
    char control_register_path[CRED_STR_URL] = {0};
    char control_token_path[CRED_STR_URL]    = {0};
    char runtime_app_id[CRED_STR_SHORT]      = {0};
    char runtime_device_name[CRED_STR_SHORT] = {0};
    int  secret_version = 0;
    int  hb_interval    = D_WS_HEARTBEAT_INTERVAL_S;
    int  default_agent_id_num = 0;
    runtime_provider_e runtime_provider;

    json_extract_str(s_resp_buf, "accessKey",    access_key,    sizeof(access_key));
    json_extract_str(s_resp_buf, "accessSecret", access_secret, sizeof(access_secret));
    json_extract_int(s_resp_buf, "secretVersion", &secret_version);
    json_extract_str(s_resp_buf, "wsEndpoint",   ws_endpoint,   sizeof(ws_endpoint));
    if (json_extract_str(s_resp_buf, "defaultAgentProfileId", default_agent_id, sizeof(default_agent_id)) <= 0) {
        if ((json_extract_int(s_resp_buf, "defaultAgentProfileId", &default_agent_id_num) == 0) &&
            (default_agent_id_num > 0)) {
            snprintf(default_agent_id, sizeof(default_agent_id), "%d", default_agent_id_num);
        }
    }
    /* heartbeatIntervalSec 可能以字符串 "30" 形式返回，兼容两种格式 */
    {
        char hb_str[16] = {0};
        if (json_extract_str(s_resp_buf, "heartbeatIntervalSec", hb_str, sizeof(hb_str)) > 0) {
            int v = atoi(hb_str);
            if (v > 0) hb_interval = v;
        } else {
            json_extract_int(s_resp_buf, "heartbeatIntervalSec", &hb_interval);
        }
        if (hb_interval <= 0) hb_interval = D_WS_HEARTBEAT_INTERVAL_S;
    }

    runtime_provider = prov_parse_runtime_provider(s_resp_buf);
    if (runtime_provider == RUNTIME_PROVIDER_ALIYUN_MMI_RTOS) {
        json_extract_str(s_resp_buf, "host", control_host_raw, sizeof(control_host_raw));
        json_extract_str(s_resp_buf, "registerPath", control_register_path, sizeof(control_register_path));
        json_extract_str(s_resp_buf, "getTokenPath", control_token_path, sizeof(control_token_path));
        json_extract_str(s_resp_buf, "appId", runtime_app_id, sizeof(runtime_app_id));
        json_extract_str(s_resp_buf, "deviceName", runtime_device_name, sizeof(runtime_device_name));
        if ((prov_parse_control_host_port(control_host_raw, control_host, sizeof(control_host),
                                          control_port, sizeof(control_port)) != 0) ||
            (control_register_path[0] == '\0') ||
            (control_token_path[0] == '\0')) {
            LOGE(TAG, "finalize 响应缺少有效的 aliyunRtosConfig");
            return -1;
        }
    }

    if (access_key[0] == '\0' || access_secret[0] == '\0') {
        LOGE(TAG, "finalize 响应缺少运行时凭证 (accessKey 或 accessSecret 为空)");
        return -1;
    }

    /* 写入 VM */
    runtime_cred_t rt = {
        .access_key       = access_key,
        .access_secret    = access_secret,
        .secret_version   = secret_version,
        .ws_endpoint      = ws_endpoint[0] ? ws_endpoint : NULL,
        .hb_interval_sec  = hb_interval,
        .default_agent_id = default_agent_id[0] ? default_agent_id : NULL,
        .runtime_provider = runtime_provider,
        .control_host = control_host[0] ? control_host : NULL,
        .control_port = control_port[0] ? control_port : NULL,
        .control_register_path = control_register_path[0] ? control_register_path : NULL,
        .control_token_path = control_token_path[0] ? control_token_path : NULL,
        .runtime_app_id = runtime_app_id[0] ? runtime_app_id : NULL,
        .runtime_device_name = runtime_device_name[0] ? runtime_device_name : NULL,
    };

    err = cred_save_runtime(&rt);
    if (err != D_OK) {
        LOGE(TAG, "凭证写入 VM 失败");
        return -1;
    }

    LOGI(TAG, "Step3 finalize 成功: accessKey=%s, ver=%d, agentId=%s",
         access_key, secret_version,
         default_agent_id[0] ? default_agent_id : "(none)");
    return 0;
}

//*********************************************************************************//
//                              首配主任务                                          //
//*********************************************************************************//

static void provision_task(void *priv)
{
    LOGI(TAG, "首配流程开始");

    const device_cred_t *cred = cred_get();
    d_error_info_t err_info = {0};
    int ret;

    /* Step 1: init */
    ret = prov_step_init(cred);
    if (ret == 1) {
        /* 已激活，直接成功 */
        d_event_post(EVT_PROVISION_OK, NULL, 0);
        goto done;
    }
    if (ret == 2) {
        err_info.code = 5;
        err_info.msg  = "device already activated but local runtime credential missing";
        goto fail;
    }
    if (ret < 0) {
        err_info.code = 1;
        err_info.msg  = "bootstrap init failed";
        goto fail;
    }

    /* Step 2: claim */
    ret = prov_step_claim(cred);
    if (ret < 0) {
        /* Session expired: automatically re-run init then claim.
         * 会话超时：自动重新 init→claim */
        int claim_err = 0;
        json_extract_int(s_resp_buf, "code", &claim_err);
        if (claim_err == D_PROV_ERR_SESSION_EXPIRED) {
            LOGW(TAG, "Session expired (code=%d), re-running init", claim_err);
            ret = prov_step_init(cred);
            /* Handle all prov_step_init return values in the retry path */
            if (ret == 1) {
                /* Re-init says already activated with valid local creds -> done */
                d_event_post(EVT_PROVISION_OK, NULL, 0);
                goto done;
            }
            if (ret == 2) {
                err_info.code = 5;
                err_info.msg  = "device already activated but local runtime credential missing (re-init)";
                goto fail;
            }
            if (ret == 0) {
                ret = prov_step_claim(cred);
            }
        }
        if (ret < 0) {
            err_info.code = 2;
            err_info.msg  = "claim failed";
            goto fail;
        }
    }

    /* Step 2.5: poll（若 Step2 已直接确认 claimStatus="claimed"，跳过） */
    if (ret == 0) {   /* ret==0 表示需轮询；ret==1 表示无需轮询 */
        ret = prov_step_poll_status(cred);
        if (ret < 0) {
            err_info.code = 3;
            err_info.msg  = "claim status poll timeout";
            goto fail;
        }
    } else {
        LOGI(TAG, "Step2.5 跳过（claim 响应已确认）");
    }

    /* Step 3: finalize */
    ret = prov_step_finalize(cred);
    if (ret < 0) {
        err_info.code = 4;
        err_info.msg  = "bootstrap finalize failed";
        goto fail;
    }

    /* 首配成功 */
    LOGI(TAG, "首配流程完成");
    d_event_post(EVT_PROVISION_OK, NULL, 0);
    goto done;

fail:
    LOGE(TAG, "首配失败: step=%d, %s", err_info.code, err_info.msg);
    d_event_post(EVT_PROVISION_FAIL, &err_info, sizeof(err_info));

done:
    s_busy = 0;
}

//*********************************************************************************//
//                              事件回调                                            //
//*********************************************************************************//

static void on_ntp_synced(const d_event_t *evt, void *ctx)
{
    (void)ctx;
    if (!evt || evt->id != EVT_NTP_SYNCED) return;
    s_ntp_ready = 1;
    LOGI(TAG, "NTP 就绪，启动首配");
    provision_start();
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

d_err_t provision_service_init(void)
{
    LOGI(TAG, "首配服务初始化");

    /* 订阅 NTP 校时完成事件 */
    if (d_event_subscribe(EVT_NTP_SYNCED, on_ntp_synced, NULL) != 0) {
        LOGE(TAG, "NTP 事件订阅失败");
        return D_ERR_IO;
    }

    return D_OK;
}

d_err_t provision_start(void)
{
    int ret;

    if (s_busy) {
        LOGW(TAG, "首配正在进行中");
        return D_ERR_BUSY;
    }

    if (cred_is_activated()) {
        LOGI(TAG, "设备已激活，跳过首配");
        d_event_post(EVT_PROVISION_OK, NULL, 0);
        return D_OK;
    }

    s_busy = 1;
    /* 改用 thread_fork，与架构其他模块保持一致，优先级语义统一 */
    ret = thread_fork("prov_task", 10, 4096, 0, NULL, provision_task, NULL);
    if (ret != OS_NO_ERR) {
        s_busy = 0;
        LOGE(TAG, "prov_task 创建失败: %d", ret);
        return D_ERR_IO;
    }

    return D_OK;
}

#endif /* D_DEVICE_CLOUD_ENABLE */
