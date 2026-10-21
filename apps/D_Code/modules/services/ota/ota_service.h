/** @file ota_service.h @brief Single-owner, confirmed HTTPS OTA transaction. */
#ifndef D_OTA_SERVICE_H
#define D_OTA_SERVICE_H
#include "ota_protocol.h"

#define OTA_VERSION_CAPACITY 48U
#define OTA_TRANSACTION_MS 600000U
#define OTA_PREPARE_MS 10000U
typedef enum {
    OTA_IDLE, OTA_CHECKING, OTA_CONFIRM, OTA_PREPARING, OTA_WRITING,
    OTA_VERIFYING, OTA_COMMITTING, OTA_REBOOT_PENDING, OTA_FAILED, OTA_UNCERTAIN
} ota_state_e;
typedef enum { OTA_VM_UNKNOWN, OTA_VM_KEEP, OTA_VM_ERASE } ota_vm_policy_e;
typedef enum {
    OTA_ERROR_NONE, OTA_ERROR_SOURCE, OTA_ERROR_NETWORK_TIME, OTA_ERROR_TRANSPORT,
    OTA_ERROR_PACKAGE, OTA_ERROR_MAINTENANCE, OTA_ERROR_WRITE, OTA_ERROR_VERIFY,
    OTA_ERROR_COMMIT, OTA_ERROR_DEADLINE, OTA_ERROR_CLEANUP
} ota_error_e;
typedef struct {
    char url[OTA_URL_CAPACITY];
    char version[OTA_VERSION_CAPACITY];
    uint32_t expected_size; /* Zero means not supplied. */
    uint32_t board;         /* Zero means publisher did not supply it. */
    uint8_t sha256[32];
    uint8_t has_sha256;
    ota_vm_policy_e vm_policy;
} ota_candidate_t;
typedef struct {
    uint32_t request;
    uint32_t generation;
    uint32_t received;
    uint32_t total;
    uint32_t revision;
    ota_state_e state;
    ota_error_e error;
    d_err_t detail;
    ota_vm_policy_e vm_policy;
    char version[OTA_VERSION_CAPACITY];
    uint8_t worker_active;
    uint8_t maintenance;
    uint8_t configured;
    uint8_t can_retry;
} ota_snapshot_t;

/** Product coordination callbacks run on the OTA worker without its lock held.
 * release must retain ownership until pending work stops; BUSY is retried. */
typedef struct {
    d_err_t (*begin)(void);
    d_err_t (*wait)(uint32_t timeout_ms);
    d_err_t (*release)(void);
} ota_maintenance_backend_t;

/** Copies callbacks before any request starts; their code must remain valid. */
d_err_t ota_service_register_maintenance(const ota_maintenance_backend_t *backend);
d_err_t ota_service_init(void);
d_err_t ota_service_set_candidate(const ota_candidate_t *candidate, uint32_t *generation);
d_err_t ota_service_check(uint32_t *request);
/** Use generation from the completed check snapshot; every recheck renews it. */
d_err_t ota_service_confirm(uint32_t generation, uint32_t *request);
d_err_t ota_service_cancel_check(uint32_t request);
void ota_service_get_snapshot(ota_snapshot_t *out);
int ota_service_is_busy(void);
/** Confirmed upgrade admission/cleanup owns maintenance; a check never does,
 * even when its network worker is retained after the transaction deadline. */
int ota_service_maintenance_active(void);
/** Shared atomic admission with software shutdown; release on shutdown abort. */
d_err_t ota_service_shutdown_begin(void);
void ota_service_shutdown_cancel(void);
/** Observe a blocked SDK operation without releasing its data. */
void ota_service_observe_deadline(void);
#endif
