/** @file product_ui_alarm_service.h @brief 唯一闹钟运行时，异步存储完成确认与非阻塞音频请求分开。 */
#ifndef PRODUCT_UI_ALARM_SERVICE_H
#define PRODUCT_UI_ALARM_SERVICE_H
#include "product_ui_alarm_model.h"

typedef struct {
    product_ui_clock_snapshot_t clock;
    uint32_t revision;
    uint32_t group_generation;
    uint32_t request_id;
    uint32_t completed_id;
    uint32_t persisted_revision;
    uint32_t next_id;
    int64_t next_utc;
    uint16_t ring_mask;
    uint16_t stop_mask;
    d_err_t timer_error;
    d_err_t storage_error;
    d_err_t load_error;
    d_err_t audio_error;
    uint8_t loaded;
    uint8_t timer_ready;
    uint8_t storage_ready;
    uint8_t storage_protected;
    uint8_t saving;
    uint8_t dirty;
    uint8_t quiescing;
    uint8_t audio_owned;
    uint8_t audio_playing;
    uint8_t count;
    uint8_t enabled_count;
    uint8_t missed_count;
    uint8_t volume;
    uint8_t has_record;
} product_ui_alarm_snapshot_t;

/** 可选本地操作观察器，不存储传输或会话身份。 */
typedef enum {
    PRODUCT_UI_ALARM_RECEIPT_NONE = 0,
    PRODUCT_UI_ALARM_RECEIPT_PENDING,
    PRODUCT_UI_ALARM_RECEIPT_SAVED,
    PRODUCT_UI_ALARM_RECEIPT_FAILED,
    PRODUCT_UI_ALARM_RECEIPT_SUPERSEDED,
} product_ui_alarm_receipt_state_e;
typedef struct {
    uint32_t operation;
    uint32_t id;
    uint32_t rule_revision;
    uint32_t revision;
    product_ui_alarm_receipt_state_e state;
    d_err_t error;
} product_ui_alarm_receipt_t;

/**
 * @brief 在 AI 闹钟功能开启时受理一个带本地持久化观察回执的变更。
 * @param[in] command 仅允许编辑、删除或关闭既有规则。
 * @param[in] clock_revision 依赖时间的变更须提供预期可信时钟代次；零仅供不依赖墙钟的变更。
 * @param[out] receipt 必填，验证动作后先清零；受理成功接收本地操作编号及待保存状态。
 * @return 成功受理为 D_OK；不支持的动作、空参数、存储／计时未就绪、旧时钟或在途跟踪操作返回对应错误。
 * @note 无传输／会话身份；FAILED 操作须显式重试或由更新的规则取代后才可提交另一跟踪变更。D_OK 不表示保存完成。
 */
d_err_t product_ui_alarm_submit_tracked(const product_ui_alarm_command_t *command,
    uint32_t clock_revision, product_ui_alarm_receipt_t *receipt);
/**
 * @brief 为匹配且仍未被取代的 FAILED 跟踪操作受理一次保存重试。
 * @param[in] operation get_receipt 或 submit_tracked 返回的本地操作编号。
 * @return 已 SAVED 或成功受理为 D_OK，操作不存在为 NOT_FOUND，状态冲突 BUSY，资源未就绪 NOT_READY。
 * @note 功能开启时实现；相同规则身份由精确规则内容及版本核对，不按全局保存计数冒充本操作完成。
 */
d_err_t product_ui_alarm_retry_tracked(uint32_t operation);
/**
 * @brief 读取本地跟踪回执，并把已被活动规则取代的失败操作标记为 SUPERSEDED。
 * @param[out] receipt 接收值副本，NULL 时忽略。
 * @note AI 闹钟功能开启时实现；读取可能更新回执状态。
 */
void product_ui_alarm_get_receipt(product_ui_alarm_receipt_t *receipt);
/**
 * @brief 在同一闹钟锁下复制状态及全部十六槽记录。
 * @param[out] snapshot 接收服务状态，不能为空。
 * @param[out] records 至少 ALARM_COUNT 项的调用方数组，不能为空。
 * @note AI 闹钟功能开启时实现；任一指针为空时均不写入，值副本不借用内部存储。
 */
void product_ui_alarm_copy_set(product_ui_alarm_snapshot_t *snapshot,
    product_ui_alarm_record_t records[PRODUCT_UI_ALARM_COUNT]);

/**
 * @brief 一次性绑定时间域桥接、启动软件时钟、保存 worker 及独立调度。
 * @return 时钟与计时器就绪返回 D_OK，失败返回 IO；重复调用返回当前 timer_error。
 * @note 加载在后台进行，返回成功不等于记录已加载或可持久化；worker 失败时仅本次会话规则可用且不会写 VM。
 */
d_err_t product_ui_alarm_init(void);
/**
 * @brief 在闹钟自旋锁内复制阶段、计数、保存及音频投影。
 * @param[out] snapshot 接收值副本，NULL 时忽略。
 * @note next_utc 只有有效时钟下才填充；audio_owned 与 audio_playing 分别报告保留资源及已启动播放。
 */
void product_ui_alarm_get_snapshot(product_ui_alarm_snapshot_t *snapshot);
/**
 * @brief 按稳定非零规则编号读取一个完整记录。
 * @param[in] id 规则编号，不是物理槽或可视下标。
 * @param[out] record 成功接收值副本，失败不改。
 * @return 成功返回 D_OK；空输出或零编号为 INVALID_ARG，未加载为 NOT_READY，未找到为 NOT_FOUND。
 */
d_err_t product_ui_alarm_get_record(uint32_t id, product_ui_alarm_record_t *record);
/**
 * @brief 按有效槽顺序读取逻辑下标对应的完整记录。
 * @param[in] index 有效记录中的零起始逻辑下标。
 * @param[out] record 成功接收值副本，失败不改。
 * @return 成功 D_OK；空输出或下标超容量 INVALID_ARG，未加载 NOT_READY，无对应条目 NOT_FOUND。
 */
d_err_t product_ui_alarm_get_at(uint8_t index, product_ui_alarm_record_t *record);
/**
 * @brief 同步提交模型变更，变化后登记后台保存并按组变化请求音频取消。
 * @param[in] command 调用期间借用的完整命令。
 * @param[out] request 可为空，预置零；通过准入后返回当前保存请求编号，不单独证明命令或保存成功。
 * @param[out] affected_id 可为空，预置零；成功时按命令返回受影响规则编号。
 * @return 返回模型／准入实际结果；未知版本为 ALARM_ERR_VERSION，未加载或静默为 NOT_READY。
 * @note RAM 修改不等待持久化，普通 UI 变更不以 worker 就绪作为全部准入条件；调用方须继续观察 storage_ready、dirty、completed_id 及 storage_error。
 */
d_err_t product_ui_alarm_submit(const product_ui_alarm_command_t *command, uint32_t *request, uint32_t *affected_id);
/**
 * @brief 显式重新加载未完成初始读取，或重试当前脏模型保存。
 * @param[out] request 可为空；仅受理成功写入新请求编号，失败保持原值。
 * @return 成功受理返回 D_OK；未知版本为 ALARM_ERR_VERSION，无 worker 或静默为 NOT_READY。
 * @note 成功不代表已重读或写入；没有确认记录时显式重试允许建立当前模型记录。
 */
d_err_t product_ui_alarm_retry_save(uint32_t *request);
/**
 * @brief 按当前组代次重试停止或允许下一轮重新提交闹铃请求。
 * @param[in] group_generation 当前观察到的响铃组代次。
 * @return 受理返回 D_OK；静默、旧组或无可重试条件返回 BUSY。
 * @note 不等待音频开始或资源释放，继续读快照确认。
 */
d_err_t product_ui_alarm_retry_audio(uint32_t group_generation);
/**
 * @brief 受理共享提示音试听，音量必须在一至一百。
 * @param[in] volume 试听音量百分比。
 * @return 音量无效返回 INVALID_ARG，其余原样返回试听受理结果。
 */
d_err_t product_ui_alarm_preview(uint8_t volume);
/**
 * @brief 请求取消当前试听，不等待其资源释放。
 */
void product_ui_alarm_stop_preview(void);
/**
 * @brief 读取试听来源是否仍待处理、播放或占有资源。
 * @param[out] active 接收活动标志；空指针返回 INVALID_ARG。
 * @return 原样返回试听状态接口结果。
 */
d_err_t product_ui_alarm_preview_status(uint8_t *active);
/**
 * @brief 冻结时钟校正、结算实例后关闭新操作，响铃组转入停止意图。
 * @note 此路径用于关机等明确要求停止的场景；不是不打断闹铃的维护准入接口。
 */
void product_ui_alarm_quiesce_begin(void);
/**
 * @brief 在同一锁内检查已加载、未静默且无响铃组后取得维护准入。
 * @return 成功返回 D_OK；不满足任一准入条件返回 BUSY。
 * @note 已有响铃不会在此被强制停止；成功后冻结时钟校正并取消已发出的旧音频尝试。
 */
d_err_t product_ui_alarm_maintenance_begin(void);
/**
 * @brief 解除时钟校正及闹钟静默，并立即推进一次真实状态。
 */
void product_ui_alarm_quiesce_cancel(void);
/**
 * @brief 限时等待当前已知脏数据、STOPPING 实例及闹铃音频资源归还。
 * @param[in] timeout_ms 最大等待毫秒数，零只检查当前状态。
 * @return 当前均空闲为 D_OK；保留存储／音频错误原样返回，保护或无 worker 且有脏数据为 IO，超时为 TIMEOUT。
 * @note 成功不单独证明初始加载已完成；超时或错误不删除静态状态及在途 IO 缓冲。
 */
d_err_t product_ui_alarm_wait_idle(uint32_t timeout_ms);
/**
 * @brief 先静默并排空，再停止计时器并要求保存 worker 退出。
 * @param[in] timeout_ms 排空及 worker 退出共用的总毫秒预算。
 * @return 排空失败原样返回，worker 已停为 D_OK，总期限到达为 TIMEOUT。
 * @note 错误或超时保留同步对象及所有者状态，调用方不能据此释放在途资源。
 */
d_err_t product_ui_alarm_service_stop(uint32_t timeout_ms);
#endif
