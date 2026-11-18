/** @file product_ui_pomodoro_model.h @brief 注入单调时钟的有限番茄钟模型，只记录已经完成的专注统计。 */
#ifndef PRODUCT_UI_POMODORO_MODEL_H
#define PRODUCT_UI_POMODORO_MODEL_H
#include <stdint.h>
#include "framework/d_error.h"

/* 时长与循环间隔的业务范围；模型校验和页面控件共用，记录格式不变。 */
#define PRODUCT_UI_POMODORO_MIN_MINUTES 1U
#define PRODUCT_UI_POMODORO_FOCUS_MAX_MINUTES 120U
#define PRODUCT_UI_POMODORO_SHORT_MAX_MINUTES 30U
#define PRODUCT_UI_POMODORO_LONG_MAX_MINUTES 60U
#define PRODUCT_UI_POMODORO_MIN_INTERVAL 1U
#define PRODUCT_UI_POMODORO_MAX_INTERVAL 8U

#define PRODUCT_UI_POMODORO_DAYS 7U
#define PRODUCT_UI_POMODORO_RECORD_SIZE 128U

typedef enum {
    PRODUCT_UI_POMODORO_FOCUS = 0,
    PRODUCT_UI_POMODORO_SHORT_BREAK,
    PRODUCT_UI_POMODORO_LONG_BREAK,
} product_ui_pomodoro_phase_e;

typedef enum {
    PRODUCT_UI_POMODORO_WAITING = 0,
    PRODUCT_UI_POMODORO_RUNNING,
    PRODUCT_UI_POMODORO_PAUSED,
} product_ui_pomodoro_state_e;

typedef enum {
    PRODUCT_UI_POMODORO_START = 0,
    PRODUCT_UI_POMODORO_PAUSE,
    PRODUCT_UI_POMODORO_RESUME,
    PRODUCT_UI_POMODORO_SKIP,
    PRODUCT_UI_POMODORO_END,
} product_ui_pomodoro_command_e;

typedef struct {
    uint8_t focus_minutes;
    uint8_t short_minutes;
    uint8_t long_minutes;
    uint8_t interval;
    uint8_t auto_focus;
    uint8_t auto_break;
    uint8_t sound;
} product_ui_pomodoro_settings_t;

typedef struct {
    uint32_t date; /**< YYYYMMDD 日期编码，零表示空槽。 */
    uint32_t count;
    uint32_t minutes;
} product_ui_pomodoro_day_t;

typedef struct {
    product_ui_pomodoro_settings_t settings;
    product_ui_pomodoro_day_t days[PRODUCT_UI_POMODORO_DAYS];
    uint32_t undated_count;
    uint32_t undated_minutes;
} product_ui_pomodoro_saved_t;

typedef struct {
    product_ui_pomodoro_saved_t saved;
    product_ui_pomodoro_phase_e phase;
    product_ui_pomodoro_state_e state;
    product_ui_pomodoro_phase_e completed_phase;
    uint32_t generation;
    uint32_t revision;
    uint32_t saved_revision;
    uint32_t remaining_ms;
    uint32_t duration_ms;
    uint32_t sampled_ms;
    uint32_t event_id;
    uint32_t acknowledged_event;
    uint32_t event_ms;
    uint8_t completed_in_cycle;
    uint8_t cycle_interval;
    uint8_t cycle_active;
    uint8_t auto_next;
} product_ui_pomodoro_model_t;

/**
 * @brief 建立新启动的等待阶段，仅恢复设置与已完成统计，不恢复未完成计时。
 * @param[out] model 调用方持有的模型，NULL 时忽略。
 * @param[in] saved 可为空的已校验记录；模型仅检查设置范围，完整统计校验由解码入口负责。
 */
void product_ui_pomodoro_model_init(product_ui_pomodoro_model_t *model,
                                   const product_ui_pomodoro_saved_t *saved);
/**
 * @brief 检查专注／休息时长、长休息间隔及布尔开关范围。
 * @param[in] settings 借用的候选设置。
 * @return 所有设置合法返回非零，空指针或任一越界返回零。
 */
uint8_t product_ui_pomodoro_settings_valid(const product_ui_pomodoro_settings_t *settings);
/**
 * @brief 验证 YYYYMMDD 统计日期及公历闰年规则。
 * @param[in] date 待校验的年月日编码。
 * @return 1900 至 9999 年内的真实日期返回非零，零及其他无效日期返回零。
 */
uint8_t product_ui_pomodoro_date_valid(uint32_t date);
/**
 * @brief 按回绕毫秒差推进运行阶段，最多完成一个阶段并生成一次事件。
 * @param[in,out] model 须由调用方串行保护的模型，非运行态不操作。
 * @param[in] now_ms 当前单调毫秒计数。
 * @param[in] date 完成时归属的日期；零或不能放入日期槽时计入未归日统计。
 * @note 只在完整专注结束时增加完成统计；超出阶段的时间不连跳多阶段，新阶段从 now_ms 重新开始。
 */
void product_ui_pomodoro_model_advance(product_ui_pomodoro_model_t *model,
                                      uint32_t now_ms, uint32_t date);
/**
 * @brief 按当前操作代次执行开始、暂停、继续、跳过或结束。
 * @param[in,out] model 调用方串行保护的模型。
 * @param[in] command 有类型的计时操作。
 * @param[in] generation 按钮绑定时捕获的操作代次，必须匹配当前模型。
 * @param[in] now_ms 操作发生时的单调毫秒计数。
 * @return 成功返回 D_OK；参数无效返回 INVALID_ARG；代次或当前阶段不允许返回 BUSY。
 * @note 本函数不预先结算自上次采样以来的运行时间；服务入口先 advance/tick 再执行命令。跳过不计入完成专注。
 */
d_err_t product_ui_pomodoro_model_command(product_ui_pomodoro_model_t *model,
    product_ui_pomodoro_command_e command, uint32_t generation, uint32_t now_ms);
/**
 * @brief 校验并复制新设置，运行阶段保留冻结时长，等待阶段重新装载。
 * @param[in,out] model 调用方串行保护的模型。
 * @param[in] settings 本次复制的完整设置。
 * @return 成功返回 D_OK，空模型或设置无效返回 INVALID_ARG。
 * @note 推进保存代次；活跃周期的长休息间隔不因中途设置而重解释。
 */
d_err_t product_ui_pomodoro_model_settings(product_ui_pomodoro_model_t *model,
    const product_ui_pomodoro_settings_t *settings);
/**
 * @brief 清除日期及未归日统计，推进保存及显示代次，保留当前计时阶段。
 * @param[in,out] model 调用方串行保护的模型，NULL 时忽略。
 */
void product_ui_pomodoro_model_clear_stats(product_ui_pomodoro_model_t *model);
/**
 * @brief 把迟到的首次加载与本次会话数据合并一次，保持当前阶段。
 * @param[in,out] model 尚未合并旧记录的会话模型。
 * @param[in] loaded 借用的已验证旧保存数据。
 * @param[in] keep_settings 非零保留本次会话显式保存的设置。
 * @param[in] cleared_stats 非零表示本次已显式清空统计，不恢复旧统计。
 * @note 调用方负责只合并一次；当前运行阶段的冻结时长及自动策略不被旧记录替换。
 */
void product_ui_pomodoro_model_recover(product_ui_pomodoro_model_t *model,
    const product_ui_pomodoro_saved_t *loaded, uint8_t keep_settings, uint8_t cleared_stats);
/**
 * @brief 编码固定 128 字节小端保存记录并写入 CRC32。
 * @param[in] saved 调用方已验证的设置及已完成统计。
 * @param[in] sequence A/B 记录的环形序号。
 * @param[out] record 至少 RECORD_SIZE 字节，任一必要指针为空时不操作。
 * @note CRC 覆盖前 124 字节；不序列化在途阶段或本次剩余时长。
 */
void product_ui_pomodoro_encode(const product_ui_pomodoro_saved_t *saved,
    uint32_t sequence, uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE]);
/**
 * @brief 校验固定格式、保留字节、CRC、设置及统计一致性后提交输出。
 * @param[in] record 至少 RECORD_SIZE 字节的完整记录。
 * @param[out] saved 成功时接收完成数据，失败保留原值。
 * @param[out] sequence 成功时接收环形序号，失败保留原值。
 * @return 成功返回 D_OK；空必要指针返回 INVALID_ARG；任一记录检查失败返回 IO。
 */
d_err_t product_ui_pomodoro_decode(const uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE],
    product_ui_pomodoro_saved_t *saved, uint32_t *sequence);
/**
 * @brief 按半周期差比较相邻 A/B 记录的环形序号。
 * @param[in] left 候选新序号。
 * @param[in] right 已观察序号。
 * @return left 与 right 不同且差值在半周期内时为非零，否则为零。
 * @note 只用于相邻保存代次，不是任意历史序号的全序关系。
 */
uint8_t product_ui_pomodoro_sequence_after(uint32_t left, uint32_t right);
#endif
