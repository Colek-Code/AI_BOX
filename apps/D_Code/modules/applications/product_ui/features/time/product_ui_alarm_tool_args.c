/** @file product_ui_alarm_tool_args.c @brief 只解释显式标量日期规则，不从自由文本猜测闹钟意图。 */
#include "config/app_config.h"
#if D_FEATURE_AI_ALARM
#include "product_ui_alarm_tool.h"
#include "product_ui_time_domain_internal.h"
#include <string.h>

/**
 * @brief 按键借用工具参数的已有文字值。
 * @param[in] call 借用的工具调用。
 * @param[in] key 参数键。
 * @return 参数存在返回内部值指针，不存在为 NULL。
 */
static const char *text(const qwen_tool_call_t *call, const char *key)
{
    const qwen_tool_arg_t *arg = qwen_tool_argument(call, key);
    return arg ? arg->value : NULL;
}

/**
 * @brief 读取参数中的非负十进制 int32_t，拒绝布尔种类、空值及溢出。
 */
int product_ui_alarm_tool_integer(const qwen_tool_call_t *call, const char *key, int32_t *value)
{
    const qwen_tool_arg_t *arg = qwen_tool_argument(call, key);
    const char *p;
    uint32_t number = 0U;
    if (!arg || arg->kind == QWEN_TOOL_BOOLEAN || !arg->value[0]) return 0;
    for (p = arg->value; *p; p++) {
        if (*p < '0' || *p > '9' || number > (2147483647U - (uint32_t)(*p - '0')) / 10U) return 0;
        number = number * 10U + (uint32_t)(*p - '0');
    }
    *value = (int32_t)number; return 1;
}

/**
 * @brief 读取 true／false 或 1／0 文本参数。
 */
int product_ui_alarm_tool_boolean(const qwen_tool_call_t *call, const char *key, uint8_t *value)
{
    const char *p = text(call, key);
    if (!p) return 0;
    if (!strcmp(p, "true") || !strcmp(p, "1")) { *value = 1U; return 1; }
    if (!strcmp(p, "false") || !strcmp(p, "0")) { *value = 0U; return 1; }
    return 0;
}

/**
 * @brief 解析逗号分隔的一至七星期编号，拒绝重复及空项。
 * @param[in] p NUL 结尾星期列表，一为周一、七为周日。
 * @param[out] mask 非空输出，成功接收周日位为零的七位掩码。
 * @return 成功写掩码并返回 1，格式不符返回 0。
 */
static int weekdays(const char *p, uint8_t *mask)
{
    uint8_t result = 0U;
    if (!p || !*p) return 0;
    while (*p) {
        uint8_t bit;
        if (*p < '1' || *p > '7') return 0;
        bit = (uint8_t)(1U << ((*p++ - '0') % 7));
        if (result & bit) return 0;
        result |= bit;
        if (!*p) break;
        if (*p++ != ',' || !*p) return 0;
    }
    *mask = result; return 1;
}

/**
 * @brief 只解析固定 YYYY-MM-DD 的数字形状，不在此判断真实日历合法性。
 * @param[in] p 固定十字节日期文字。
 * @param[out] date 调用链保证非空，接收年月日字段，后续仍须校验。
 * @return 形状合法并填写返回 1，否则为 0。
 */
static int absolute_date(const char *p, product_ui_date_t *date)
{
    unsigned i;
    if (!p || strlen(p) != 10U || p[4] != '-' || p[7] != '-') return 0;
    for (i = 0U; i < 10U; i++) if (i != 4U && i != 7U && (p[i] < '0' || p[i] > '9')) return 0;
    date->year = (uint16_t)((p[0]-'0')*1000+(p[1]-'0')*100+(p[2]-'0')*10+p[3]-'0');
    date->month = (uint8_t)((p[5]-'0')*10+p[6]-'0');
    date->day = (uint8_t)((p[8]-'0')*10+p[9]-'0'); return 1;
}

/**
 * @brief 按 next、delay、absolute 或 relative 显式模式解析单次规则日期。
 * @param[in] call 借用工具调用。
 * @param[in] clock 调用方已确认有效的时钟快照。
 * @param[in] creating 非零表示新建或从其他计划切成单次。
 * @param[in,out] rule 已具备时分的候选规则，成功更新日期，delay 也更新时分。
 * @return 成功 D_OK，模式冲突、参数或转换错误 INVALID_ARG。
 * @note delay 向上取整到整分钟；无模式的既有单次更新保留原绝对日期，新建默认 next。
 */
static d_err_t once_date(const qwen_tool_call_t *call, const product_ui_clock_snapshot_t *clock,
    uint8_t creating, product_ui_alarm_t *rule)
{
    const char *mode = text(call, "date_mode");
    product_ui_datetime_t date;
    int32_t value;
    int64_t stamp;
    if (!mode) {
        if (text(call, "date") || text(call, "day_offset") || text(call, "delay_minutes")) return D_ERR_INVALID_ARG;
        if (!creating) return D_OK; /* 更新必须保留既有绝对日期。 */
        mode = "next";
    }
    if (!strcmp(mode, "delay")) {
        if (text(call, "hour") || text(call, "minute") || text(call, "date") || text(call, "day_offset") ||
            !product_ui_alarm_tool_integer(call, "delay_minutes", &value) || value < 1 || value > 525600) return D_ERR_INVALID_ARG;
        stamp = ((clock->utc_seconds + (int64_t)value * 60 + 59) / 60) * 60;
        if (product_ui_time_utc_to_beijing(stamp, &date) != PRODUCT_UI_TIME_OK) return D_ERR_INVALID_ARG;
        rule->date = date.date; rule->hour = date.time.hour; rule->minute = date.time.minute; return D_OK;
    }
    if (text(call, "delay_minutes")) return D_ERR_INVALID_ARG;
    if (!strcmp(mode, "absolute")) {
        if (text(call, "day_offset") || !absolute_date(text(call, "date"), &rule->date)) return D_ERR_INVALID_ARG;
    } else {
        if (text(call, "date") || product_ui_time_utc_to_beijing(clock->utc_seconds, &date) != PRODUCT_UI_TIME_OK)
            return D_ERR_INVALID_ARG;
        if (!strcmp(mode, "relative")) {
            if (!product_ui_alarm_tool_integer(call, "day_offset", &value) || value > 3650) return D_ERR_INVALID_ARG;
            if (product_ui_time_utc_to_beijing(clock->utc_seconds + (int64_t)value * 86400, &date) != PRODUCT_UI_TIME_OK)
                return D_ERR_INVALID_ARG;
        } else if (strcmp(mode, "next") || text(call, "day_offset")) return D_ERR_INVALID_ARG;
        rule->date = date.date;
        date.time = (product_ui_time_of_day_t){rule->hour, rule->minute, 0U};
        if (product_ui_time_beijing_to_utc(&date, &stamp) != PRODUCT_UI_TIME_OK) return D_ERR_INVALID_ARG;
        if (!strcmp(mode, "next") && stamp <= clock->utc_seconds && product_ui_time_add_one_day(&rule->date) != PRODUCT_UI_TIME_OK)
            return D_ERR_INVALID_ARG;
    }
    return D_OK;
}
/**
 * @brief 按显式时间、日期模式及周重复参数生成受限闹钟编辑命令。
 */
d_err_t product_ui_alarm_tool_rule(const qwen_tool_call_t *call, const product_ui_clock_snapshot_t *clock,
    const product_ui_alarm_record_t *previous, product_ui_alarm_command_t *command)
{
    const char *schedule = text(call, "schedule"), *label = text(call, "label"), *mode = text(call, "date_mode");
    product_ui_alarm_record_t candidate = {0};
    int32_t value;
    uint8_t creating = previous == NULL;
    if (!clock->valid) return D_ERR_NOT_READY;
    if (previous && previous->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT) return D_ERR_INVALID_ARG;
    memset(command, 0, sizeof(*command));
    command->action = PRODUCT_UI_ALARM_EDIT;
    command->label_kind = previous ? previous->label_kind : 255U;
    if (previous) { command->rule = previous->rule; command->expected_revision = previous->rule_revision; }
    else { command->rule.enabled = 1U; strcpy(command->rule.label, "闹钟"); }
    if (schedule) {
        if (!strcmp(schedule, "once")) command->rule.schedule = PRODUCT_UI_ALARM_SCHEDULE_ONCE;
        else if (!strcmp(schedule, "weekly")) command->rule.schedule = PRODUCT_UI_ALARM_SCHEDULE_WEEKLY;
        else return D_ERR_INVALID_ARG;
        command->rule.routine_template = PRODUCT_UI_ALARM_ROUTINE_BASIC;
        command->rule.alternate_mask = 0U;
    }
    if (creating && !schedule) return D_ERR_INVALID_ARG;
    if (text(call, "hour")) {
        if (!product_ui_alarm_tool_integer(call, "hour", &value) || value > 23) return D_ERR_INVALID_ARG;
        command->rule.hour = (uint8_t)value;
    } else if (creating && (!mode || strcmp(mode, "delay"))) return D_ERR_INVALID_ARG;
    if (text(call, "minute")) {
        if (!product_ui_alarm_tool_integer(call, "minute", &value) || value > 59) return D_ERR_INVALID_ARG;
        command->rule.minute = (uint8_t)value;
    } else if (creating && (!mode || strcmp(mode, "delay"))) return D_ERR_INVALID_ARG;
    if (label) {
        if (!*label || strlen(label) >= sizeof(command->rule.label)) return D_ERR_INVALID_ARG;
        memset(command->rule.label, 0, sizeof(command->rule.label)); strcpy(command->rule.label, label);
        command->label_kind = 255U;
    }
    if (command->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_WEEKLY) {
        if (mode || text(call, "date") || text(call, "day_offset") || text(call, "delay_minutes")) return D_ERR_INVALID_ARG;
        if (text(call, "weekdays")) {
            if (!weekdays(text(call, "weekdays"), &command->rule.repeat_mask)) return D_ERR_INVALID_ARG;
        } else if (creating || previous->rule.schedule != PRODUCT_UI_ALARM_SCHEDULE_WEEKLY) return D_ERR_INVALID_ARG;
    } else {
        d_err_t error;
        if (text(call, "weekdays")) return D_ERR_INVALID_ARG;
        command->rule.repeat_mask = 0U;
        error = once_date(call, clock, creating || (previous && previous->rule.schedule != command->rule.schedule), &command->rule);
        if (error != D_OK) return error;
    }
    candidate.rule = command->rule; candidate.rule.id = 1U; candidate.rule_revision = 1U;
    candidate.label_kind = command->label_kind;
    if (!product_ui_alarm_record_valid(&candidate) ||
        (candidate.rule.enabled && !product_ui_alarm_next_occurrence(&candidate.rule, clock->utc_seconds))) return D_ERR_INVALID_ARG;
    return D_OK;
}
#endif
