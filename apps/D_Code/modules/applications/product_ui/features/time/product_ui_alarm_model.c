/**
 * @file product_ui_alarm_model.c
 * @brief 闹钟实例生命周期，IO 完成必须经显式状态转换确认。
 * @author D_Code
 * @date 2026-09-21
 */
#include "product_ui_alarm_model.h"
#include "product_ui_time_domain_internal.h"
#include <string.h>

/**
 * @brief 递增非零版本或代次并跳过回绕零值。
 * @param[in,out] value 调用链保证非空的字段。
 */
static void product_ui_alarm_increment(uint32_t *value)
{
    if (++*value == 0U) ++*value;
}

/**
 * @brief 推进模型内容代次，供保存及 UI 观察变化。
 * @param[in,out] model 有效模型。
 */
static void product_ui_alarm_changed(product_ui_alarm_model_t *model)
{
    product_ui_alarm_increment(&model->revision);
}

/**
 * @brief 仅在单次规则仍启用时关闭它并推进规则版本。
 * @param[in,out] record 有效记录。
 */
static void product_ui_alarm_disable_once(product_ui_alarm_record_t *record)
{
    if (record->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE && record->rule.enabled) {
        record->rule.enabled = 0U;
        product_ui_alarm_increment(&record->rule_revision);
    }
}

/**
 * @brief 建立新启动模型并复制已验证保存值，原在响／待音频／停止中实例改为错过提醒。
 */
void product_ui_alarm_model_init(product_ui_alarm_model_t *model, const product_ui_alarm_saved_t *saved)
{
    uint8_t index;
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    model->saved.next_id = 1U;
    model->saved.volume = PRODUCT_UI_ALARM_DEFAULT_VOLUME;
    model->revision = model->group_generation = 1U;
    if (saved == NULL) return;
    model->saved = *saved;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        product_ui_alarm_record_t *record = &model->saved.records[index];
        if (!record->rule.id) continue;
        if (record->state == PRODUCT_UI_ALARM_SNOOZED) model->restore_mask |= (uint16_t)(1U << index);
        if (record->state == PRODUCT_UI_ALARM_RINGING || record->state == PRODUCT_UI_ALARM_WAIT_AUDIO ||
            record->state == PRODUCT_UI_ALARM_STOPPING) {
            record->state = PRODUCT_UI_ALARM_MISSED;
            record->notice = 1U;
            record->stop_action = PRODUCT_UI_ALARM_IDLE;
            product_ui_alarm_disable_once(record);
            product_ui_alarm_changed(model);
        }
    }
}

/**
 * @brief 仅向完整空模型填入六条关闭状态的默认工作日闹钟。
 */
d_err_t product_ui_alarm_model_seed_defaults(product_ui_alarm_model_t *model)
{
    static const uint8_t hours[] = {7U, 8U, 12U, 14U, 18U, 22U};
    static const uint8_t minutes[] = {0U, 30U, 30U, 0U, 30U, 30U};
    static const uint8_t labels[] = {1U, 0U, 2U, 4U, 5U, 0U};
    uint8_t index;
    if (model == NULL) return D_ERR_INVALID_ARG;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++)
        if (model->saved.records[index].rule.id) return D_ERR_BUSY;
    for (index = 0U; index < sizeof(hours); index++) {
        product_ui_alarm_record_t *record = &model->saved.records[index];
        const char *label = product_ui_alarm_label(labels[index]);
        memset(record, 0, sizeof(*record));
        record->rule.id = (uint32_t)index + 1U;
        record->rule.schedule = PRODUCT_UI_ALARM_SCHEDULE_WEEKLY;
        record->rule.routine_template = PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST;
        record->rule.repeat_mask = 62U;
        record->rule.hour = hours[index]; record->rule.minute = minutes[index];
        record->label_kind = labels[index]; record->rule_revision = 1U;
        memcpy(record->rule.label, label, strlen(label));
    }
    model->saved.next_id = (uint32_t)sizeof(hours) + 1U;
    product_ui_alarm_changed(model);
    return D_OK;
}

/**
 * @brief 为到期或贪睡重响的实例进入 WAIT_AUDIO，建立截止及新实例代次。
 * @param[in,out] model 有效模型。
 * @param[in] index 有效记录槽。
 * @param[in] due 本次实例归属的原始执行 UTC。
 * @param[in] fresh 非零为新例次，零保留原例次及贪睡次数。
 */
static void product_ui_alarm_raise(product_ui_alarm_model_t *model, uint8_t index, int64_t due, uint8_t fresh)
{
    product_ui_alarm_record_t *record = &model->saved.records[index];
    if (fresh) {
        record->occurrence_utc = due;
        record->last_handled_utc = due;
        record->snooze_count = 0U;
        if (record->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE && record->rule.enabled) {
            /* 使到期前按压的旧行版本失效，但当前单次实例（含贪睡）
             * 结束之前仍保留规则启用。 */
            product_ui_alarm_increment(&record->rule_revision);
        }
        record->next_regular_utc = product_ui_alarm_next_occurrence(&record->rule, model->clock.utc_seconds);
    }
    record->state = PRODUCT_UI_ALARM_WAIT_AUDIO;
    record->stop_action = PRODUCT_UI_ALARM_IDLE;
    record->snooze_utc = 0;
    record->notice = 0U;
    product_ui_alarm_increment(&record->instance_generation);
    model->deadlines[index] = model->clock.monotonic_ms + PRODUCT_UI_ALARM_RING_MS;
    product_ui_alarm_changed(model);
}

/**
 * @brief 将超过补响窗口的例次记录为错过并计算下一正常执行点。
 * @param[in,out] model 有效模型。
 * @param[in] index 有效记录槽。
 * @param[in] due 错过的原执行 UTC。
 */
static void product_ui_alarm_missed(product_ui_alarm_model_t *model, uint8_t index, int64_t due)
{
    product_ui_alarm_record_t *record = &model->saved.records[index];
    record->occurrence_utc = due;
    record->last_handled_utc = due;
    record->state = PRODUCT_UI_ALARM_MISSED;
    record->notice = 1U;
    product_ui_alarm_disable_once(record);
    record->next_regular_utc = product_ui_alarm_next_occurrence(&record->rule, model->clock.utc_seconds);
    product_ui_alarm_changed(model);
}

/**
 * @brief 取得可信时钟后重建保存贪睡的单调截止，越界时标记错过或时间冲突。
 * @param[in,out] model 有效模型。
 * @param[in] index 带恢复标志的记录槽。
 */
static void product_ui_alarm_restore_snooze(product_ui_alarm_model_t *model, uint8_t index)
{
    product_ui_alarm_record_t *record = &model->saved.records[index];
    int64_t remaining;
    if (!model->clock.valid) return;
    remaining = record->snooze_utc - model->clock.utc_seconds;
    model->restore_mask &= (uint16_t)~(1U << index);
    if (remaining > 0 && remaining <= PRODUCT_UI_ALARM_SNOOZE_SECONDS) {
        model->deadlines[index] = model->clock.monotonic_ms + (uint64_t)remaining * 1000U;
    } else if (remaining <= 0 && remaining >= -(int64_t)PRODUCT_UI_ALARM_LATE_SECONDS) {
        product_ui_alarm_raise(model, index, record->occurrence_utc, 0U);
    } else {
        record->state = remaining > 0 ? PRODUCT_UI_ALARM_TIME_CONFLICT : PRODUCT_UI_ALARM_MISSED;
        if (record->state == PRODUCT_UI_ALARM_MISSED) product_ui_alarm_disable_once(record);
        record->notice = 1U;
        record->snooze_utc = 0;
        product_ui_alarm_changed(model);
    }
}

/**
 * @brief 推进单个实例的贪睡恢复／到期，或响铃超时后的 STOPPING 意图。
 * @param[in,out] model 有效模型。
 * @param[in] index 有效记录槽。
 */
static void product_ui_alarm_advance_instance(product_ui_alarm_model_t *model, uint8_t index)
{
    product_ui_alarm_record_t *record = &model->saved.records[index];
    if (model->restore_mask & (1U << index)) {
        product_ui_alarm_restore_snooze(model, index);
        return;
    }
    if (record->state == PRODUCT_UI_ALARM_SNOOZED && model->clock.monotonic_ms >= model->deadlines[index]) {
        if (model->clock.monotonic_ms - model->deadlines[index] > PRODUCT_UI_ALARM_LATE_SECONDS * 1000U) {
            record->state = PRODUCT_UI_ALARM_MISSED;
            product_ui_alarm_disable_once(record);
            record->notice = 1U;
            record->snooze_utc = 0;
            product_ui_alarm_changed(model);
        } else product_ui_alarm_raise(model, index, record->occurrence_utc, 0U);
    } else if ((record->state == PRODUCT_UI_ALARM_RINGING || record->state == PRODUCT_UI_ALARM_WAIT_AUDIO) &&
               model->clock.monotonic_ms >= model->deadlines[index]) {
        record->state = PRODUCT_UI_ALARM_STOPPING;
        record->stop_action = PRODUCT_UI_ALARM_MISSED;
        product_ui_alarm_changed(model);
    }
}

/**
 * @brief 按调度代次区分手动校正与网络修正，保持贪睡单调期限并重排正常计划。
 * @param[in,out] model 已接纳新时钟的模型。
 * @param[in] previous 接纳之前的时钟快照。
 * @note 旧时钟中的未来缓存不能隐藏新时钟下更早的可执行例次，已经处理的时刻仍受抑制。
 */
static void product_ui_alarm_correct_time(product_ui_alarm_model_t *model,
    const product_ui_clock_snapshot_t *previous)
{
    uint8_t index;
    uint8_t manual = model->has_clock && model->clock.schedule_epoch != previous->schedule_epoch;
    if (!model->clock.valid) return;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        product_ui_alarm_record_t *record = &model->saved.records[index];
        if (!record->rule.id) continue;
        if (record->state == PRODUCT_UI_ALARM_SNOOZED && !(model->restore_mask & (1U << index))) {
            uint64_t remaining = model->deadlines[index] > model->clock.monotonic_ms ?
                model->deadlines[index] - model->clock.monotonic_ms : 0U;
            int64_t deadline = model->clock.utc_seconds + (int64_t)((remaining + 999U) / 1000U);
            if (deadline > PRODUCT_UI_CLOCK_END_UTC) {
                record->state = PRODUCT_UI_ALARM_TIME_CONFLICT;
                record->notice = 1U;
                record->snooze_utc = 0;
            } else record->snooze_utc = deadline;
            product_ui_alarm_changed(model);
        }
        if (manual) {
            product_ui_date_t previous_date = record->rule.date;
            uint8_t previous_enabled = record->rule.enabled;
            record->last_handled_utc = 0;
            if (record->rule.enabled && record->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE) {
                product_ui_datetime_t next;
                if (product_ui_time_utc_to_beijing(model->clock.utc_seconds, &next) == PRODUCT_UI_TIME_OK) {
                    record->rule.date = next.date;
                    if (!product_ui_alarm_next_occurrence(&record->rule, model->clock.utc_seconds))
                        (void)product_ui_time_add_one_day(&record->rule.date);
                }
            }
            record->next_regular_utc = product_ui_alarm_next_occurrence(&record->rule, model->clock.utc_seconds);
            if (record->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE &&
                record->rule.date.year > PRODUCT_UI_CLOCK_MAX_YEAR) {
                record->rule.date.year = PRODUCT_UI_CLOCK_MAX_YEAR;
                record->rule.date.month = 12U;
                record->rule.date.day = 31U;
                record->rule.enabled = 0U;
            }
            if (previous_date.year != record->rule.date.year || previous_date.month != record->rule.date.month ||
                previous_date.day != record->rule.date.day || previous_enabled != record->rule.enabled)
                product_ui_alarm_increment(&record->rule_revision);
            product_ui_alarm_changed(model);
        } else if (record->rule.enabled && record->rule.schedule != PRODUCT_UI_ALARM_SCHEDULE_ONCE &&
                   record->next_regular_utc > model->clock.utc_seconds &&
                   record->state != PRODUCT_UI_ALARM_SNOOZED && record->state != PRODUCT_UI_ALARM_RINGING &&
                   record->state != PRODUCT_UI_ALARM_WAIT_AUDIO && record->state != PRODUCT_UI_ALARM_STOPPING) {
            int64_t after = model->clock.utc_seconds - 1;
            int64_t next;
            /* 旧时钟的未来缓存不能遮住更早的合法例次；保留已处理时刻抑制，
             * 不为错过创建时刻后才建立的规则补造过去实例；
             * 已有逾期条目仍按下方一百二十秒补响窗口结算。 */
            if (after < record->last_handled_utc) after = record->last_handled_utc;
            next = product_ui_alarm_next_occurrence(&record->rule, after);
            if (next != record->next_regular_utc) {
                record->next_regular_utc = next;
                product_ui_alarm_changed(model);
            }
        }
    }
}

/**
 * @brief 接纳非过期时钟，处理校正、贪睡、到期／错过及单调响铃期限。
 */
void product_ui_alarm_model_advance(product_ui_alarm_model_t *model, const product_ui_clock_snapshot_t *clock)
{
    product_ui_clock_snapshot_t previous;
    uint16_t before;
    uint8_t index;
    if (model == NULL || clock == NULL) return;
    if ((clock->valid && (clock->utc_seconds < PRODUCT_UI_CLOCK_MIN_UTC || clock->utc_seconds > PRODUCT_UI_CLOCK_END_UTC)) ||
        (model->has_clock && clock->monotonic_ms < model->clock.monotonic_ms)) return;
    if (model->has_clock && clock->revision != model->clock.revision &&
        (uint32_t)(clock->revision - model->clock.revision) >= 0x80000000U) return;
    before = product_ui_alarm_model_ring_mask(model);
    previous = model->clock;
    model->clock = *clock;
    if (!model->has_clock || previous.revision != clock->revision)
        product_ui_alarm_correct_time(model, &previous);
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        product_ui_alarm_record_t *record = &model->saved.records[index];
        int64_t due;
        if (!record->rule.id) continue;
        product_ui_alarm_advance_instance(model, index);
        if (!clock->valid || !record->rule.enabled || record->state == PRODUCT_UI_ALARM_SNOOZED ||
            record->state == PRODUCT_UI_ALARM_RINGING || record->state == PRODUCT_UI_ALARM_WAIT_AUDIO ||
            record->state == PRODUCT_UI_ALARM_STOPPING) continue;
        if (!record->next_regular_utc) {
            int64_t after = clock->utc_seconds - PRODUCT_UI_ALARM_LATE_SECONDS - 1;
            if (after < PRODUCT_UI_CLOCK_MIN_UTC - 1) after = PRODUCT_UI_CLOCK_MIN_UTC - 1;
            if (after < record->last_handled_utc) after = record->last_handled_utc;
            record->next_regular_utc = product_ui_alarm_next_occurrence(&record->rule, after);
            if (record->next_regular_utc) product_ui_alarm_changed(model);
        }
        due = record->next_regular_utc;
        if (!due || due > clock->utc_seconds) continue;
        if (due <= record->last_handled_utc) {
            record->next_regular_utc = product_ui_alarm_next_occurrence(&record->rule, record->last_handled_utc);
            product_ui_alarm_changed(model);
            continue;
        }
        if (record->rule.schedule != PRODUCT_UI_ALARM_SCHEDULE_ONCE &&
            clock->utc_seconds - due > PRODUCT_UI_ALARM_LATE_SECONDS) {
            int64_t after = clock->utc_seconds - PRODUCT_UI_ALARM_LATE_SECONDS - 1;
            int64_t recent;
            /* 过期缓存不能遮住今天仍在窗口内的例次；窗口短于一天，最多一个重复例次补响。 */
            if (after < record->last_handled_utc) after = record->last_handled_utc;
            recent = product_ui_alarm_next_occurrence(&record->rule, after);
            if (recent && recent <= clock->utc_seconds) due = recent;
        }
        if (clock->utc_seconds - due <= PRODUCT_UI_ALARM_LATE_SECONDS)
            product_ui_alarm_raise(model, index, due, 1U);
        else product_ui_alarm_missed(model, index, due);
    }
    model->has_clock = 1U;
    if (before != product_ui_alarm_model_ring_mask(model)) product_ui_alarm_increment(&model->group_generation);
}

/**
 * @brief 只为当前响铃组代次把 WAIT_AUDIO 转为 RINGING，并重置响铃期限。
 */
void product_ui_alarm_model_audio_started(product_ui_alarm_model_t *model, uint32_t group_generation)
{
    uint8_t index;
    if (model == NULL || group_generation != model->group_generation) return;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        product_ui_alarm_record_t *record = &model->saved.records[index];
        if (record->state != PRODUCT_UI_ALARM_WAIT_AUDIO) continue;
        record->state = PRODUCT_UI_ALARM_RINGING;
        model->deadlines[index] = model->clock.monotonic_ms + PRODUCT_UI_ALARM_RING_MS;
        product_ui_alarm_changed(model);
    }
}

/**
 * @brief 在真实音频停止确认后提交所有 STOPPING 实例的待完成动作。
 */
void product_ui_alarm_model_audio_stopped(product_ui_alarm_model_t *model)
{
    uint8_t index;
    if (model == NULL) return;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        product_ui_alarm_record_t *record = &model->saved.records[index];
        if (record->state != PRODUCT_UI_ALARM_STOPPING) continue;
        record->state = record->stop_action;
        record->stop_action = PRODUCT_UI_ALARM_IDLE;
        record->notice = record->state == PRODUCT_UI_ALARM_MISSED;
        if (record->state == PRODUCT_UI_ALARM_DONE || record->state == PRODUCT_UI_ALARM_MISSED)
            product_ui_alarm_disable_once(record);
        if (record->state == PRODUCT_UI_ALARM_SNOOZED) {
            if (!model->clock.valid || model->clock.utc_seconds > PRODUCT_UI_CLOCK_END_UTC - PRODUCT_UI_ALARM_SNOOZE_SECONDS) {
                record->state = PRODUCT_UI_ALARM_TIME_CONFLICT;
                record->notice = 1U;
            } else {
                record->snooze_count++;
                model->deadlines[index] = model->clock.monotonic_ms + PRODUCT_UI_ALARM_SNOOZE_MS;
                record->snooze_utc = model->clock.utc_seconds + PRODUCT_UI_ALARM_SNOOZE_SECONDS;
            }
        }
        product_ui_alarm_changed(model);
    }
}

/**
 * @brief 在候选记录验证标签、单次日期及双周相位，不先改活动记录。
 * @param[in] model 提供已接纳时钟及原规则的模型。
 * @param[in] command 借用编辑／启用命令。
 * @param[out] candidate 候选完整记录；失败不得提交。
 * @return 候选合法为 D_OK，时钟不足为 NOT_READY，字段或可执行时刻无效为 INVALID_ARG。
 * @note 新建／改动单次与建立双周相位需可信墙钟；无时钟仍可关闭既有规则。
 */
static d_err_t product_ui_alarm_prepare_rule(product_ui_alarm_model_t *model,
    const product_ui_alarm_command_t *command, product_ui_alarm_record_t *candidate)
{
    memset(candidate, 0, sizeof(*candidate));
    candidate->rule = command->rule;
    candidate->rule.id = command->rule.id ? command->rule.id : 1U;
    candidate->rule_revision = 1U;
    candidate->label_kind = command->label_kind;
    /* 可不自动解析日期，但新建／编辑单次规则始终要满足可信时间资格；
     * 时钟未就绪仍允许关闭既有规则。 */
    if (candidate->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE && !model->clock.valid &&
        (command->action != PRODUCT_UI_ALARM_ENABLE || command->enabled)) return D_ERR_NOT_READY;
    if (candidate->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT && !model->clock.valid) {
        int index = product_ui_alarm_model_find(model, command->rule.id);
        const product_ui_alarm_t *previous = index >= 0 ? &model->saved.records[index].rule : NULL;
        /* 解码及编辑既有周期不需墙钟；建立新的周期相位必须有可信时间，
         * 此约束也覆盖页面之外的调用方。 */
        if (previous == NULL || previous->schedule != PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT ||
            previous->date.year != candidate->rule.date.year ||
            previous->date.month != candidate->rule.date.month ||
            previous->date.day != candidate->rule.date.day) return D_ERR_NOT_READY;
    }
    if (command->label_kind < 6U) {
        const char *label = product_ui_alarm_label(command->label_kind);
        memset(candidate->rule.label, 0, sizeof(candidate->rule.label));
        memcpy(candidate->rule.label, label, strlen(label));
    }
    if (candidate->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE && command->resolve_once) {
        product_ui_datetime_t next;
        if (!model->clock.valid) return D_ERR_NOT_READY;
        if (product_ui_time_utc_to_beijing(model->clock.utc_seconds, &next) != PRODUCT_UI_TIME_OK) return D_ERR_INVALID_ARG;
        candidate->rule.date = next.date;
        candidate->rule.enabled = 1U;
        if (!product_ui_alarm_next_occurrence(&candidate->rule, model->clock.utc_seconds) &&
            product_ui_time_add_one_day(&candidate->rule.date) != PRODUCT_UI_TIME_OK) return D_ERR_INVALID_ARG;
        candidate->rule.enabled = command->rule.enabled;
    }
    if (!product_ui_alarm_record_valid(candidate)) return D_ERR_INVALID_ARG;
    if (model->clock.valid) {
        candidate->next_regular_utc = product_ui_alarm_next_occurrence(&candidate->rule, model->clock.utc_seconds);
        if (candidate->rule.enabled && !candidate->next_regular_utc) return D_ERR_INVALID_ARG;
    }
    return D_OK;
}

/**
 * @brief 验证规则版本和候选，成功后分配或保留编号并替换一个记录槽。
 * @param[in,out] model 活动模型。
 * @param[in] command 本次借用的规则命令。
 * @param[out] affected_id 可为空，成功接收规则编号。
 * @return 成功 D_OK，缺记录为 NOT_FOUND，版本冲突 BUSY，容量不足 FULL，其余为候选错误。
 * @note 替换重置旧实例截止及恢复标志；新编号搜索失败可能已推进 next_id。
 */
static d_err_t product_ui_alarm_put(product_ui_alarm_model_t *model,
    const product_ui_alarm_command_t *command, uint32_t *affected_id)
{
    product_ui_alarm_record_t candidate;
    int index = product_ui_alarm_model_find(model, command->rule.id);
    uint8_t attempt;
    d_err_t result;
    if (command->rule.id && index < 0) return D_ERR_NOT_FOUND;
    if (index >= 0 && command->expected_revision != model->saved.records[index].rule_revision) return D_ERR_BUSY;
    result = product_ui_alarm_prepare_rule(model, command, &candidate);
    if (result != D_OK) return result;
    if (index < 0) {
        for (index = 0; index < (int)PRODUCT_UI_ALARM_COUNT; index++)
            if (!model->saved.records[index].rule.id) break;
        if (index == (int)PRODUCT_UI_ALARM_COUNT) return D_ERR_FULL;
        for (attempt = 0U; attempt <= PRODUCT_UI_ALARM_COUNT; attempt++) {
            candidate.rule.id = model->saved.next_id;
            product_ui_alarm_increment(&model->saved.next_id);
            if (candidate.rule.id && product_ui_alarm_model_find(model, candidate.rule.id) < 0) break;
        }
        if (attempt > PRODUCT_UI_ALARM_COUNT) return D_ERR_FULL;
    } else {
        candidate.rule_revision = model->saved.records[index].rule_revision;
        product_ui_alarm_increment(&candidate.rule_revision);
        candidate.instance_generation = model->saved.records[index].instance_generation;
        product_ui_alarm_increment(&candidate.instance_generation);
    }
    model->saved.records[index] = candidate;
    model->deadlines[index] = 0U;
    model->restore_mask &= (uint16_t)~(1U << index);
    if (affected_id != NULL) *affected_id = candidate.rule.id;
    product_ui_alarm_changed(model);
    return D_OK;
}

/**
 * @brief 验证当前响铃组代次，再为整组预约停止或贪睡动作。
 * @param[in,out] model 活动模型。
 * @param[in] command 匹配组代次的停止／贪睡命令。
 * @return 成功 D_OK；无组或旧代次 BUSY，贪睡时钟不足 NOT_READY，次数耗尽 INVALID_ARG。
 * @note 只写 STOPPING 与 stop_action，真实结束须等待音频完成确认。
 */
static d_err_t product_ui_alarm_group_command(product_ui_alarm_model_t *model,
    const product_ui_alarm_command_t *command)
{
    uint16_t mask = product_ui_alarm_model_ring_mask(model);
    uint8_t index;
    if (!mask || command->group_generation != model->group_generation) return D_ERR_BUSY;
    if (command->action == PRODUCT_UI_ALARM_SNOOZE_GROUP) {
        if (!model->clock.valid || model->clock.utc_seconds > PRODUCT_UI_CLOCK_END_UTC - PRODUCT_UI_ALARM_SNOOZE_SECONDS) return D_ERR_NOT_READY;
        for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++)
            if ((mask & (1U << index)) && model->saved.records[index].snooze_count >= PRODUCT_UI_ALARM_SNOOZE_LIMIT)
                return D_ERR_INVALID_ARG;
    }
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        product_ui_alarm_record_t *record = &model->saved.records[index];
        if (!(mask & (1U << index))) continue;
        record->state = PRODUCT_UI_ALARM_STOPPING;
        record->stop_action = command->action == PRODUCT_UI_ALARM_SNOOZE_GROUP ? PRODUCT_UI_ALARM_SNOOZED : PRODUCT_UI_ALARM_DONE;
    }
    product_ui_alarm_changed(model);
    return D_OK;
}

/**
 * @brief 按规则版本或响铃组代次执行有界编辑、删除、启用、组停止／贪睡等变更。
 */
d_err_t product_ui_alarm_model_submit(product_ui_alarm_model_t *model,
    const product_ui_alarm_command_t *command, uint32_t *affected_id)
{
    uint16_t before;
    int index;
    d_err_t result = D_OK;
    if (affected_id != NULL) *affected_id = 0U;
    if (model == NULL || command == NULL) return D_ERR_INVALID_ARG;
    before = product_ui_alarm_model_ring_mask(model);
    index = product_ui_alarm_model_find(model, command->id);
    if (command->action == PRODUCT_UI_ALARM_EDIT) result = product_ui_alarm_put(model, command, affected_id);
    else if (command->action == PRODUCT_UI_ALARM_STOP_GROUP || command->action == PRODUCT_UI_ALARM_SNOOZE_GROUP)
        result = product_ui_alarm_group_command(model, command);
    else if (command->action == PRODUCT_UI_ALARM_VOLUME) {
        if (command->volume < 1U || command->volume > 100U) return D_ERR_INVALID_ARG;
        if (model->saved.volume != command->volume) { model->saved.volume = command->volume; product_ui_alarm_changed(model); }
    } else {
        product_ui_alarm_record_t *record;
        if (index < 0) return D_ERR_NOT_FOUND;
        record = &model->saved.records[index];
        if (command->expected_revision != record->rule_revision) return D_ERR_BUSY;
        if (command->action == PRODUCT_UI_ALARM_DELETE) {
            memset(record, 0, sizeof(*record));
            model->restore_mask &= (uint16_t)~(1U << index);
            model->deadlines[index] = 0U;
            product_ui_alarm_changed(model);
        } else if (command->action == PRODUCT_UI_ALARM_ENABLE) {
            product_ui_alarm_command_t edit = *command;
            if (command->enabled > 1U) return D_ERR_INVALID_ARG;
            edit.rule = record->rule; edit.rule.enabled = command->enabled;
            edit.label_kind = record->label_kind;
            edit.resolve_once = command->enabled && record->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE;
            result = product_ui_alarm_put(model, &edit, affected_id);
        } else if (command->action == PRODUCT_UI_ALARM_CLEAR_NOTICE) {
            record->notice = 0U;
            product_ui_alarm_changed(model);
        } else result = D_ERR_INVALID_ARG;
        if (result == D_OK && affected_id != NULL) *affected_id = command->id;
    }
    if (before != product_ui_alarm_model_ring_mask(model)) product_ui_alarm_increment(&model->group_generation);
    return result;
}
