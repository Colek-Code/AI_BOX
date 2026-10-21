/**
 * @file product_ui_alarm_rules.c
 * @brief Rule validation and future occurrence calculation for bounded civil dates.
 * @author D_Code
 * @date 2026-09-21
 */
#include "product_ui_alarm_model.h"
#include "product_ui_time_domain_internal.h"
#include <string.h>

const char *product_ui_alarm_label(uint8_t kind)
{
    static const char *const labels[] = {"闹钟", "起床", "午休", "吃药", "学习", "运动"};
    return kind < 6U ? labels[kind] : labels[0];
}

int product_ui_alarm_model_find(const product_ui_alarm_model_t *model, uint32_t id)
{
    uint8_t index;
    if (model == NULL || id == 0U) return -1;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++)
        if (model->saved.records[index].rule.id == id) return (int)index;
    return -1;
}

static uint8_t product_ui_alarm_stamp_valid(int64_t stamp)
{
    return stamp == 0 || (stamp >= PRODUCT_UI_CLOCK_MIN_UTC && stamp <= PRODUCT_UI_CLOCK_END_UTC);
}

static uint8_t product_ui_alarm_label_valid(const char *label)
{
    uint8_t index = 0U;
    while (index < PRODUCT_UI_TIME_DOMAIN_ALARM_LABEL_SIZE) {
        uint8_t first = (uint8_t)label[index++];
        uint8_t remaining;
        uint8_t second;
        if (first == 0U) return 1U;
        if (first < 0x20U || first == 0x7fU) return 0U;
        if (first < 0x80U) continue;
        remaining = first >= 0xc2U && first <= 0xdfU ? 1U :
            first >= 0xe0U && first <= 0xefU ? 2U : first >= 0xf0U && first <= 0xf4U ? 3U : 0U;
        if (!remaining || (uint16_t)index + remaining >= PRODUCT_UI_TIME_DOMAIN_ALARM_LABEL_SIZE) return 0U;
        second = (uint8_t)label[index];
        if ((first == 0xe0U && second < 0xa0U) || (first == 0xedU && second >= 0xa0U) ||
            (first == 0xf0U && second < 0x90U) || (first == 0xf4U && second >= 0x90U)) return 0U;
        while (remaining--) if (((uint8_t)label[index++] & 0xc0U) != 0x80U) return 0U;
    }
    return 0U;
}

uint8_t product_ui_alarm_record_valid(const product_ui_alarm_record_t *record)
{
    if (record == NULL) return 0U;
    if (record->rule.id == 0U) return 1U;
    if (product_ui_time_domain_validate_alarm(&record->rule) != PRODUCT_UI_TIME_OK ||
        !product_ui_alarm_label_valid(record->rule.label) ||
        (record->label_kind >= 6U && record->label_kind != 255U) || !record->rule_revision ||
        record->state > PRODUCT_UI_ALARM_TIME_CONFLICT || record->notice > 1U ||
        record->snooze_count > PRODUCT_UI_ALARM_SNOOZE_LIMIT ||
        (record->stop_action != PRODUCT_UI_ALARM_DONE && record->stop_action != PRODUCT_UI_ALARM_MISSED &&
         record->stop_action != PRODUCT_UI_ALARM_SNOOZED && record->stop_action != PRODUCT_UI_ALARM_IDLE)) return 0U;
    if (record->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE &&
        (record->rule.date.year < PRODUCT_UI_CLOCK_MIN_YEAR || record->rule.date.year > PRODUCT_UI_CLOCK_MAX_YEAR))
        return 0U;
    if (!product_ui_alarm_stamp_valid(record->occurrence_utc) || !product_ui_alarm_stamp_valid(record->last_handled_utc) ||
        !product_ui_alarm_stamp_valid(record->snooze_utc) || !product_ui_alarm_stamp_valid(record->next_regular_utc)) return 0U;
    if (record->state != PRODUCT_UI_ALARM_IDLE && record->occurrence_utc == 0) return 0U;
    if (record->state == PRODUCT_UI_ALARM_SNOOZED && (!record->snooze_count || !record->snooze_utc)) return 0U;
    return 1U;
}

static d_err_t product_ui_alarm_monday(const product_ui_date_t *date, int64_t *monday)
{
    product_ui_datetime_t day;
    int64_t stamp;
    if (date == NULL || monday == NULL) return D_ERR_INVALID_ARG;
    day = (product_ui_datetime_t){*date, {0U, 0U, 0U}};
    if (product_ui_time_beijing_to_utc(&day, &stamp) != PRODUCT_UI_TIME_OK) return D_ERR_INVALID_ARG;
    *monday = stamp - (int64_t)((product_ui_time_weekday(date) + 6U) % 7U) * 86400;
    return D_OK;
}

d_err_t product_ui_alarm_week_index(const product_ui_alarm_t *rule,
    const product_ui_date_t *date, uint8_t *week)
{
    int64_t anchor, monday, difference;
    if (rule == NULL || week == NULL || rule->schedule != PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT ||
        product_ui_time_domain_validate_alarm(rule) != PRODUCT_UI_TIME_OK ||
        product_ui_alarm_monday(&rule->date, &anchor) != D_OK ||
        product_ui_alarm_monday(date, &monday) != D_OK) return D_ERR_INVALID_ARG;
    /* Monday-to-Monday is an exact signed number of weeks, also before the anchor. */
    difference = (monday - anchor) / (7 * 86400);
    *week = (uint8_t)(difference % 2 != 0);
    return D_OK;
}

d_err_t product_ui_alarm_anchor_for_week(const product_ui_date_t *date,
    uint8_t week, product_ui_date_t *anchor)
{
    int64_t monday;
    product_ui_datetime_t day;
    if (date == NULL || anchor == NULL || week > 1U ||
        date->year < PRODUCT_UI_CLOCK_MIN_YEAR || date->year > PRODUCT_UI_CLOCK_MAX_YEAR ||
        product_ui_alarm_monday(date, &monday) != D_OK ||
        product_ui_time_utc_to_beijing(monday - (int64_t)week * 7 * 86400, &day) != PRODUCT_UI_TIME_OK)
        return D_ERR_INVALID_ARG;
    *anchor = day.date;
    return D_OK;
}

int64_t product_ui_alarm_next_occurrence(const product_ui_alarm_t *rule, int64_t after_utc)
{
    product_ui_datetime_t day;
    int64_t candidate;
    uint8_t offset;
    if (rule == NULL || !rule->enabled || after_utc < PRODUCT_UI_CLOCK_MIN_UTC - 1 ||
        after_utc >= PRODUCT_UI_CLOCK_END_UTC) return 0;
    if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE) {
        day = (product_ui_datetime_t){rule->date, {rule->hour, rule->minute, 0U}};
        if (product_ui_time_beijing_to_utc(&day, &candidate) != PRODUCT_UI_TIME_OK) return 0;
        return candidate > after_utc && candidate <= PRODUCT_UI_CLOCK_END_UTC ? candidate : 0;
    }
    if (product_ui_time_utc_to_beijing(after_utc, &day) != PRODUCT_UI_TIME_OK) return 0;
    day.time = (product_ui_time_of_day_t){rule->hour, rule->minute, 0U};
    if (product_ui_time_domain_validate_alarm(rule) != PRODUCT_UI_TIME_OK) return 0;
    for (offset = 0U; offset <= (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT ? 14U : 7U); offset++) {
        uint8_t week = 0U;
        uint8_t mask;
        if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT &&
            product_ui_alarm_week_index(rule, &day.date, &week) != D_OK) return 0;
        mask = week ? rule->alternate_mask : rule->repeat_mask;
        if ((mask & (1U << product_ui_time_weekday(&day.date))) != 0U &&
            product_ui_time_beijing_to_utc(&day, &candidate) == PRODUCT_UI_TIME_OK &&
            candidate > after_utc && candidate <= PRODUCT_UI_CLOCK_END_UTC) return candidate;
        if (product_ui_time_add_one_day(&day.date) != PRODUCT_UI_TIME_OK ||
            day.date.year > PRODUCT_UI_CLOCK_MAX_YEAR) break;
    }
    return 0;
}

uint16_t product_ui_alarm_model_ring_mask(const product_ui_alarm_model_t *model)
{
    uint16_t mask = 0U;
    uint8_t index;
    if (model == NULL) return mask;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        const product_ui_alarm_record_t *record = &model->saved.records[index];
        if (record->rule.id && (record->state == PRODUCT_UI_ALARM_WAIT_AUDIO || record->state == PRODUCT_UI_ALARM_RINGING))
            mask |= (uint16_t)(1U << index);
    }
    return mask;
}

uint16_t product_ui_alarm_model_stop_mask(const product_ui_alarm_model_t *model)
{
    uint16_t mask = 0U;
    uint8_t index;
    if (model == NULL) return mask;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++)
        if (model->saved.records[index].rule.id && model->saved.records[index].state == PRODUCT_UI_ALARM_STOPPING)
            mask |= (uint16_t)(1U << index);
    return mask;
}
