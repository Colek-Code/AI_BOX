/** @file qwen_full_credential.c @brief 全托管注册意图与签发证书的受检持久化。 */
#include "config/app_config.h"
#if D_FEATURE_QWEN
#include "qwen_full_credential.h"
#include "qwen_credential_format.h"
#include "platform/storage/d_vm_status.h"
#include "system/includes.h"
#include "system/spinlock.h"
#include "syscfg/syscfg_id.h"
#include <stddef.h>
#include <string.h>

enum { FULL_SDK_SLOT = 16, FULL_MAX_SDK = 200, FULL_VERSION = 1 };
typedef struct {
    uint8_t magic[4];
    uint32_t version;
    uint32_t format;
    uint8_t identity[32];
    uint32_t crc;
} full_intent_t;
typedef struct {
    full_intent_t intent;
    uint8_t owner[QWEN_AUTH_OWNER_SIZE];
    uint8_t sdk[FULL_MAX_SDK];
    uint32_t length;
    uint32_t crc;
} full_cert_t;
typedef enum { FULL_CLOSED, FULL_EMPTY, FULL_DEFERRED, FULL_RESERVED, FULL_READY } full_phase_e;
static full_phase_e s_phase;
static full_intent_t s_intent;
static full_cert_t s_transfer, s_readback;
/* 仅保存已验证且尚未确认落盘的签发快照；SDK 的清理只作用于运行副本。 */
static uint8_t s_commit_pending;
static uint8_t s_probe[sizeof(full_cert_t) + 1U];
static uint32_t s_vm_status[D_VM_STATUS_SIZE / sizeof(uint32_t)];
static DEFINE_SPINLOCK(s_live_lock);
static uint8_t s_sdk[FULL_MAX_SDK], s_owner[QWEN_AUTH_OWNER_SIZE];
static uint16_t s_sdk_length, s_owner_length;

int qwen_full_credential_enabled(void)
{
    return D_QWEN_FULL_CREDENTIAL_STORE && D_QWEN_AUTH_MODE == D_QWEN_AUTH_MODE_FULL_MANAGED;
}

/** @brief 只在读前、读后均无该槽且 SDK 返回缺项时报告空。短读、损坏或存在位不明均报错。 */
static int read_bounded(uint16_t id, void *record, uint16_t size)
{
    int result = -1;
    int before = d_vm_present(id, s_vm_status);
    /* vm_read 对过长记录返回请求容量。多读一字节才能识别有效前缀后的未知数据。 */
    int count = syscfg_read(id, s_probe, (uint16_t)(size + 1U));
    int after = d_vm_present(id, s_vm_status);
    if (before == 1 && after == 1 && count > 0 && count <= size) {
        memcpy(record, s_probe, count);
        result = count;
    } else if (!before && !after && count == VM_READ_DATA_ERR) {
        result = 0;
    }
    memset(s_probe, 0, size + 1U);
    return result;
}

/** @brief 定长记录额外拒绝短读，返回 1 完整、0 确认缺失、-1 不可信。 */
static int read_record(uint16_t id, void *record, uint16_t size)
{
    int count = read_bounded(id, record, size);
    return count == size ? 1 : (count == 0 ? 0 : -1);
}

/** @brief 完整写入后独立读回比较，调用方的 Flash 缓冲与核对缓冲不重叠。 */
static d_err_t save_record(uint16_t id, const void *data, void *check, uint16_t size)
{
    if (syscfg_write(id, (void *)data, size) != size) return D_ERR_IO;
    if (read_record(id, check, size) != 1 || memcmp(data, check, size)) return D_ERR_IO;
    return D_OK;
}

static uint16_t serialized_length(uint32_t format)
{
    if (format == QWEN_FULL_FORMAT_110) return 176U;
    if (format == QWEN_FULL_FORMAT_131) return 200U;
    return 0;
}

static uint32_t little32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief 1.1.0 与 1.3.1 注册结构共享前 176 字节；两个版本的注册位均为 0x1d。 */
static int registered_layout(const uint8_t *sdk, int length)
{
    uint32_t version = little32(sdk);
    uint32_t flags = little32(sdk + 4);
    uint32_t mask;
    if (length == 176 && version == QWEN_FULL_FORMAT_110) mask = 0x3fU;
    else if (length == 200 && version == QWEN_FULL_FORMAT_131) mask = 0x7fU;
    else return 0;
    return (flags & ~mask) == 0 && (flags & 0x1dU) == 0x1dU;
}

static void clear_live(void)
{
    s_phase = FULL_CLOSED;
    spin_lock(&s_live_lock);
    memset(s_sdk, 0, sizeof(s_sdk));
    memset(s_owner, 0, sizeof(s_owner));
    s_sdk_length = s_owner_length = 0;
    spin_unlock(&s_live_lock);
}

/** @brief 从已验证的输入装入 SDK 运行副本。旧格式仅在 RAM 加上 workspace 和对齐区。 */
static void restore_live(const full_cert_t *record, uint16_t target_length)
{
    spin_lock(&s_live_lock);
    memcpy(s_sdk, record->sdk, record->length);
    memset(s_sdk + record->length, 0, target_length - record->length);
    memcpy(s_owner, record->owner, sizeof(s_owner));
    s_sdk_length = target_length;
    s_owner_length = sizeof(s_owner);
    spin_unlock(&s_live_lock);
    s_phase = FULL_READY;
}

/** @brief 独立槽为空或产品使用旧槽时，读取物理 16/33，禁止单边缺失或错误归属触发重建。 */
static d_err_t open_legacy(const uint8_t identity[32], uint8_t mode, uint16_t target_length,
                           int allow_separate_semi)
{
    int length = read_bounded(FULL_SDK_SLOT, s_readback.sdk, sizeof(s_readback.sdk));
    int owner = read_record(QWEN_AUTH_VM_OWNER, s_readback.owner, sizeof(s_readback.owner));
    d_err_t result = D_ERR_IO;
    if (!length && !owner) {
        s_phase = FULL_EMPTY;
        result = D_OK;
    } else if (owner == 1 && length > 0 && registered_layout(s_readback.sdk, length)) {
        int stored_mode = qwen_auth_owner_mode(s_readback.owner);
        if (stored_mode == mode && !memcmp(s_readback.owner + 8, identity, 32) &&
            length <= target_length) {
            s_readback.length = (uint32_t)length;
            restore_live(&s_readback, target_length);
            result = D_OK;
        } else if (allow_separate_semi && stored_mode == D_QWEN_AUTH_MODE_SEMI_MANAGED) {
            /* 已签发的半托管归属占用原槽；独立全托管区首次注册不会修改这些字节。 */
            s_phase = FULL_EMPTY;
            result = D_OK;
        }
    }
    memset(&s_readback, 0, sizeof(s_readback));
    return result;
}

d_err_t qwen_legacy_credential_open(const uint8_t identity[32])
{
    clear_live();
    if (!identity) return D_ERR_INVALID_ARG;
    return open_legacy(identity, D_QWEN_AUTH_MODE, FULL_MAX_SDK, 0);
}

/** @brief 复核或重写本次已签发快照；Flash 已有不同的完整记录或读失败时保持现场。 */
static d_err_t finish_signed_snapshot(int cert_exists)
{
    if (cert_exists == 0) {
        return save_record(QWEN_FULL_VM_CERT, &s_transfer, &s_readback, sizeof(s_transfer));
    }
    if (cert_exists != 1 || memcmp(&s_transfer, &s_readback, sizeof(s_transfer))) return D_ERR_IO;
    return D_OK;
}

d_err_t qwen_full_credential_open(const uint8_t identity[32], uint32_t format)
{
    full_intent_t saved;
    full_intent_t deferred;
    int may_reuse = s_phase == FULL_DEFERRED;
    int intent, cert;
    d_err_t result = D_ERR_IO;
    if (may_reuse) deferred = s_intent;
    clear_live();
    if (!identity || !serialized_length(format)) return D_ERR_INVALID_ARG;
    memset(&s_intent, 0, sizeof(s_intent));
    memcpy(s_intent.magic, "QFCR", 4);
    s_intent.version = FULL_VERSION;
    s_intent.format = format;
    memcpy(s_intent.identity, identity, sizeof(s_intent.identity));
    s_intent.crc = qwen_auth_crc32((const uint8_t *)&s_intent, offsetof(full_intent_t, crc));
    intent = read_record(QWEN_FULL_VM_INTENT, &saved, sizeof(saved));
    cert = read_record(QWEN_FULL_VM_CERT, &s_readback, sizeof(s_readback));
    if (s_commit_pending) {
        /* 只恢复同一启动、同一归属的本地提交。无法解释的记录不覆盖。 */
        if (intent != 1 || memcmp(&saved, &s_transfer.intent, sizeof(saved)) ||
            memcmp(&s_intent, &saved, sizeof(saved))) goto done;
        if (finish_signed_snapshot(cert) != D_OK) goto done;
        cert = 1;
        s_commit_pending = 0U;
        memset(&s_transfer, 0, sizeof(s_transfer));
    }
    if (intent == 0 && cert == 0) {
        return open_legacy(identity, D_QWEN_AUTH_MODE_FULL_MANAGED, serialized_length(format), 1);
    }
    if (may_reuse && intent == 1 && cert == 0 && !memcmp(&saved, &deferred, sizeof(saved)) &&
        !memcmp(&saved, &s_intent, sizeof(saved))) {
        /* 本次启动的工作任务在 HTTP 首字节前确认取消；重启后不具备这项证据。 */
        s_phase = FULL_DEFERRED;
        return D_OK;
    }
    if (intent == 1 && cert == 1 && !memcmp(saved.magic, s_intent.magic, 4) &&
               saved.version == FULL_VERSION && !memcmp(saved.identity, identity, 32) &&
               (saved.format == format || (saved.format == QWEN_FULL_FORMAT_110 && format == QWEN_FULL_FORMAT_131)) &&
               saved.crc == qwen_auth_crc32((const uint8_t *)&saved, offsetof(full_intent_t, crc)) &&
               !memcmp(&s_readback.intent, &saved, sizeof(saved)) &&
               s_readback.length == serialized_length(saved.format) &&
               registered_layout(s_readback.sdk, s_readback.length) &&
               qwen_auth_owner_mode(s_readback.owner) == D_QWEN_AUTH_MODE_FULL_MANAGED &&
               !memcmp(s_readback.owner + 8, identity, 32) &&
               s_readback.crc == qwen_auth_crc32((const uint8_t *)&s_readback, offsetof(full_cert_t, crc))) {
        restore_live(&s_readback, serialized_length(format));
        result = D_OK;
    }
done:
    memset(&s_readback, 0, sizeof(s_readback));
    return result;
}

int qwen_full_credential_can_register(void)
{
    return s_phase == FULL_EMPTY || s_phase == FULL_DEFERRED;
}

d_err_t qwen_full_credential_reserve(void)
{
    full_intent_t check;
    if (s_phase == FULL_DEFERRED) {
        s_phase = FULL_RESERVED;
        return D_OK;
    }
    if (s_phase != FULL_EMPTY) return D_ERR_NOT_READY;
    s_phase = FULL_CLOSED;
    if (save_record(QWEN_FULL_VM_INTENT, &s_intent, &check, sizeof(check)) != D_OK) return D_ERR_IO;
    s_phase = FULL_RESERVED;
    return D_OK;
}

void qwen_full_credential_defer_unsent(void)
{
    if (s_phase == FULL_RESERVED) s_phase = FULL_DEFERRED;
}

int qwen_full_credential_restored(void)
{
    return s_phase == FULL_READY;
}

d_err_t qwen_full_credential_commit(void)
{
    d_err_t result = D_ERR_IO;
    if (s_phase != FULL_RESERVED) return D_ERR_NOT_READY;
    if (s_commit_pending) {
        /* 同一次 SDK 准备期间重试，也使用原签发快照。 */
        result = finish_signed_snapshot(read_record(QWEN_FULL_VM_CERT, &s_readback, sizeof(s_readback)));
        goto finish;
    }
    memset(&s_transfer, 0, sizeof(s_transfer));
    s_transfer.intent = s_intent;
    spin_lock(&s_live_lock);
    if (s_sdk_length == serialized_length(s_intent.format) && s_owner_length == sizeof(s_owner)) {
        memcpy(s_transfer.sdk, s_sdk, s_sdk_length);
        memcpy(s_transfer.owner, s_owner, sizeof(s_owner));
        s_transfer.length = s_sdk_length;
    }
    spin_unlock(&s_live_lock);
    if (s_transfer.length && registered_layout(s_transfer.sdk, s_transfer.length) &&
        qwen_auth_owner_mode(s_transfer.owner) == D_QWEN_AUTH_MODE_FULL_MANAGED &&
        !memcmp(s_transfer.owner + 8, s_intent.identity, 32)) {
        s_transfer.crc = qwen_auth_crc32((const uint8_t *)&s_transfer, offsetof(full_cert_t, crc));
        s_commit_pending = 1U;
        result = finish_signed_snapshot(0);
    }
finish:
    if (result == D_OK) {
        s_phase = FULL_READY;
        s_commit_pending = 0U;
    }
    if (!s_commit_pending) memset(&s_transfer, 0, sizeof(s_transfer));
    memset(&s_readback, 0, sizeof(s_readback));
    return result;
}

int qwen_full_credential_read(uint16_t id, void *bytes, uint16_t capacity)
{
    uint16_t length;
    if (!bytes || !capacity || (id != FULL_SDK_SLOT && id != QWEN_AUTH_VM_OWNER)) return D_ERR_INVALID_ARG;
    spin_lock(&s_live_lock);
    length = id == FULL_SDK_SLOT ? s_sdk_length : s_owner_length;
    if (length <= capacity) memcpy(bytes, id == FULL_SDK_SLOT ? s_sdk : s_owner, length);
    spin_unlock(&s_live_lock);
    return length <= capacity ? (int)length : VM_DATA_LEN_ERR;
}

int qwen_full_credential_write(uint16_t id, const void *bytes, uint16_t length)
{
    if (!bytes || !length || (id != FULL_SDK_SLOT && id != QWEN_AUTH_VM_OWNER)) return D_ERR_INVALID_ARG;
    if (length > (id == FULL_SDK_SLOT ? sizeof(s_sdk) : sizeof(s_owner))) return VM_DATA_LEN_ERR;
    spin_lock(&s_live_lock);
    if (id == FULL_SDK_SLOT) {
        memcpy(s_sdk, bytes, length);
        s_sdk_length = length;
    } else {
        memcpy(s_owner, bytes, length);
        s_owner_length = length;
    }
    spin_unlock(&s_live_lock);
    return length;
}
#endif
