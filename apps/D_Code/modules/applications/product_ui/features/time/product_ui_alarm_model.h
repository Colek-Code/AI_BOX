/**
 * @file product_ui_alarm_model.h
 * @brief 有限闹钟规则及实例模型，无 UI、SDK 或存储所有权。
 * @author D_Code
 * @date 2026-09-21
 */
#ifndef PRODUCT_UI_ALARM_MODEL_H
#define PRODUCT_UI_ALARM_MODEL_H
#include "product_ui_clock_model.h"
#include "framework/d_error.h"

#define PRODUCT_UI_ALARM_COUNT 16U
#define PRODUCT_UI_ALARM_RECORD_SIZE 1536U
#define PRODUCT_UI_ALARM_LATE_SECONDS 120U
#define PRODUCT_UI_ALARM_RING_MS 300000U
#define PRODUCT_UI_ALARM_SNOOZE_MINUTES 5U
#define PRODUCT_UI_ALARM_SNOOZE_SECONDS (PRODUCT_UI_ALARM_SNOOZE_MINUTES * 60U)
#define PRODUCT_UI_ALARM_SNOOZE_MS (PRODUCT_UI_ALARM_SNOOZE_SECONDS * 1000U)
#define PRODUCT_UI_ALARM_SNOOZE_LIMIT 3U
#define PRODUCT_UI_ALARM_DEFAULT_VOLUME 60U
/* 较新固件记录的保护错误，区别于损坏或已证实缺失。 */
#define PRODUCT_UI_ALARM_ERR_VERSION ((d_err_t)-40)

typedef enum {
    PRODUCT_UI_ALARM_IDLE = 0,
    PRODUCT_UI_ALARM_WAIT_AUDIO,
    PRODUCT_UI_ALARM_RINGING,
    PRODUCT_UI_ALARM_STOPPING,
    PRODUCT_UI_ALARM_SNOOZED,
    PRODUCT_UI_ALARM_MISSED,
    PRODUCT_UI_ALARM_DONE,
    PRODUCT_UI_ALARM_TIME_CONFLICT,
} product_ui_alarm_instance_e;

typedef struct {
    product_ui_alarm_t rule;
    uint32_t rule_revision;
    uint32_t instance_generation;
    int64_t occurrence_utc;
    int64_t last_handled_utc;
    int64_t snooze_utc;
    int64_t next_regular_utc;
    uint8_t label_kind;
    uint8_t state;
    uint8_t snooze_count;
    uint8_t stop_action;
    uint8_t notice;
} product_ui_alarm_record_t;

typedef struct {
    product_ui_alarm_record_t records[PRODUCT_UI_ALARM_COUNT];
    uint32_t next_id;
    uint8_t volume;
} product_ui_alarm_saved_t;

typedef struct {
    product_ui_alarm_saved_t saved;
    product_ui_clock_snapshot_t clock;
    uint64_t deadlines[PRODUCT_UI_ALARM_COUNT];
    uint32_t revision;
    uint32_t group_generation;
    uint16_t restore_mask;
    uint8_t has_clock;
} product_ui_alarm_model_t;

typedef enum {
    PRODUCT_UI_ALARM_EDIT = 0,
    PRODUCT_UI_ALARM_DELETE,
    PRODUCT_UI_ALARM_ENABLE,
    PRODUCT_UI_ALARM_STOP_GROUP,
    PRODUCT_UI_ALARM_SNOOZE_GROUP,
    PRODUCT_UI_ALARM_CLEAR_NOTICE,
    PRODUCT_UI_ALARM_VOLUME,
} product_ui_alarm_action_e;

typedef struct {
    product_ui_alarm_action_e action;
    product_ui_alarm_t rule;
    uint32_t id;
    uint32_t expected_revision;
    uint32_t group_generation;
    uint8_t enabled;
    uint8_t volume;
    uint8_t resolve_once;
    uint8_t label_kind;
} product_ui_alarm_command_t;

/**
 * @brief 建立新启动模型并复制已验证保存值，原在响／待音频／停止中实例改为错过提醒。
 * @param[out] model 调用方持有的模型，NULL 时忽略。
 * @param[in] saved 可为空的已完整校验保存值，本函数不重新解码校验。
 * @note 贪睡实例保留待恢复标志，须取得可信时钟后重建单调截止时间；不恢复已在途音频资源。
 */
void product_ui_alarm_model_init(product_ui_alarm_model_t *model, const product_ui_alarm_saved_t *saved);
/**
 * @brief 仅向完整空模型填入六条关闭状态的默认工作日闹钟。
 * @return 成功返回 D_OK，空模型指针为 INVALID_ARG，已有任一记录为 BUSY。
 * @note 调用方必须先证明存储确实缺失，不能因损坏、未读到或未知版本而用默认值覆盖。
 */
d_err_t product_ui_alarm_model_seed_defaults(product_ui_alarm_model_t *model);
/**
 * @brief 接纳非过期时钟，处理校正、贪睡、到期／错过及单调响铃期限。
 * @param[in,out] model 调用方串行保护的模型。
 * @param[in] clock 本次借用的可信时钟快照。
 * @note 拒绝越界有效 UTC、倒退的单调计数或旧内容代次；手动调度代次变化重排后续闹钟，普通网络校正保留已处理抑制。
 */
void product_ui_alarm_model_advance(product_ui_alarm_model_t *model, const product_ui_clock_snapshot_t *clock);
/**
 * @brief 按规则版本或响铃组代次执行有界编辑、删除、启用、组停止／贪睡等变更。
 * @param[in,out] model 调用方串行保护的模型。
 * @param[in] command 本次借用的完整命令。
 * @param[out] affected_id 可为空，调用前清零；有记录身份的成功操作写入编号。
 * @return 成功返回 D_OK；参数、未就绪、记录不存在、容量或版本冲突返回对应错误。
 * @note 停止／贪睡只转入 STOPPING，须在真实音频停止完成后再调用 audio_stopped；模型不操作硬件。
 */
d_err_t product_ui_alarm_model_submit(product_ui_alarm_model_t *model,
    const product_ui_alarm_command_t *command, uint32_t *affected_id);
/**
 * @brief 只为当前响铃组代次把 WAIT_AUDIO 转为 RINGING，并重置响铃期限。
 * @param[in,out] model 调用方串行保护的模型。
 * @param[in] group_generation 真实音频启动确认所属的响铃组代次。
 */
void product_ui_alarm_model_audio_started(product_ui_alarm_model_t *model, uint32_t group_generation);
/**
 * @brief 在真实音频停止确认后提交所有 STOPPING 实例的待完成动作。
 * @param[in,out] model 调用方串行保护且仍对应当前音频所有权的模型。
 * @note DONE／MISSED 才禁用单次规则；贪睡在此建立新的单调截止及持久 UTC，非法时钟转入冲突。
 */
void product_ui_alarm_model_audio_stopped(product_ui_alarm_model_t *model);
/**
 * @brief 读取仍待音频或正在响铃的实例槽位集合。
 * @param[in] model 借用模型。
 * @return 十六位槽掩码，空模型为零；不等于硬件正在播放的证明。
 */
uint16_t product_ui_alarm_model_ring_mask(const product_ui_alarm_model_t *model);
/**
 * @brief 读取仍等待真实音频停止确认的实例集合。
 * @param[in] model 借用模型。
 * @return STOPPING 槽位掩码，空模型为零。
 */
uint16_t product_ui_alarm_model_stop_mask(const product_ui_alarm_model_t *model);
/**
 * @brief 按非零规则编号查找固定记录槽。
 * @param[in] model 借用模型。
 * @param[in] id 稳定规则编号，不是可视行下标。
 * @return 找到返回物理槽下标，空模型、零编号或未找到返回 -1。
 */
int product_ui_alarm_model_find(const product_ui_alarm_model_t *model, uint32_t id);
/**
 * @brief 统一验证规则、标签 UTF-8、版本、实例字段及 UTC 关系。
 * @param[in] record 借用的完整记录。
 * @return 记录有效为非零；NULL 或任一校验失败为零；零编号槽视为空有效槽。
 * @note 不改输入，用于命令候选及持久记录解码。
 */
uint8_t product_ui_alarm_record_valid(const product_ui_alarm_record_t *record);
/**
 * @brief 为已校验且启用的单次、每周或双周规则寻找严格晚于给定时刻的执行点。
 * @param[in] rule 调用方已校验的规则值。
 * @param[in] after_utc 排他下界，至少为最小产品 UTC 减一，且早于最大产品 UTC。
 * @return 找到返回有效 UTC 秒，禁用、不可计算、超范围或没有后续日期返回零。
 * @note 重复规则最多搜索下一周或两周窗口，不读取当前时钟。
 */
int64_t product_ui_alarm_next_occurrence(const product_ui_alarm_t *rule, int64_t after_utc);
/**
 * @brief 以规则首周周一为锚点，计算指定日期属于双周的第几周。
 * @param[in] rule 合法双周规则。
 * @param[in] date 待判定的公历日期。
 * @param[out] week 成功写入 0 第一周或 1 第二周；失败不改。
 * @return 成功为 D_OK，规则、日期或输出无效为 INVALID_ARG。
 * @note 周一到周一的有符号整周差也支持锚点之前日期。
 */
d_err_t product_ui_alarm_week_index(const product_ui_alarm_t *rule,
    const product_ui_date_t *date, uint8_t *week);
/**
 * @brief 把可信日期所属周及用户确认周次转换为固定第一周周一。
 * @param[in] date 2024 至 2099 年的合法基准日期。
 * @param[in] week 0 表示当前是第一周，1 表示当前是第二周。
 * @param[out] anchor 成功接收第一周周一，可能位于前一公历年；失败不改。
 * @return 成功为 D_OK，日期、周次或转换无效为 INVALID_ARG。
 */
d_err_t product_ui_alarm_anchor_for_week(const product_ui_date_t *date,
    uint8_t week, product_ui_date_t *anchor);
/**
 * @brief 读取具名标签种类的静态中文文字。
 * @param[in] kind 标签种类编号。
 * @return 0 至 5 返回对应静态标签，其他值回退闹钟。
 */
const char *product_ui_alarm_label(uint8_t kind);
/**
 * @brief 把已验证保存值编码为 V3 固定小端记录并追加 CRC32。
 * @param[in] saved 本次借用的完整保存值。
 * @param[in] sequence A/B 保存环形序号。
 * @param[out] record 至少 RECORD_SIZE 字节；必要指针为空时不操作。
 * @note 空槽保持全零，显式编码字段不依赖目标结构体布局。
 */
void product_ui_alarm_encode(const product_ui_alarm_saved_t *saved, uint32_t sequence, uint8_t *record);
/**
 * @brief 验证 V1／V2／V3 记录，并按旧版本合同迁移双周字段及贪睡期限。
 * @param[in] record 至少 RECORD_SIZE 字节的完整记录。
 * @param[out] saved 解码输出；失败可能已部分改写，调用方不得消费。
 * @param[out] sequence 仅全部成功后写入保存序号。
 * @return 成功返回 D_OK；空参数为 INVALID_ARG，未知版本为 ALARM_ERR_VERSION，内容／CRC 无效为 IO。
 * @note V1 十分钟贪睡改为原受理时刻起的五分钟，不是每次重启新增五分钟。
 */
d_err_t product_ui_alarm_decode(const uint8_t *record, product_ui_alarm_saved_t *saved, uint32_t *sequence);
/**
 * @brief 以半周期差比较相邻 A/B 保存序号，支持回绕。
 * @param[in] left 候选新序号。
 * @param[in] right 既有序号。
 * @return 不同且 left 在 right 之后半周期内时为非零，否则为零。
 */
uint8_t product_ui_alarm_sequence_after(uint32_t left, uint32_t right);
#endif
