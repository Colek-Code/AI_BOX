/** @file ota_service.c @brief Request acceptance is distinct from Flash completion. */
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
    uint8_t is_upgrade; /* Transaction identity survives uncertain/timeout states. */
    uint8_t buffer[OTA_PACKET_SIZE];
} s_ota;

static uint32_t ota_next(uint32_t value) { return value + 1U ? value + 1U : 1U; }

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

static uint8_t ota_stopped(void)
{
    uint8_t stopped;
    ota_port_lock();
    if ((uint32_t)(ota_port_now() - s_ota.started) >= OTA_TRANSACTION_MS) s_ota.expired = 1U;
    stopped = s_ota.cancelled || s_ota.expired;
    ota_port_unlock();
    return stopped;
}

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

static d_err_t ota_response_check(const ota_http_response_t *response)
{
    /* SDK checks the actual inactive-bank size from app_core. Bound transport
     * input here before handing a header to the SDK. */
    if (response->status != 200U || !response->has_length ||
        response->length <= OTA_PACKAGE_PREFIX || response->length > 16U * 1024U * 1024U ||
        (s_ota.candidate.expected_size && response->length != s_ota.candidate.expected_size))
        return D_ERR_INVALID_ARG;
    return D_OK;
}

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
    /* HEAD alone can describe an HTML error page. Inspect only the fixed UFW
     * prefix before offering confirmation; no Flash/maintenance side effect. */
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
    /* Preserve the inspected prefix. Fill the remainder without a second
     * download buffer; all following reads start at buffer offset zero. */
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
        /* Commit is irreversible. A late success still belongs to this
         * transaction; retain maintenance until reboot instead of resuming. */
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
        /* Keep ownership, but continue observing late stop/save completion.
         * No retry or new Flash operation is admitted during this wait. */
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
    /* Do not touch mutable transaction data after publishing worker completion. */
    ota_port_notify(finished);
}

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
    if (s_ota.snapshot.worker_active || s_ota.snapshot.maintenance || s_ota.shutdown) {
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

d_err_t ota_service_register_maintenance(const ota_maintenance_backend_t *backend)
{
    if (!backend || !backend->begin || !backend->wait || !backend->release) return D_ERR_INVALID_ARG;
    ota_port_lock();
    if (s_ota.snapshot.worker_active) { ota_port_unlock(); return D_ERR_BUSY; }
    s_ota.maintenance = *backend;
    ota_port_unlock();
    return D_OK;
}

static d_err_t ota_start(uint8_t upgrade, uint32_t generation, uint32_t *request)
{
    d_err_t result;
    if (request) *request = 0U;
    ota_port_lock();
    if (!s_ota.initialized || !s_ota.snapshot.configured) {
        ota_port_unlock(); return D_ERR_NOT_READY;
    }
    if (s_ota.snapshot.worker_active || s_ota.snapshot.maintenance || s_ota.shutdown) {
        ota_port_unlock(); return D_ERR_BUSY;
    }
    if (upgrade && (generation != s_ota.snapshot.generation ||
        s_ota.snapshot.state != OTA_CONFIRM || !s_ota.maintenance.begin)) {
        ota_port_unlock(); return D_ERR_NOT_READY;
    }
    s_ota.snapshot.request = ota_next(s_ota.snapshot.request);
    if (!upgrade) {
        /* Revalidation can observe a different object at the same URL. A
         * confirmation belongs to the completed check, not merely the URL. */
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

d_err_t ota_service_check(uint32_t *request) { return ota_start(0U, 0U, request); }
d_err_t ota_service_confirm(uint32_t generation, uint32_t *request)
{ return ota_start(1U, generation, request); }

d_err_t ota_service_cancel_check(uint32_t request)
{
    d_err_t result = D_ERR_BUSY;
    ota_port_lock();
    if (request == s_ota.snapshot.request && s_ota.snapshot.worker_active &&
        s_ota.snapshot.state == OTA_CHECKING) { s_ota.cancelled = 1U; result = D_OK; }
    ota_port_unlock();
    return result;
}

void ota_service_get_snapshot(ota_snapshot_t *out)
{
    if (!out) return;
    ota_service_observe_deadline();
    ota_port_lock();
    *out = s_ota.snapshot;
    ota_port_unlock();
}

int ota_service_is_busy(void)
{
    int busy;
    ota_port_lock(); busy = s_ota.snapshot.worker_active; ota_port_unlock();
    return busy;
}

int ota_service_maintenance_active(void)
{
    int active;
    ota_port_lock();
    active = s_ota.snapshot.maintenance || (s_ota.snapshot.worker_active && s_ota.is_upgrade);
    ota_port_unlock();
    return active;
}

d_err_t ota_service_shutdown_begin(void)
{
    ota_port_lock();
    if (s_ota.shutdown || s_ota.snapshot.worker_active || s_ota.snapshot.maintenance) {
        ota_port_unlock(); return D_ERR_BUSY;
    }
    s_ota.shutdown = 1U;
    ota_port_unlock();
    return D_OK;
}
void ota_service_shutdown_cancel(void)
{ ota_port_lock(); s_ota.shutdown = 0U; ota_port_unlock(); }
