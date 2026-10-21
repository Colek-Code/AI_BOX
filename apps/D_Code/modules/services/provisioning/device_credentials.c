/**
 * @file    device_credentials.c
 * @brief   设备凭证管理 — VM 持久化读写实现
 * @note    依赖：syscfg（SDK）、config/deployment_config.h（默认值）
 *          不依赖任何业务模块
 */

#include "config/app_config.h"

#if D_DEVICE_CLOUD_ENABLE

#include "system/includes.h"
#include "device_credentials.h"
#include "framework/d_log.h"
#include "syscfg/syscfg_id.h"
#include <string.h>

#define TAG "CRED"

//*********************************************************************************//
//                              内存缓存                                            //
//*********************************************************************************//

static device_cred_t s_cred;
static int s_initialized = 0;

//*********************************************************************************//
//                              内部工具                                            //
//*********************************************************************************//

/* 从 VM 读字符串，失败则用默认值填充 */
static void vm_read_str(u16 id, char *buf, int buf_size, const char *def_val)
{
    memset(buf, 0, buf_size);
    int ret = syscfg_read(id, buf, (u16)(buf_size - 1));
    if (ret <= 0 || buf[0] == '\0' || buf[0] == (char)0xFF) {
        /* VM 为空或读取失败，使用默认值 */
        if (def_val) {
            strncpy(buf, def_val, buf_size - 1);
            buf[buf_size - 1] = '\0';
        }
    }
}

/* 从 VM 读 int，失败则用默认值 */
static int vm_read_int(u16 id, int def_val)
{
    int val = 0;
    int ret = syscfg_read(id, &val, sizeof(val));
    if (ret <= 0) {
        return def_val;
    }
    return val;
}

/* 从 VM 读 1 字节 */
static u8 vm_read_u8(u16 id, u8 def_val)
{
    u8 val = 0;
    int ret = syscfg_read(id, &val, sizeof(val));
    if (ret <= 0) {
        return def_val;
    }
    return val;
}

/* 写字符串到 VM */
static d_err_t vm_write_str(u16 id, const char *str, int max_len)
{
    int len = (int)strlen(str) + 1;
    if (len > max_len) {
        len = max_len;
    }
    int ret = syscfg_write(id, (void *)str, (u16)len);
    if (ret <= 0) {
        LOGE(TAG, "VM 写入失败: id=%d, ret=%d", id, ret);
        return D_ERR_IO;
    }
    return D_OK;
}

/* 写 int 到 VM */
static d_err_t vm_write_int(u16 id, int val)
{
    int ret = syscfg_write(id, &val, sizeof(val));
    if (ret <= 0) {
        LOGE(TAG, "VM 写入失败: id=%d, ret=%d", id, ret);
        return D_ERR_IO;
    }
    return D_OK;
}

/* 写 1 字节到 VM */
static d_err_t vm_write_u8(u16 id, u8 val)
{
    int ret = syscfg_write(id, &val, sizeof(val));
    if (ret <= 0) {
        LOGE(TAG, "VM 写入失败: id=%d, ret=%d", id, ret);
        return D_ERR_IO;
    }
    return D_OK;
}

static d_err_t vm_clear_str(u16 id)
{
    static const char empty[] = "";

    return vm_write_str(id, empty, 1);
}

static void cred_copy_str(char *dst, int dst_size, const char *src)
{
    if ((dst == NULL) || (dst_size <= 0)) {
        return;
    }

    memset(dst, 0, dst_size);
    if ((src != NULL) && (src[0] != '\0')) {
        strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

static runtime_provider_e cred_normalize_runtime_provider(u8 value)
{
    if (value == (u8)RUNTIME_PROVIDER_ALIYUN_MMI_RTOS) {
        return RUNTIME_PROVIDER_ALIYUN_MMI_RTOS;
    }

    if (value == (u8)RUNTIME_PROVIDER_PLATFORM_WS) {
        return RUNTIME_PROVIDER_PLATFORM_WS;
    }

    return RUNTIME_PROVIDER_PLATFORM_WS;
}

static d_err_t cred_store_optional_str(u16 id, const char *value, int max_len)
{
    if ((value != NULL) && (value[0] != '\0')) {
        return vm_write_str(id, value, max_len);
    }

    return vm_clear_str(id);
}

static d_err_t cred_store_runtime_control(const runtime_cred_t *rt, runtime_provider_e provider)
{
    d_err_t err;

    err = vm_write_u8(VM_RUNTIME_PROVIDER, (u8)provider);
    if (err != D_OK) return err;
    if (provider != RUNTIME_PROVIDER_ALIYUN_MMI_RTOS) {
        err = cred_store_optional_str(VM_CONTROL_HOST, NULL, CRED_STR_URL);
        if (err != D_OK) return err;
        err = cred_store_optional_str(VM_CONTROL_PORT, NULL, CRED_STR_PORT);
        if (err != D_OK) return err;
        err = cred_store_optional_str(VM_CONTROL_REGISTER, NULL, CRED_STR_URL);
        if (err != D_OK) return err;
        err = cred_store_optional_str(VM_CONTROL_TOKEN, NULL, CRED_STR_URL);
        if (err != D_OK) return err;
        err = cred_store_optional_str(VM_RUNTIME_APP_ID, NULL, CRED_STR_SHORT);
        if (err != D_OK) return err;
        return cred_store_optional_str(VM_RUNTIME_DEVICE_NAME, NULL, CRED_STR_SHORT);
    }

    err = cred_store_optional_str(VM_CONTROL_HOST, rt->control_host, CRED_STR_URL);
    if (err != D_OK) return err;
    err = cred_store_optional_str(VM_CONTROL_PORT, rt->control_port, CRED_STR_PORT);
    if (err != D_OK) return err;
    err = cred_store_optional_str(VM_CONTROL_REGISTER, rt->control_register_path, CRED_STR_URL);
    if (err != D_OK) return err;
    err = cred_store_optional_str(VM_CONTROL_TOKEN, rt->control_token_path, CRED_STR_URL);
    if (err != D_OK) return err;
    err = cred_store_optional_str(VM_RUNTIME_APP_ID, rt->runtime_app_id, CRED_STR_SHORT);
    if (err != D_OK) return err;
    return cred_store_optional_str(VM_RUNTIME_DEVICE_NAME, rt->runtime_device_name, CRED_STR_SHORT);
}

static void cred_sync_runtime_control(const runtime_cred_t *rt, runtime_provider_e provider)
{
    s_cred.runtime_provider = provider;
    if (provider != RUNTIME_PROVIDER_ALIYUN_MMI_RTOS) {
        memset(&s_cred.runtime_control, 0, sizeof(s_cred.runtime_control));
        return;
    }

    cred_copy_str(s_cred.runtime_control.control_host,
                  sizeof(s_cred.runtime_control.control_host),
                  rt->control_host);
    cred_copy_str(s_cred.runtime_control.control_port,
                  sizeof(s_cred.runtime_control.control_port),
                  rt->control_port);
    cred_copy_str(s_cred.runtime_control.control_register_path,
                  sizeof(s_cred.runtime_control.control_register_path),
                  rt->control_register_path);
    cred_copy_str(s_cred.runtime_control.control_token_path,
                  sizeof(s_cred.runtime_control.control_token_path),
                  rt->control_token_path);
    cred_copy_str(s_cred.runtime_control.runtime_app_id,
                  sizeof(s_cred.runtime_control.runtime_app_id),
                  rt->runtime_app_id);
    cred_copy_str(s_cred.runtime_control.runtime_device_name,
                  sizeof(s_cred.runtime_control.runtime_device_name),
                  rt->runtime_device_name);
}

static void cred_sync_runtime_memory(const runtime_cred_t *rt, runtime_provider_e provider)
{
    cred_copy_str(s_cred.access_key, sizeof(s_cred.access_key), rt->access_key);
    cred_copy_str(s_cred.access_secret, sizeof(s_cred.access_secret), rt->access_secret);
    s_cred.secret_version = rt->secret_version;
    s_cred.hb_interval_sec = rt->hb_interval_sec;
    cred_copy_str(s_cred.default_agent_id, sizeof(s_cred.default_agent_id), rt->default_agent_id);
    if (provider == RUNTIME_PROVIDER_PLATFORM_WS) {
        cred_copy_str(s_cred.ws_endpoint, sizeof(s_cred.ws_endpoint), rt->ws_endpoint);
    } else {
        memset(s_cred.ws_endpoint, 0, sizeof(s_cred.ws_endpoint));
    }
    cred_sync_runtime_control(rt, provider);
    s_cred.state = PROV_STATE_ACTIVATED;
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

d_err_t cred_init(void)
{
    if (s_initialized) {
        return D_OK;
    }

    memset(&s_cred, 0, sizeof(s_cred));

    /* 出厂标识：VM → 内存，空则用宏默认值 */
    vm_read_str(VM_DEVICE_ID,       s_cred.device_id,       CRED_STR_SHORT, D_DEFAULT_DEVICE_ID);
    vm_read_str(VM_PRODUCT_KEY,     s_cred.product_key,     CRED_STR_SHORT, D_DEFAULT_PRODUCT_KEY);
    vm_read_str(VM_DEVICE_SN,       s_cred.device_sn,       CRED_STR_SHORT, D_DEFAULT_DEVICE_SN);
    vm_read_str(VM_FACTORY_SECRET,  s_cred.factory_secret,  CRED_STR_SHORT, D_DEFAULT_FACTORY_SECRET);
    vm_read_str(VM_CLAIM_TICKET,    s_cred.claim_ticket,    CRED_STR_SHORT, D_DEFAULT_CLAIM_TICKET);
    vm_read_str(VM_TENANT_ID,       s_cred.tenant_id,       CRED_TENANT_LEN, D_DEFAULT_TENANT_ID);

    /* 运行时凭证 */
    vm_read_str(VM_ACCESS_KEY,      s_cred.access_key,      CRED_STR_SHORT, NULL);
    vm_read_str(VM_ACCESS_SECRET,   s_cred.access_secret,   CRED_STR_SHORT, NULL);
    s_cred.secret_version   = vm_read_int(VM_SECRET_VERSION, 0);
    s_cred.runtime_provider = cred_normalize_runtime_provider(
        vm_read_u8(VM_RUNTIME_PROVIDER, (u8)RUNTIME_PROVIDER_PLATFORM_WS));
    s_cred.hb_interval_sec  = vm_read_int(VM_HB_INTERVAL, 30);
    vm_read_str(VM_DEFAULT_AGENT_ID, s_cred.default_agent_id, sizeof(s_cred.default_agent_id), NULL);
    vm_read_str(VM_CONTROL_HOST, s_cred.runtime_control.control_host,
                sizeof(s_cred.runtime_control.control_host), NULL);
    vm_read_str(VM_CONTROL_PORT, s_cred.runtime_control.control_port,
                sizeof(s_cred.runtime_control.control_port), NULL);
    vm_read_str(VM_CONTROL_REGISTER, s_cred.runtime_control.control_register_path,
                sizeof(s_cred.runtime_control.control_register_path), NULL);
    vm_read_str(VM_CONTROL_TOKEN, s_cred.runtime_control.control_token_path,
                sizeof(s_cred.runtime_control.control_token_path), NULL);
    vm_read_str(VM_RUNTIME_APP_ID, s_cred.runtime_control.runtime_app_id,
                sizeof(s_cred.runtime_control.runtime_app_id), NULL);
    vm_read_str(VM_RUNTIME_DEVICE_NAME, s_cred.runtime_control.runtime_device_name,
                sizeof(s_cred.runtime_control.runtime_device_name), NULL);

    /* 容错：runtimeProvider 仅 1 字节，掉电可能导致该 VM 项损坏并回退为默认
     * PLATFORM_WS；若控制面 registerPath/getTokenPath 仍在，则按 ALIYUN_MMI_RTOS
     * 恢复（与 prov_parse_runtime_provider 的判定逻辑一致），避免 ws_service 误连
     * /device/ws 且 Qwen 又跳过，导致设备彻底无法联网 */
    if (s_cred.runtime_provider != RUNTIME_PROVIDER_ALIYUN_MMI_RTOS &&
        s_cred.runtime_control.control_register_path[0] != '\0' &&
        s_cred.runtime_control.control_token_path[0] != '\0') {
        LOGW(TAG, "runtimeProvider 读回异常，依据控制面字段恢复为 ALIYUN_MMI_RTOS");
        s_cred.runtime_provider = RUNTIME_PROVIDER_ALIYUN_MMI_RTOS;
    }

    vm_read_str(VM_WS_ENDPOINT,     s_cred.ws_endpoint,     CRED_STR_URL,
                (s_cred.runtime_provider == RUNTIME_PROVIDER_PLATFORM_WS) ? D_WS_DEFAULT_ENDPOINT : NULL);

    /* device_cert 模式（可选） */
    vm_read_str(VM_DEVICE_CERT,     s_cred.device_cert_pem, CRED_STR_PEM, NULL);
    vm_read_str(VM_DEVICE_PKEY,     s_cred.device_pkey_pem, CRED_STR_PEM, NULL);

    /* 首配状态 */
    s_cred.state = (prov_state_t)vm_read_u8(VM_PROVISION_STATE, PROV_STATE_NONE);

    s_initialized = 1;

    LOGI(TAG, "凭证初始化完成: device_id=%s, state=0x%02X, provider=%d, ctrl_reg=%d ctrl_tok=%d",
         s_cred.device_id, s_cred.state, (int)s_cred.runtime_provider,
         s_cred.runtime_control.control_register_path[0] != '\0',
         s_cred.runtime_control.control_token_path[0] != '\0');

    return D_OK;
}

d_err_t cred_save_runtime(const runtime_cred_t *rt)
{
    runtime_provider_e provider;
    d_err_t err;

    if (!rt || !rt->access_key || !rt->access_secret) {
        return D_ERR_INVALID_ARG;
    }

    provider = (rt->runtime_provider == RUNTIME_PROVIDER_ALIYUN_MMI_RTOS)
        ? RUNTIME_PROVIDER_ALIYUN_MMI_RTOS
        : RUNTIME_PROVIDER_PLATFORM_WS;

    if ((provider == RUNTIME_PROVIDER_ALIYUN_MMI_RTOS) &&
        ((rt->control_host == NULL) || (rt->control_host[0] == '\0') ||
         (rt->control_port == NULL) || (rt->control_port[0] == '\0') ||
         (rt->control_register_path == NULL) || (rt->control_register_path[0] == '\0') ||
         (rt->control_token_path == NULL) || (rt->control_token_path[0] == '\0'))) {
        return D_ERR_INVALID_ARG;
    }

    /* 写入 VM */
    err = vm_write_str(VM_ACCESS_KEY,    rt->access_key,    CRED_STR_SHORT);
    if (err != D_OK) return err;

    err = vm_write_str(VM_ACCESS_SECRET, rt->access_secret, CRED_STR_SHORT);
    if (err != D_OK) return err;

    err = vm_write_int(VM_SECRET_VERSION, rt->secret_version);
    if (err != D_OK) return err;

    err = cred_store_optional_str(VM_WS_ENDPOINT,
                                  (provider == RUNTIME_PROVIDER_PLATFORM_WS) ? rt->ws_endpoint : NULL,
                                  CRED_STR_URL);
    if (err != D_OK) return err;

    err = vm_write_int(VM_HB_INTERVAL, rt->hb_interval_sec);
    if (err != D_OK) return err;

    err = cred_store_optional_str(VM_DEFAULT_AGENT_ID, rt->default_agent_id, sizeof(s_cred.default_agent_id));
    if (err != D_OK) return err;

    err = cred_store_runtime_control(rt, provider);
    if (err != D_OK) return err;

    /* 写入激活状态标志 */
    err = vm_write_u8(VM_PROVISION_STATE, (u8)PROV_STATE_ACTIVATED);
    if (err != D_OK) return err;

    /* 同步内存缓存 */
    cred_sync_runtime_memory(rt, provider);

    LOGI(TAG, "运行时凭证已保存: access_key=%s, ver=%d",
         s_cred.access_key, s_cred.secret_version);

    return D_OK;
}

int cred_is_activated(void)
{
    return (s_cred.state == PROV_STATE_ACTIVATED
            && s_cred.access_key[0] != '\0'
            && s_cred.access_secret[0] != '\0');
}

void cred_clear_runtime(void)
{
    /* 清除 VM */
    char empty[4] = {0};
    syscfg_write(VM_ACCESS_KEY,          empty, 1);
    syscfg_write(VM_ACCESS_SECRET,       empty, 1);
    syscfg_write(VM_SECRET_VERSION,      empty, 1);
    syscfg_write(VM_WS_ENDPOINT,         empty, 1);
    syscfg_write(VM_HB_INTERVAL,         empty, 1);
    syscfg_write(VM_DEFAULT_AGENT_ID,    empty, 1);
    syscfg_write(VM_RUNTIME_PROVIDER,    empty, 1);
    syscfg_write(VM_CONTROL_HOST,        empty, 1);
    syscfg_write(VM_CONTROL_PORT,        empty, 1);
    syscfg_write(VM_CONTROL_REGISTER,    empty, 1);
    syscfg_write(VM_CONTROL_TOKEN,       empty, 1);
    syscfg_write(VM_RUNTIME_APP_ID,      empty, 1);
    syscfg_write(VM_RUNTIME_DEVICE_NAME, empty, 1);
    syscfg_write(VM_PROVISION_STATE,     empty, 1);

    /* 清除内存缓存 */
    memset(s_cred.access_key,    0, sizeof(s_cred.access_key));
    memset(s_cred.access_secret, 0, sizeof(s_cred.access_secret));
    memset(s_cred.ws_endpoint, 0, sizeof(s_cred.ws_endpoint));
    memset(s_cred.default_agent_id, 0, sizeof(s_cred.default_agent_id));
    memset(&s_cred.runtime_control, 0, sizeof(s_cred.runtime_control));
    s_cred.secret_version = 0;
    s_cred.hb_interval_sec = 0;
    s_cred.runtime_provider = RUNTIME_PROVIDER_NONE;
    s_cred.state = PROV_STATE_NONE;

    LOGI(TAG, "运行时凭证已清除");
}

const device_cred_t *cred_get(void)
{
    return &s_cred;
}

runtime_provider_e cred_get_runtime_provider(void)
{
    return s_cred.runtime_provider;
}

#endif /* D_DEVICE_CLOUD_ENABLE */
