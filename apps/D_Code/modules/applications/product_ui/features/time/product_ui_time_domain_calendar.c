/**
 * @file product_ui_time_domain_calendar.c
 * @brief Gregorian validation and calendar math for the Product UI time domain.
 */

#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include <stddef.h>

static uint8_t product_ui_time_is_leap_year(uint16_t year)
{
    if ((year % 400U) == 0U) {
        return 1U;
    }
    if ((year % 100U) == 0U) {
        return 0U;
    }
    return ((year % 4U) == 0U) ? 1U : 0U;
}

uint8_t product_ui_time_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };

    if ((month == 0U) || (month > 12U)) {
        return 0U;
    }
    if ((month == 2U) && product_ui_time_is_leap_year(year)) {
        return 29U;
    }
    return days[month - 1U];
}

product_ui_time_result_e product_ui_time_utc_to_beijing(
    int64_t utc_seconds, product_ui_datetime_t *out)
{
    product_ui_datetime_t value = {{1970U, 1U, 1U}, {0U, 0U, 0U}};
    int64_t local_seconds;
    uint32_t days;
    uint32_t remaining;
    uint16_t year_days;
    uint8_t month_days;

    /* Bound before adding the offset, including INT64_MAX failure samples. */
    if ((out == NULL) || (utc_seconds < 0) || (utc_seconds > 253402271999LL)) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    local_seconds = utc_seconds + 28800LL;
    days = (uint32_t)(local_seconds / 86400LL);
    remaining = (uint32_t)(local_seconds % 86400LL);
    for (;;) {
        year_days = product_ui_time_is_leap_year(value.date.year) ? 366U : 365U;
        if (days < year_days) {
            break;
        }
        days -= year_days;
        value.date.year++;
    }
    for (;;) {
        month_days = product_ui_time_days_in_month(value.date.year, value.date.month);
        if (days < month_days) {
            break;
        }
        days -= month_days;
        value.date.month++;
    }
    value.date.day = (uint8_t)(days + 1U);
    value.time.hour = (uint8_t)(remaining / 3600U);
    value.time.minute = (uint8_t)((remaining % 3600U) / 60U);
    value.time.second = (uint8_t)(remaining % 60U);
    *out = value;
    return PRODUCT_UI_TIME_OK;
}

product_ui_time_result_e product_ui_time_beijing_to_utc(
    const product_ui_datetime_t *value, int64_t *utc_seconds)
{
    int64_t days = 0;
    int64_t seconds;
    uint16_t year;
    uint8_t month;
    if (utc_seconds == NULL || product_ui_time_domain_validate_datetime(value) != PRODUCT_UI_TIME_OK ||
        value->date.year < 1970U) return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    for (year = 1970U; year < value->date.year; year++)
        days += product_ui_time_is_leap_year(year) ? 366 : 365;
    for (month = 1U; month < value->date.month; month++)
        days += product_ui_time_days_in_month(value->date.year, month);
    days += value->date.day - 1U;
    seconds = days * 86400LL + value->time.hour * 3600LL + value->time.minute * 60LL + value->time.second - 28800LL;
    if (seconds < 0) return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    *utc_seconds = seconds;
    return PRODUCT_UI_TIME_OK;
}

static product_ui_time_result_e product_ui_time_validate_date(
    const product_ui_date_t *date)
{
    uint8_t max_day;

    if ((date == NULL) || (date->year == 0U) ||
        (date->year > PRODUCT_UI_TIME_DOMAIN_MAX_YEAR)) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    max_day = product_ui_time_days_in_month(date->year, date->month);
    if ((date->day == 0U) || (date->day > max_day)) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    return PRODUCT_UI_TIME_OK;
}

product_ui_time_result_e product_ui_time_domain_validate_datetime(
    const product_ui_datetime_t *value)
{
    if ((value == NULL) ||
        (product_ui_time_validate_date(&value->date) != PRODUCT_UI_TIME_OK) ||
        (value->time.hour > 23U) ||
        (value->time.minute > 59U) ||
        (value->time.second > 59U)) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    return PRODUCT_UI_TIME_OK;
}

int product_ui_time_compare_datetime(const product_ui_datetime_t *left,
                                     const product_ui_datetime_t *right)
{
    if (left->date.year != right->date.year) {
        return (left->date.year < right->date.year) ? -1 : 1;
    }
    if (left->date.month != right->date.month) {
        return (left->date.month < right->date.month) ? -1 : 1;
    }
    if (left->date.day != right->date.day) {
        return (left->date.day < right->date.day) ? -1 : 1;
    }
    if (left->time.hour != right->time.hour) {
        return (left->time.hour < right->time.hour) ? -1 : 1;
    }
    if (left->time.minute != right->time.minute) {
        return (left->time.minute < right->time.minute) ? -1 : 1;
    }
    if (left->time.second != right->time.second) {
        return (left->time.second < right->time.second) ? -1 : 1;
    }
    return 0;
}

product_ui_time_result_e product_ui_time_add_one_day(product_ui_date_t *date)
{
    uint8_t max_day;

    if (product_ui_time_validate_date(date) != PRODUCT_UI_TIME_OK) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    max_day = product_ui_time_days_in_month(date->year, date->month);

    if (date->day < max_day) {
        date->day++;
        return PRODUCT_UI_TIME_OK;
    }
    if (date->month < 12U) {
        date->day = 1U;
        date->month++;
        return PRODUCT_UI_TIME_OK;
    }
    if (date->year >= PRODUCT_UI_TIME_DOMAIN_MAX_YEAR) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    date->day = 1U;
    date->month = 1U;
    date->year++;
    return PRODUCT_UI_TIME_OK;
}

uint8_t product_ui_time_weekday(const product_ui_date_t *date)
{
    static const uint8_t offsets[] = {
        0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U,
    };
    uint32_t year = date->year;

    if (date->month < 3U) {
        year--;
    }
    return (uint8_t)((year + (year / 4U) - (year / 100U) +
                      (year / 400U) + offsets[date->month - 1U] +
                      date->day) % 7U);
}

product_ui_time_result_e product_ui_time_domain_validate_alarm(
    const product_ui_alarm_t *alarm)
{
    if ((alarm == NULL) || (alarm->enabled > 1U) ||
        (alarm->hour > 23U) || (alarm->minute > 59U) ||
        ((alarm->repeat_mask & ~PRODUCT_UI_TIME_WEEKDAY_ALL) != 0U) ||
        ((alarm->alternate_mask & ~PRODUCT_UI_TIME_WEEKDAY_ALL) != 0U)) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    if (alarm->schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE) {
        if ((alarm->repeat_mask != 0U) || alarm->alternate_mask ||
            alarm->routine_template != PRODUCT_UI_ALARM_ROUTINE_BASIC ||
            (product_ui_time_validate_date(&alarm->date) != PRODUCT_UI_TIME_OK)) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
        return PRODUCT_UI_TIME_OK;
    }
    if (alarm->schedule == PRODUCT_UI_ALARM_SCHEDULE_WEEKLY) {
        /* Old weekly records can contain an unused once-date. Keep accepting it. */
        return alarm->repeat_mask && !alarm->alternate_mask &&
            alarm->routine_template <= PRODUCT_UI_ALARM_ROUTINE_SINGLE_REST ?
            PRODUCT_UI_TIME_OK : PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    if (alarm->schedule == PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT &&
        (alarm->repeat_mask | alarm->alternate_mask) &&
        alarm->routine_template == PRODUCT_UI_ALARM_ROUTINE_ALTERNATING &&
        product_ui_time_validate_date(&alarm->date) == PRODUCT_UI_TIME_OK &&
        alarm->date.year >= 2023U && alarm->date.year <= 2099U &&
        (alarm->date.year != 2023U || (alarm->date.month == 12U && alarm->date.day >= 25U)) &&
        product_ui_time_weekday(&alarm->date) == 1U) return PRODUCT_UI_TIME_OK;
    return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
}

product_ui_time_result_e product_ui_time_domain_validate_event(
    const product_ui_calendar_event_t *event)
{
    if ((event == NULL) || (event->all_day > 1U) ||
        (event->completed > 1U) ||
        (product_ui_time_domain_validate_datetime(&event->start) != PRODUCT_UI_TIME_OK) ||
        (product_ui_time_domain_validate_datetime(&event->end) != PRODUCT_UI_TIME_OK) ||
        (product_ui_time_compare_datetime(&event->start, &event->end) > 0)) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    return PRODUCT_UI_TIME_OK;
}
