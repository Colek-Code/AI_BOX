/** @file qwen_activation.h @brief 激活命令与值快照，存储和网络 I/O 由 Qwen 工作任务拥有。 */
#ifndef QWEN_ACTIVATION_H
#define QWEN_ACTIVATION_H
#include "framework/d_error.h"
#include <stdint.h>
#include <stddef.h>

#define QWEN_ACTIVATION_CODE_SIZE 17U
typedef enum {
    QWEN_CODE_UI = 1, QWEN_CODE_FACTORY = 2, QWEN_CODE_STATIC = 3,
    QWEN_CODE_PROGRAMMER = 4
} qwen_code_source_e;
typedef enum {
    QWEN_ACT_ERROR_NONE, QWEN_ACT_ERROR_CODE, QWEN_ACT_ERROR_NETWORK,
    QWEN_ACT_ERROR_HTTP, QWEN_ACT_ERROR_RESPONSE, QWEN_ACT_ERROR_CONFIG,
    QWEN_ACT_ERROR_STORAGE, QWEN_ACT_ERROR_VERSION, QWEN_ACT_ERROR_DENIED,
    QWEN_ACT_ERROR_IDENTITY, QWEN_ACT_ERROR_SOURCE_EMPTY,
    QWEN_ACT_ERROR_SOURCE_READ, QWEN_ACT_ERROR_SOURCE_CRC, QWEN_ACT_ERROR_STORAGE_READ
} qwen_activation_error_e;

typedef enum {
    QWEN_ACT_NEED_CODE, QWEN_ACT_QUEUED, QWEN_ACT_REQUESTING,
    QWEN_ACT_READY, QWEN_ACT_FAILED, QWEN_ACT_READING_CODE
} qwen_activation_state_e;

typedef struct {
    qwen_activation_state_e state;
    qwen_activation_error_e error;
    uint32_t revision;
    uint32_t request;
    uint8_t source;
    uint8_t usable;
    uint8_t busy;
    uint8_t save_pending;
    uint8_t source_pending;
} qwen_activation_snapshot_t;

typedef int (*qwen_activation_admit_fn)(uint32_t generation);

/**
 * @brief 校验恰好 16 位的激活码格式。
 * @param[in] code 待验证零结尾激活码。
 * @return 仅数字及排除 I、L、O、U 的大写字母且恰好零结尾时返回 1，否则为 0。
 * @note 实现位于 qwen_auth_store.c。
 */
int qwen_auth_code_valid(const char *code);
/**
 * @brief 只建立分阶段激活状态及配置摘要，不读取 VM。
 * @return 成功或已准备返回 D_OK；接入未配置或功能关闭返回 D_ERR_NOT_READY；型号或摘要输入无效返回 D_ERR_INVALID_ARG。
 * @note 启动阶段串行调用，后续 I/O 由工作任务完成。
 */
d_err_t qwen_activation_init(void);
/**
 * @brief 由 Qwen 或工厂任务完成阶段初始化，按来源读取已有记录。
 * @return 完成初始化返回 D_OK；接入或阶段准备未就绪返回 D_ERR_NOT_READY。
 * @note 加载失败或版本保护主要通过快照呈现，D_OK 不表示已有可用凭据；PROGRAMMER 来源把本地读取留给 prepare_source。
 */
d_err_t qwen_activation_worker_init(void);
/**
 * @brief 在激活锁内复制状态值。
 * @param[out] snapshot 状态输出；为空时不处理。
 */
void qwen_activation_get_snapshot(qwen_activation_snapshot_t *snapshot);
/**
 * @brief 在 UI 激活来源下非阻塞复制新激活码并排队。
 * @param[in] code 格式有效的 16 位激活码。
 * @return 受理返回 D_OK；来源或码无效、未准备、写保护或忙返回对应错误。
 * @note 新显式输入会替换待处理候选；服务所有者还需唤醒自己的工作任务。
 */
d_err_t qwen_activation_submit(const char *code);
/**
 * @brief 重试保留的待存凭据、本地来源读取或有效激活码。
 * @return 已排队返回 D_OK；未准备、写保护、忙或没有可重试内容返回对应错误。
 * @note 保留的已验证凭据可离线存盘，不必重复访问网络；调用者负责唤醒工作任务。
 */
d_err_t qwen_activation_retry(void);
/**
 * @brief 在后台准备占用期间核验 PROGRAMMER 本地身份与码，并尝试恢复匹配 VM 凭据。
 * @param[in] admitted PROGRAMMER 分支要求的代次准入探针。
 * @param[in] generation 当前后台准备占用的会话代次。
 * @return 准备成功或非 PROGRAMMER 来源返回 D_OK；准入撤销或无可处理来源返回 D_ERR_NOT_READY；来源读取失败返回 D_ERR_IO。
 * @note 先执行本地 I/O，之后重新核对 needs_network 再应用 WiFi/NTP 准入；I/O 不在激活锁内。
 */
d_err_t qwen_activation_prepare_source(qwen_activation_admit_fn admitted, uint32_t generation);
/**
 * @brief 推进一次后台激活请求，或保存已有已验证候选。
 * @param[in] admitted 非空会话准入探针。
 * @param[in] generation 本次工作占用的会话代次。
 * @param[in] now_ms 设备当前毫秒值，用于重试期限。
 * @return 成功返回 D_OK；当前无工作、未就绪、准入失效或重试尚未到期返回 D_ERR_NOT_READY；其余为请求、解析或存盘错误。
 * @note 需要工作任务准备占用；HTTP 期间闹铃或维护可撤销会话，但已签发且验证的凭据仍完成受检保存，随后由服务重新核对维护。存盘失败保留候选。
 */
d_err_t qwen_activation_step(qwen_activation_admit_fn admitted,
                             uint32_t generation, uint32_t now_ms);
/**
 * @brief 检查已初始化且可用凭据没有被待处理或在途工作遮挡。
 * @return 运行凭据可用返回非零。
 */
int qwen_activation_runtime_ready(void);
/**
 * @brief 检查是否有待激活码或待读取的本地来源。
 * @return 任一待处理存在返回非零。
 */
int qwen_activation_needs_work(void);
/**
 * @brief 检查当前工作是否真正需要网络激活。
 * @return 存在待激活且没有本地来源或存盘待处理时返回非零。
 * @note 已验证的保留候选仅需存盘，不要求网络或 NTP。
 */
int qwen_activation_needs_network(void);
/**
 * @brief 记录服务器拒绝，撤销可用状态并阻止同一 PROGRAMMER 身份直接恢复。
 */
void qwen_activation_rejected(void);
/**
 * @brief 仅由独立工厂固件同步写入激活码并回读验证。
 * @param[in] code 有效 16 位激活码。
 * @return 工厂配置下返回初始化或存盘实际结果；非工厂配置或码无效返回 D_ERR_INVALID_ARG。
 * @note 调用者为独立工厂工作任务，不可作为普通前台写 VM 入口。
 */
d_err_t qwen_activation_factory_write(const char *code);
#endif
