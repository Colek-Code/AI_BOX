/** @file ota_service.c @brief 区分请求受理与 Flash 完成，保持升级事务所有权至受检收尾。 */
#include "ota_port.h"
#include <string.h>

static struct {
    ota_candidate_t candidate;
    ota_snapshot_t snapshot;
    ota_http_response_t checked;
    ota_maintenance_backend_t maintenance;
    uint32_t started;
    uint8_t initialized;
    uint8_t cancelled;
    uint8_t expired;
    uint8_t sdk_owned;
    uint8_t shutdown;
    uint8_t is_upgrade; /* 事务身份跨不确定或超时状态持续保留。 */
    uint8_t buffer[OTA_PACKET_SIZE];
} s_ota;

/**
 * @brief 推进环形非零请求编号或代次。
 * @param[in] value 当前值。
 * @return 下一非零值。
 */
static uint32_t ota_next(uint32_t value) { return value + 1U ? value + 1U : 1U; }

/**
 * @brief 在锁内发布状态及版本，再在锁外通知事件。
 * @param[in] state 新事务状态。
 * @param[in] error 业务错误分类。
 * @param[in] detail 实际错误细节。
 */
static void ota_state(ota_state_e state, ota_error_e error, d_err_t detail)
{
    ota_port_lock();
    s_ota.snapshot.state = state;
    s_ota.snapshot.error = error;
    s_ota.snapshot.detail = detail;
    s_ota.snapshot.revision = ota_next(s_ota.snapshot.revision);
    ota_port_unlock();
    ota_port_notify(state);
}

/**
 * @brief 核对取消与十分钟事务总期限，必要时锁存过期。
 * @return 已取消或过期返回非零。
 */
static uint8_t ota_stopped(void)
{
    uint8_t stopped;
    ota_port_lock();
    if ((uint32_t)(ota_port_now() - s_ota.started) >= OTA_TRANSACTION_MS) s_ota.expired = 1U;
    stopped = s_ota.cancelled || s_ota.expired;
    ota_port_unlock();
    return stopped;
}

/**
 * @brief 独立观察阻塞任务的绝对期限并发布 UNCERTAIN 诊断；完整合同见 ota_service.h。
 */
void ota_service_observe_deadline(void)
{
    ota_port_lock();
    if (s_ota.snapshot.worker_active && s_ota.snapshot.state != OTA_REBOOT_PENDING && !s_ota.expired &&
        (uint32_t)(ota_port_now() - s_ota.started) >= OTA_TRANSACTION_MS) {
        s_ota.expired = 1U;
        s_ota.snapshot.state = OTA_UNCERTAIN;
        s_ota.snapshot.error = OTA_ERROR_DEADLINE;
        s_ota.snapshot.detail = D_ERR_TIMEOUT;
        s_ota.snapshot.revision = ota_next(s_ota.snapshot.revision);
    }
    ota_port_unlock();
}

/**
 * @brief 打开当前候选传输，仅对网络错误最多尝试三次。
 * @param[in] head 非零使用 HEAD，否则 GET。
 * @param[in] etag 可选 If-Match 值。
 * @param[out] response HTTP 响应摘要输出。
 * @return 打开结果；取消或总期限到期返回 D_ERR_TIMEOUT。
 */
static d_err_t ota_open(uint8_t head, const char *etag, ota_http_response_t *response)
{
    unsigned attempt;
    d_err_t result = D_ERR_NETWORK;
    for (attempt = 0U; attempt < 3U; attempt++) {
        if (ota_stopped()) return D_ERR_TIMEOUT;
        result = ota_transport_open(s_ota.candidate.url, head, etag, response);
        if (result != D_ERR_NETWORK) return result;
        ota_transport_close();
    }
    return result;
}

/**
 * @brief 在交给 SDK 前检查 200、确定长度和传输大小上限。
 * @param[in] response 已解析 HTTP 响应。
 * @return 符合约束返回 D_OK，否则 D_ERR_INVALID_ARG。
 * @note 非活动分区实际大小仍由 SDK 根据 app_core 检查。
 */
static d_err_t ota_response_check(const ota_http_response_t *response)
{
    /* SDK 根据 app_core 检查实际非活动分区大小；
     * 交给 SDK 前先在本层限制传输输入。 */
    if (response->status != 200U || !response->has_length ||
        response->length <= OTA_PACKAGE_PREFIX || response->length > 16U * 1024U * 1024U ||
        (s_ota.candidate.expected_size && response->length != s_ota.candidate.expected_size))
        return D_ERR_INVALID_ARG;
    return D_OK;
}

/**
 * @brief 从传输中读满当前静态包缓冲的指定长度。
 * @param[in] length 待读字节数，调用者保证不超过 OTA_PACKET_SIZE。
 * @return 读满返回 D_OK；取消、超时或网络异常返回对应错误。
 */
static d_err_t ota_read_exact(uint32_t length)
{
    uint32_t offset = 0U;
    while (offset < length) {
        int count;
        if (ota_stopped()) return D_ERR_TIMEOUT;
        count = ota_transport_read(s_ota.buffer + offset, (uint16_t)(length - offset));
        if (count <= 0 || (uint32_t)count > length - offset) return D_ERR_NETWORK;
        offset += (uint32_t)count;
    }
    return D_OK;
}

/**
 * @brief 通过 HEAD 元数据与 GET 固定前缀检查候选对象。
 * @return 核验成功返回 D_OK；传输、包头或期限错误返回对应值。
 * @note HEAD 不支持时退到 GET；此过程不申请维护或写 Flash。
 */
static d_err_t ota_check_candidate(void)
{
    ota_http_response_t response;
    ota_http_response_t metadata;
    ota_package_t package;
    d_err_t result = ota_open(1U, NULL, &response);
    if (result != D_OK) return result;
    if (response.status == 405U || response.status == 501U) {
        memset(&metadata, 0, sizeof(metadata));
    } else {
        result = ota_response_check(&response);
        if (result != D_OK) return result;
        metadata = response;
    }
    ota_transport_close();
    /* 仅 HEAD 也可能描述 HTML 错误页；提供确认前先检查固定 UFW 前缀，
     * 此阶段没有 Flash 写入或维护占用副作用。 */
    result = ota_open(0U, metadata.etag, &response);
    if (result != D_OK) return result;
    result = ota_response_check(&response);
    if (result == D_OK && ((metadata.has_length && response.length != metadata.length) ||
        (metadata.etag[0] && strcmp(metadata.etag, response.etag)))) result = D_ERR_INVALID_ARG;
    if (result == D_OK) result = ota_read_exact(OTA_PACKAGE_PREFIX);
    if (result == D_OK) result = ota_package_parse(s_ota.buffer, response.length, &package);
    if (result == D_OK && !ota_stopped()) {
        ota_port_lock();
        s_ota.checked = response;
        s_ota.snapshot.total = response.length;
        ota_port_unlock();
        return D_OK;
    }
    return result == D_OK ? D_ERR_TIMEOUT : result;
}

/**
 * @brief 关闭传输，受检释放 SDK，最后归还产品维护占用。
 * @return 全部收尾完成返回 1；任一步仍未确认返回 0。
 * @note 未完成时保留工作任务与占用，调用者继续重试同一收尾。
 */
static uint8_t ota_cleanup(void)
{
    ota_transport_close();
    if (s_ota.sdk_owned) {
        if (ota_sdk_close() != D_OK) return 0U;
        s_ota.sdk_owned = 0U;
    }
    if (s_ota.snapshot.maintenance) {
        if (s_ota.maintenance.release() != D_OK) return 0U;
        ota_port_lock();
        s_ota.snapshot.maintenance = 0U;
        ota_port_unlock();
    }
    return 1U;
}

/**
 * @brief 下载并流式写入、校验包正文与摘要，最后提交 SDK 升级信息。
 * @param[out] error 当前失败所属业务阶段分类输出。
 * @return 完整提交结果；传输、包、写入、校验或提交失败返回实际错误。
 * @note 提交前始终检查事务期限；写入开始前先保留 SDK 所有权标记，失败不能推定无资源。
 */
static d_err_t ota_download(ota_error_e *error)
{
    ota_http_response_t response;
    ota_package_t package;
    uint32_t received = 0U, chunk;
    uint16_t body_crc = 0U;
    uint8_t digest[32], extra;
    d_err_t result;
    *error = OTA_ERROR_TRANSPORT;
    result = ota_open(0U, s_ota.checked.etag, &response);
    if (result != D_OK) return result;
    if (ota_response_check(&response) != D_OK || response.length != s_ota.checked.length ||
        (s_ota.checked.etag[0] && strcmp(response.etag, s_ota.checked.etag)))
        return D_ERR_INVALID_ARG;
    result = ota_read_exact(OTA_PACKAGE_PREFIX);
    if (result != D_OK) return result;
    *error = OTA_ERROR_PACKAGE;
    result = ota_package_parse(s_ota.buffer, response.length, &package);
    if (result != D_OK || response.length < 256U) return D_ERR_INVALID_ARG;
    if (ota_stopped()) return D_ERR_TIMEOUT;
    *error = OTA_ERROR_WRITE;
    s_ota.sdk_owned = 1U;
    result = ota_sdk_begin();
    if (result != D_OK) return result;
    chunk = ota_sdk_first_size();
    if (chunk < 256U || chunk > OTA_PACKET_SIZE) return D_ERR_IO;
    if (chunk > response.length) chunk = response.length;
    /* 保留已检查的前缀，继续填满首包而不增加第二个下载缓冲；
     * 后续各次读取均从缓冲偏移零开始。 */
    {
        uint32_t offset = OTA_PACKAGE_PREFIX;
        while (offset < chunk) {
            int count;
            if (ota_stopped()) return D_ERR_TIMEOUT;
            count = ota_transport_read(s_ota.buffer + offset, (uint16_t)(chunk - offset));
            if (count <= 0 || (uint32_t)count > chunk - offset) return D_ERR_NETWORK;
            offset += (uint32_t)count;
        }
    }
    ota_transport_hash_begin();
    while (received < response.length) {
        if (ota_stopped()) return D_ERR_TIMEOUT;
        ota_transport_hash_update(s_ota.buffer, chunk);
        body_crc = ota_crc16(body_crc, s_ota.buffer + (received ? 0U : 32U),
                            chunk - (received ? 0U : 32U));
        *error = OTA_ERROR_WRITE;
        result = ota_sdk_write(s_ota.buffer, (uint16_t)chunk);
        if (result != D_OK) return result;
        received += chunk;
        ota_port_lock();
        s_ota.snapshot.received = received;
        s_ota.snapshot.revision = ota_next(s_ota.snapshot.revision);
        ota_port_unlock();
        chunk = response.length - received;
        if (chunk > OTA_PACKET_SIZE) chunk = OTA_PACKET_SIZE;
        if (chunk) {
            *error = OTA_ERROR_TRANSPORT;
            result = ota_read_exact(chunk);
            if (result != D_OK) return result;
        }
    }
    *error = OTA_ERROR_PACKAGE;
    if (ota_transport_read(&extra, 1U) != 0) return D_ERR_NETWORK;
    if (ota_transport_hash_finish(digest) != D_OK) return D_ERR_IO;
    if (body_crc != package.body_crc ||
        (s_ota.candidate.has_sha256 && memcmp(digest, s_ota.candidate.sha256, sizeof(digest))))
        return D_ERR_INVALID_ARG;
    ota_transport_close();
    if (ota_stopped()) return D_ERR_TIMEOUT;
    *error = OTA_ERROR_VERIFY;
    ota_state(OTA_VERIFYING, OTA_ERROR_NONE, D_OK);
    result = ota_sdk_verify();
    if (result != D_OK) return result;
    if (ota_stopped()) return D_ERR_TIMEOUT;
    *error = OTA_ERROR_COMMIT;
    ota_state(OTA_COMMITTING, OTA_ERROR_NONE, D_OK);
    return ota_sdk_commit();
}

/**
 * @brief 执行检查或已确认升级，并在资源收尾后发布最终任务状态。
 * @param[in] context 以指针值编码的升级标志，非零表示已确认升级。
 * @note 提交成功保留维护至重启；不确定收尾持续保留原任务，禁止新事务进入。
 */
static void ota_worker(void *context)
{
    uint8_t upgrading = (uintptr_t)context != 0U;
    uint8_t clean;
    ota_error_e error = OTA_ERROR_NETWORK_TIME;
    d_err_t result = ota_port_ready();
    ota_state_e finished;
    if (result == D_OK && !upgrading) {
        error = OTA_ERROR_TRANSPORT;
        result = ota_check_candidate();
    } else if (result == D_OK) {
        error = OTA_ERROR_MAINTENANCE;
        ota_port_lock();
        s_ota.snapshot.maintenance = 1U;
        ota_port_unlock();
        result = s_ota.maintenance.begin();
        if (result == D_OK) result = s_ota.maintenance.wait(OTA_PREPARE_MS);
        if (result == D_OK && !ota_stopped()) {
            ota_state(OTA_WRITING, OTA_ERROR_NONE, D_OK);
            result = ota_download(&error);
        } else if (result == D_OK) result = D_ERR_TIMEOUT;
    }
    ota_transport_close();
    if (upgrading && result == D_OK) {
        /* 提交已经生效，迟到成功仍属于当前事务；
         * 保留维护至重启，不恢复业务运行。 */
        if (ota_sdk_close() != D_OK) {
            ota_state(OTA_UNCERTAIN, OTA_ERROR_CLEANUP, D_ERR_BUSY);
            while (ota_sdk_close() != D_OK) ota_port_wait(100U);
        }
        s_ota.sdk_owned = 0U;
        ota_state(OTA_REBOOT_PENDING, OTA_ERROR_NONE, D_OK);
        ota_port_wait(1500U);
        ota_port_reboot();
        return;
    }
    clean = ota_cleanup();
    if (!clean) {
        ota_state(OTA_UNCERTAIN, OTA_ERROR_CLEANUP, result == D_OK ? D_ERR_BUSY : result);
        /* 保留所有权并持续观察迟到的停止或保存完成；
         * 等待期间不受理重试或新的 Flash 操作。 */
        do { ota_port_wait(100U); clean = ota_cleanup(); } while (!clean);
    }
    if (ota_stopped()) result = D_ERR_TIMEOUT;
    ota_port_lock();
    s_ota.snapshot.state = result == D_OK ? OTA_CONFIRM : s_ota.cancelled ? OTA_IDLE : OTA_FAILED;
    s_ota.snapshot.error = result == D_OK || s_ota.cancelled ? OTA_ERROR_NONE :
        s_ota.expired ? OTA_ERROR_DEADLINE : error;
    s_ota.snapshot.detail = s_ota.cancelled ? D_OK : result;
    s_ota.snapshot.can_retry = result != D_OK && !s_ota.cancelled;
    s_ota.snapshot.worker_active = 0U;
    s_ota.snapshot.revision = ota_next(s_ota.snapshot.revision);
    finished = s_ota.snapshot.state;
    ota_port_unlock();
    /* 发布工作任务完成后不再访问可变事务数据。 */
    ota_port_notify(finished);
}

/**
 * @brief 一次性建立 OTA 状态，并在有效时复制默认发布 URL；完整合同见 ota_service.h。
 */
d_err_t ota_service_init(void)
{
    const char *source = ota_port_default_url();
    unsigned length = 0U;
    ota_port_lock();
    if (s_ota.initialized) { ota_port_unlock(); return D_OK; }
    s_ota.initialized = 1U;
    s_ota.snapshot.generation = 1U;
    s_ota.snapshot.revision = 1U;
    if (source) while (length < OTA_URL_CAPACITY && source[length]) length++;
    if (length && length < OTA_URL_CAPACITY) {
        memcpy(s_ota.candidate.url, source, length + 1U);
        s_ota.snapshot.configured = 1U;
    }
    ota_port_unlock();
    return D_OK;
}

/**
 * @brief 在没有工作任务或其他维护占用时复制候选升级信息；完整合同见 ota_service.h。
 */
d_err_t ota_service_set_candidate(const ota_candidate_t *candidate, uint32_t *generation)
{
    ota_url_t parsed;
    unsigned index;
    if (generation) *generation = 0U;
    if (!candidate || ota_url_parse(candidate->url, &parsed) != D_OK ||
        !memchr(candidate->version, 0, sizeof(candidate->version)) ||
        (unsigned)candidate->vm_policy > OTA_VM_ERASE || candidate->has_sha256 > 1U ||
        candidate->expected_size > 16U * 1024U * 1024U ||
        (candidate->board && candidate->board != ota_port_board())) return D_ERR_INVALID_ARG;
    for (index = 0U; candidate->version[index]; index++)
        if ((unsigned char)candidate->version[index] < 0x20U) return D_ERR_INVALID_ARG;
    ota_port_lock();
    if (!s_ota.initialized) { ota_port_unlock(); return D_ERR_NOT_READY; }
    if (s_ota.snapshot.worker_active || s_ota.snapshot.maintenance || s_ota.shutdown
        ) {
        ota_port_unlock(); return D_ERR_BUSY;
    }
    s_ota.candidate = *candidate;
    s_ota.snapshot.generation = ota_next(s_ota.snapshot.generation);
    s_ota.snapshot.revision = ota_next(s_ota.snapshot.revision);
    s_ota.snapshot.configured = 1U;
    s_ota.snapshot.state = OTA_IDLE;
    s_ota.snapshot.error = OTA_ERROR_NONE;
    s_ota.snapshot.detail = D_OK;
    s_ota.snapshot.received = s_ota.snapshot.total = 0U;
    s_ota.snapshot.can_retry = 0U;
    s_ota.snapshot.vm_policy = candidate->vm_policy;
    memcpy(s_ota.snapshot.version, candidate->version, sizeof(candidate->version));
    if (generation) *generation = s_ota.snapshot.generation;
    ota_port_unlock();
    return D_OK;
}

/**
 * @brief 空闲时复制产品维护回调描述，允许检查或失败完成后替换；完整合同见 ota_service.h。
 */
d_err_t ota_service_register_maintenance(const ota_maintenance_backend_t *backend)
{
    if (!backend || !backend->begin || !backend->wait || !backend->release) return D_ERR_INVALID_ARG;
    ota_port_lock();
    if (s_ota.snapshot.worker_active) { ota_port_unlock(); return D_ERR_BUSY; }
    s_ota.maintenance = *backend;
    ota_port_unlock();
    return D_OK;
}

/**
 * @brief 原子受理一次检查或升级，再创建后台工作任务。
 * @param[in] upgrade 非零表示确认升级，0 表示重新检查。
 * @param[in] generation 确认升级时要求匹配的已检查代次。
 * @param[out] request 可选输出，记录本次受理编号。
 * @return 创建成功返回 D_OK；准入或创建失败返回对应错误。
 * @note 重查更新代次，防止同一 URL 下的新对象继承旧确认。
 */
static d_err_t ota_start(uint8_t upgrade, uint32_t generation, uint32_t *request)
{
    d_err_t result;
    if (request) *request = 0U;
    ota_port_lock();
    if (!s_ota.initialized || !s_ota.snapshot.configured) {
        ota_port_unlock(); return D_ERR_NOT_READY;
    }
    if (s_ota.snapshot.worker_active || s_ota.snapshot.maintenance || s_ota.shutdown
        ) {
        ota_port_unlock(); return D_ERR_BUSY;
    }
    if (upgrade && (generation != s_ota.snapshot.generation ||
        s_ota.snapshot.state != OTA_CONFIRM || !s_ota.maintenance.begin)) {
        ota_port_unlock(); return D_ERR_NOT_READY;
    }
    s_ota.snapshot.request = ota_next(s_ota.snapshot.request);
    if (!upgrade) {
        /* 同一 URL 重新核验可能得到不同对象；
         * 确认必须绑定已完成的本次检查，不能只绑定 URL。 */
        s_ota.snapshot.generation = ota_next(s_ota.snapshot.generation);
        s_ota.snapshot.total = 0U;
    }
    s_ota.snapshot.revision = ota_next(s_ota.snapshot.revision);
    s_ota.is_upgrade = upgrade;
    s_ota.snapshot.worker_active = 1U;
    s_ota.snapshot.can_retry = 0U;
    s_ota.snapshot.received = 0U;
    s_ota.snapshot.state = upgrade ? OTA_PREPARING : OTA_CHECKING;
    s_ota.snapshot.error = OTA_ERROR_NONE;
    s_ota.snapshot.detail = D_OK;
    s_ota.cancelled = s_ota.expired = 0U;
    s_ota.started = ota_port_now();
    if (request) *request = s_ota.snapshot.request;
    ota_port_unlock();
    result = ota_port_start(ota_worker, (void *)(uintptr_t)upgrade);
    if (result != D_OK) {
        ota_port_lock();
        s_ota.snapshot.worker_active = 0U;
        s_ota.snapshot.can_retry = 1U;
        s_ota.snapshot.state = OTA_FAILED;
        s_ota.snapshot.error = OTA_ERROR_SOURCE;
        s_ota.snapshot.detail = result;
        s_ota.snapshot.revision = ota_next(s_ota.snapshot.revision);
        ota_port_unlock();
    }
    return result;
}

/**
 * @brief 受理候选检查并为此次重新核验更新代次；完整合同见 ota_service.h。
 */
d_err_t ota_service_check(uint32_t *request) { return ota_start(0U, 0U, request); }
/**
 * @brief 基于已完成检查的代次，受理真实升级事务；完整合同见 ota_service.h。
 */
d_err_t ota_service_confirm(uint32_t generation, uint32_t *request)
{ return ota_start(1U, generation, request); }

/**
 * @brief 请求取消仍处于 CHECKING 的匹配检查任务；完整合同见 ota_service.h。
 */
d_err_t ota_service_cancel_check(uint32_t request)
{
    d_err_t result = D_ERR_BUSY;
    ota_port_lock();
    if (request == s_ota.snapshot.request && s_ota.snapshot.worker_active &&
        s_ota.snapshot.state == OTA_CHECKING) { s_ota.cancelled = 1U; result = D_OK; }
    ota_port_unlock();
    return result;
}

/**
 * @brief 先观察事务期限，再复制状态快照；完整合同见 ota_service.h。
 */
void ota_service_get_snapshot(ota_snapshot_t *out)
{
    if (!out) return;
    ota_service_observe_deadline();
    ota_port_lock();
    *out = s_ota.snapshot;
    ota_port_unlock();
}

/**
 * @brief 读取 OTA 工作任务是否仍在途；完整合同见 ota_service.h。
 */
int ota_service_is_busy(void)
{
    int busy;
    ota_port_lock(); busy = s_ota.snapshot.worker_active; ota_port_unlock();
    return busy;
}

/**
 * @brief 检查已确认升级是否持有维护准入；完整合同见 ota_service.h。
 */
int ota_service_maintenance_active(void)
{
    int active;
    ota_port_lock();
    active = s_ota.snapshot.maintenance || (s_ota.snapshot.worker_active && s_ota.is_upgrade);
    ota_port_unlock();
    return active;
}

/**
 * @brief 与 OTA 和外部维护原子互斥地申请软件关机准入；完整合同见 ota_service.h。
 */
d_err_t ota_service_shutdown_begin(void)
{
    ota_port_lock();
    if (s_ota.shutdown || s_ota.snapshot.worker_active || s_ota.snapshot.maintenance
        ) {
        ota_port_unlock(); return D_ERR_BUSY;
    }
    s_ota.shutdown = 1U;
    ota_port_unlock();
    return D_OK;
}
/**
 * @brief 解除软件关机准入占用；完整合同见 ota_service.h。
 */
void ota_service_shutdown_cancel(void)
{ ota_port_lock(); s_ota.shutdown = 0U; ota_port_unlock(); }

