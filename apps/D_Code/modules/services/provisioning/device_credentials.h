/**
 * @file    device_credentials.h
 * @brief   设备凭证管理 — VM 持久化读写
 * @note    公共底层模块，被 provision_service 和 ws_service 共同依赖
 *          不依赖任何业务模块（ws_service / ai_service）
 *
 *   - VM 存储出厂标识（deviceId, productKey, deviceSn, factorySecret）
 *   - VM 存储运行时凭证（accessKey, accessSecret, wsEndpoint 等）
 *   - 内存缓存，减少 flash 读写
 */

#ifndef DEVICE_CREDENTIALS_H
#define DEVICE_CREDENTIALS_H

#include "framework/d_error.h"

//*********************************************************************************//
//                              VM ID 分配（用户区间 1~59）                         //
//*********************************************************************************//

#define VM_DEVICE_ID            1       /* deviceId，最长 64 字节 */
#define VM_PRODUCT_KEY          2       /* productKey，最长 64 字节 */
#define VM_DEVICE_SN            3       /* deviceSn，最长 64 字节 */
#define VM_FACTORY_SECRET       4       /* 出厂密钥，最长 64 字节 */
#define VM_ACCESS_KEY           5       /* 运行时 accessKey，最长 64 字节 */
#define VM_ACCESS_SECRET        6       /* 运行时 accessSecret，最长 64 字节 */
#define VM_SECRET_VERSION       7       /* secretVersion，4 字节 */
#define VM_WS_ENDPOINT          8       /* wsEndpoint URL，最长 128 字节 */
#define VM_HB_INTERVAL          9       /* heartbeatIntervalSec，4 字节 */
#define VM_DEVICE_CERT         10       /* device_cert PEM，最长 512 字节 */
#define VM_DEVICE_PKEY         11       /* device_cert 私钥，最长 512 字节 */
#define VM_PROVISION_STATE     12       /* 首配状态标志，1 字节 */
#define VM_CLAIM_TICKET        13       /* claimTicket，最长 64 字节 */
#define VM_TENANT_ID           14       /* tenantId，最长 16 字节 */
#define VM_DEFAULT_AGENT_ID    15       /* defaultAgentProfileId，最长 32 字节 */
/* 注意：idx 16 已被 Qwen SDK 占用（qwen_hal.c QWEN_VM_STORAGE_ID=16，
 * 存 device_secret/license 约 180 字节）。本模块禁止使用 16，否则会与
 * Qwen 存储互相覆盖，导致 device_secret 损坏（getToken 签名解密失败）
 * 以及 runtimeProvider 读出乱值。runtimeProvider 因此改用 idx 23。 */
#define VM_CONTROL_HOST        17       /* 控制面 host，最长 128 字节 */
#define VM_CONTROL_PORT        18       /* 控制面 port，最长 8 字节 */
#define VM_CONTROL_REGISTER    19       /* 控制面 registerPath，最长 128 字节 */
#define VM_CONTROL_TOKEN       20       /* 控制面 getTokenPath，最长 128 字节 */
#define VM_RUNTIME_APP_ID      21       /* 运行时 appId，最长 128 字节 */
#define VM_RUNTIME_DEVICE_NAME 22       /* 运行时 deviceName，最长 128 字节 */
#define VM_RUNTIME_PROVIDER    23       /* runtimeProvider，1 字节（原 16，因与 Qwen 冲突迁移） */
/* 注意：idx 24/25 已预留给 WiFi 配网凭证持久化（见 wifi_service.c
 * WIFI_VM_SSID=24 / WIFI_VM_PWD=25）。本模块禁止使用 24/25。 */

//*********************************************************************************//
//                              字段长度限制                                        //
//*********************************************************************************//

#define CRED_STR_SHORT          128     /* 短字符串字段最大长度，128B 兼容较长 accessSecret */
#define CRED_STR_URL           128      /* URL 字段最大长度 */
#define CRED_STR_PORT            8      /* port 字符串最大长度 */
#define CRED_STR_PEM           512      /* PEM 证书/私钥最大长度 */
#define CRED_TENANT_LEN         16      /* tenantId 最大长度 */

//*********************************************************************************//
//                              首配状态枚举                                        //
//*********************************************************************************//

typedef enum {
    PROV_STATE_NONE      = 0x00,        /* 未初始化 / 未首配 */
    PROV_STATE_ACTIVATED = 0xA5,        /* 已激活（有运行时凭证） */
} prov_state_t;

typedef enum {
    RUNTIME_PROVIDER_NONE = 0,
    RUNTIME_PROVIDER_PLATFORM_WS = 1,
    RUNTIME_PROVIDER_ALIYUN_MMI_RTOS = 2,
} runtime_provider_e;

typedef struct {
    char control_host[CRED_STR_URL];
    char control_port[CRED_STR_PORT];
    char control_register_path[CRED_STR_URL];
    char control_token_path[CRED_STR_URL];
    char runtime_app_id[CRED_STR_SHORT];
    char runtime_device_name[CRED_STR_SHORT];
} runtime_control_config_t;

//*********************************************************************************//
//                              数据结构                                            //
//*********************************************************************************//

/* 完整设备凭证（内存缓存） */
typedef struct {
    /* 出厂标识 */
    char device_id[CRED_STR_SHORT];
    char product_key[CRED_STR_SHORT];
    char device_sn[CRED_STR_SHORT];
    char factory_secret[CRED_STR_SHORT];
    char claim_ticket[CRED_STR_SHORT];
    char tenant_id[CRED_TENANT_LEN];

    /* 运行时凭证（finalize 后获得） */
    char access_key[CRED_STR_SHORT];
    char access_secret[CRED_STR_SHORT];
    int  secret_version;
    char ws_endpoint[CRED_STR_URL];
    int  hb_interval_sec;
    char default_agent_id[32];  /* defaultAgentProfileId，平台指定的默认 Agent */
    runtime_provider_e runtime_provider;
    runtime_control_config_t runtime_control;

    /* device_cert 模式（可选） */
    char device_cert_pem[CRED_STR_PEM];
    char device_pkey_pem[CRED_STR_PEM];

    /* 状态 */
    prov_state_t state;
} device_cred_t;

/* finalize 返回的运行时凭证子集 */
typedef struct {
    const char *access_key;
    const char *access_secret;
    int         secret_version;
    const char *ws_endpoint;
    int         hb_interval_sec;
    const char *default_agent_id;   /* 可为 NULL（平台未返回时） */
    runtime_provider_e runtime_provider;
    const char *control_host;
    const char *control_port;
    const char *control_register_path;
    const char *control_token_path;
    const char *runtime_app_id;
    const char *runtime_device_name;
} runtime_cred_t;

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

/**
 * @brief  初始化凭证模块：从 VM 读取 → 内存缓存
 *         VM 为空时使用 config/deployment_config.h 中的默认值
 * @return D_OK 成功
 */
d_err_t cred_init(void);

/**
 * @brief  保存运行时凭证（finalize 成功后调用）
 *         同时写入 VM 和内存缓存，更新状态为 PROV_STATE_ACTIVATED
 * @param  rt  运行时凭证
 * @return D_OK 成功，D_ERR_IO 写入失败
 */
d_err_t cred_save_runtime(const runtime_cred_t *rt);

/**
 * @brief  判断设备是否已激活（VM 中有有效运行时凭证）
 * @return 1=已激活，0=未激活
 */
int cred_is_activated(void);

/**
 * @brief  清除运行时凭证（调试用，恢复为未首配状态）
 */
void cred_clear_runtime(void);

/**
 * @brief  获取当前凭证的只读指针
 * @return 凭证结构体指针，生命周期与模块相同
 */
const device_cred_t *cred_get(void);

runtime_provider_e cred_get_runtime_provider(void);

#endif /* DEVICE_CREDENTIALS_H */
