/** @file ota_service.h @brief 单所有者 HTTPS OTA，分别确认受理、Flash 完成与清理。 */
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
    uint32_t expected_size; /* 0 表示发布方未提供大小。 */
    uint32_t board;         /* 0 表示发布方未提供板型。 */
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

/** 产品协调回调运行于 OTA 工作任务且不持 OTA 锁。
 * begin 失败前可能只取得部分占用；release 仍会调用，须兼容未取得或部分取得。
 * release 出错时保留工作任务与维护准入，直到后续释放成功。
 * 提交成功后保留占用至重启，不调用 release。 */
typedef struct {
    d_err_t (*begin)(void);
    d_err_t (*wait)(uint32_t timeout_ms);
    d_err_t (*release)(void);
} ota_maintenance_backend_t;

/**
 * @brief 空闲时复制产品维护回调描述，允许检查或失败完成后替换。
 * @param[in] backend 被复制的 begin、wait、release 描述；描述本身不必持续存活。
 * @return 成功返回 D_OK；操作不完整返回 D_ERR_INVALID_ARG；任何工作任务仍在途时返回 D_ERR_BUSY。
 * @note 回调代码及其所有状态在注册期间和保留的收尾、待重启阶段均须有效；检查任务也阻止替换。
 */
d_err_t ota_service_register_maintenance(const ota_maintenance_backend_t *backend);
/**
 * @brief 一次性建立 OTA 状态，并在有效时复制默认发布 URL。
 * @return 当前实现返回 D_OK。
 */
d_err_t ota_service_init(void);
/**
 * @brief 在没有工作任务或其他维护占用时复制候选升级信息。
 * @param[in] candidate HTTPS URL、版本、板型、大小、可选摘要及 VM 声明。
 * @param[out] generation 可选输出；先置 0，成功时写入新候选代次。
 * @return 成功返回 D_OK；候选无效返回 D_ERR_INVALID_ARG；未初始化返回 D_ERR_NOT_READY；准入冲突返回 D_ERR_BUSY。
 * @note 只保存候选，不访问网络或写入 Flash；VM 声明不是本接口已实施的擦写策略。
 */
d_err_t ota_service_set_candidate(const ota_candidate_t *candidate, uint32_t *generation);
/**
 * @brief 受理候选检查并为此次重新核验更新代次。
 * @param[out] request 可选输出，准入前置 0；已准入则写入请求编号，任务创建失败也可能保留该编号。
 * @return 任务创建成功返回 D_OK；就绪、占用或创建失败返回相应错误。
 * @note 检查只读取 HTTP 元数据与固定 UFW 前缀，不申请产品维护或写 Flash。终态从快照确认。
 */
d_err_t ota_service_check(uint32_t *request);
/**
 * @brief 基于已完成检查的代次，受理真实升级事务。
 * @param[in] generation 本次已完成检查快照中的代次；每次重查都会更新。
 * @param[out] request 可选请求编号输出；准入前置 0，准入后写入编号。
 * @return 准入并创建任务成功返回 D_OK；代次、检查状态或维护回调未就绪返回 D_ERR_NOT_READY；占用或创建失败返回相应错误。
 * @note D_OK 仅表示受理；工作任务负责维护准备、下载写入、校验与提交，成功后保留占用直到重启。
 */
d_err_t ota_service_confirm(uint32_t generation, uint32_t *request);
/**
 * @brief 请求取消仍处于 CHECKING 的匹配检查任务。
 * @param[in] request 要取消的检查请求编号。
 * @return 受理返回 D_OK；请求不匹配或阶段不可取消返回 D_ERR_BUSY。
 * @note 不取消已确认升级；返回后网络与资源可能仍在收尾。
 */
d_err_t ota_service_cancel_check(uint32_t request);
/**
 * @brief 先观察事务期限，再复制状态快照。
 * @param[out] out 接收状态；为空时不处理。
 * @note UNCERTAIN 与超时不等于 SDK 已返回或资源释放，仍需检查 worker_active 与 maintenance。
 */
void ota_service_get_snapshot(ota_snapshot_t *out);
/**
 * @brief 读取 OTA 工作任务是否仍在途。
 * @return 在途返回非零，包括保留收尾与待重启阶段。
 */
int ota_service_is_busy(void);
/**
 * @brief 检查已确认升级是否持有维护准入。
 * @return 维护已取得或升级工作任务仍在途返回非零。
 * @note 只检查的任务即使超时保留网络工作也不拥有升级维护。
 */
int ota_service_maintenance_active(void);
/**
 * @brief 与 OTA 和外部维护原子互斥地申请软件关机准入。
 * @return 取得返回 D_OK；已有关机、工作任务或维护占用返回 D_ERR_BUSY。
 * @note 关机撤销时必须调用 shutdown_cancel。
 */
d_err_t ota_service_shutdown_begin(void);
/**
 * @brief 解除软件关机准入占用。
 */
void ota_service_shutdown_cancel(void);
/**
 * @brief 独立观察阻塞任务的绝对期限并发布 UNCERTAIN 诊断。
 * @note 不释放任务、SDK 上下文或缓冲；迟到完成仍属于原事务。
 */
void ota_service_observe_deadline(void);
#endif
