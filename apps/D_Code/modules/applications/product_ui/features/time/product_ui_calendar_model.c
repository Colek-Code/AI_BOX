/** @file product_ui_calendar_model.c
 * @brief Pure day/festival queries and Monday-first 42-cell month models.
 */
#include "modules/applications/product_ui/features/time/product_ui_calendar_model.h"
#include "modules/applications/product_ui/features/time/product_ui_calendar_lunar.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t month;
    uint8_t day;
    uint16_t mask;
} product_ui_calendar_festival_rule_t;

static uint16_t product_ui_calendar_festivals(const product_ui_calendar_day_t *day)
{
    static const product_ui_calendar_festival_rule_t solar[] = {
        {1U, 1U, PRODUCT_UI_CALENDAR_NEW_YEAR},
        {3U, 8U, PRODUCT_UI_CALENDAR_WOMENS_DAY},
        {5U, 1U, PRODUCT_UI_CALENDAR_LABOUR_DAY},
        {6U, 1U, PRODUCT_UI_CALENDAR_CHILDRENS_DAY},
        {9U, 10U, PRODUCT_UI_CALENDAR_TEACHERS_DAY},
        {10U, 1U, PRODUCT_UI_CALENDAR_NATIONAL_DAY},
    };
    static const product_ui_calendar_festival_rule_t lunar[] = {
        {1U, 1U, PRODUCT_UI_CALENDAR_SPRING_FESTIVAL},
        {1U, 15U, PRODUCT_UI_CALENDAR_LANTERN_FESTIVAL},
        {5U, 5U, PRODUCT_UI_CALENDAR_DRAGON_BOAT},
        {7U, 7U, PRODUCT_UI_CALENDAR_QIXI},
        {8U, 15U, PRODUCT_UI_CALENDAR_MID_AUTUMN},
        {9U, 9U, PRODUCT_UI_CALENDAR_DOUBLE_NINTH},
        {12U, 8U, PRODUCT_UI_CALENDAR_LABA},
    };
    uint16_t mask = 0U;
    uint8_t index;

    for (index = 0U; index < sizeof(solar) / sizeof(solar[0]); index++) {
        if ((day->solar.month == solar[index].month) &&
            (day->solar.day == solar[index].day)) {
            mask |= solar[index].mask;
        }
    }
    if (!day->lunar.is_leap) {
        for (index = 0U; index < sizeof(lunar) / sizeof(lunar[0]); index++) {
            if ((day->lunar.month == lunar[index].month) &&
                (day->lunar.day == lunar[index].day)) {
                mask |= lunar[index].mask;
            }
        }
    }
    if (product_ui_calendar_is_year_end(&day->solar)) {
        mask |= PRODUCT_UI_CALENDAR_NEW_YEARS_EVE;
    }
    if (day->term == PRODUCT_UI_CALENDAR_CLEAR_BRIGHT) {
        mask |= PRODUCT_UI_CALENDAR_QINGMING;
    }
    return mask;
}

product_ui_calendar_result_e product_ui_calendar_get_day(
    const product_ui_date_t *date, product_ui_calendar_day_t *out)
{
    product_ui_calendar_day_t day = {0};
    product_ui_calendar_result_e result;

    if (out == NULL) {
        return PRODUCT_UI_CALENDAR_ERR_ARGUMENT;
    }
    result = product_ui_calendar_to_lunar(date, &day.lunar);
    if (result != PRODUCT_UI_CALENDAR_OK) {
        if (result == PRODUCT_UI_CALENDAR_ERR_DATA) {
            memset(out, 0, sizeof(*out));
        }
        return result;
    }
    day.solar = *date;
    day.weekday = (product_ui_time_weekday(date) + 6U) % 7U;
    day.term = product_ui_calendar_term_for_date(date);
    day.festivals = product_ui_calendar_festivals(&day);
    *out = day;
    return PRODUCT_UI_CALENDAR_OK;
}

product_ui_calendar_result_e product_ui_calendar_build_month(
    uint16_t year, uint8_t month, product_ui_calendar_month_t *out)
{
    product_ui_date_t date = {year, month, 1U};
    uint8_t leading;
    uint8_t index;

    if ((out == NULL) || (month == 0U) || (month > 12U) || (year == 0U)) {
        return PRODUCT_UI_CALENDAR_ERR_ARGUMENT;
    }
    if ((year < PRODUCT_UI_CALENDAR_MIN_YEAR) ||
        (year > PRODUCT_UI_CALENDAR_MAX_YEAR)) {
        return PRODUCT_UI_CALENDAR_ERR_RANGE;
    }
    leading = (product_ui_time_weekday(&date) + 6U) % 7U;
    if (leading != 0U) {
        if (month == 1U) {
            date.year--;
            date.month = 12U;
        } else {
            date.month--;
        }
        date.day = product_ui_time_days_in_month(date.year, date.month) - leading + 1U;
    }
    memset(out, 0, sizeof(*out));
    out->year = year;
    out->month = month;
    for (index = 0U; index < PRODUCT_UI_CALENDAR_CELL_COUNT; index++) {
        product_ui_calendar_cell_t *cell = &out->cells[index];
        product_ui_calendar_result_e result = product_ui_calendar_get_day(&date, &cell->day);

        if (result == PRODUCT_UI_CALENDAR_ERR_RANGE) {
            cell->day.solar = date;
            cell->day.weekday = index % 7U;
            cell->state = PRODUCT_UI_CALENDAR_CELL_OUT_OF_RANGE;
        } else if (result == PRODUCT_UI_CALENDAR_OK) {
            cell->state = date.month == month ? PRODUCT_UI_CALENDAR_CELL_CURRENT :
                                               PRODUCT_UI_CALENDAR_CELL_ADJACENT;
        } else {
            memset(out, 0, sizeof(*out));
            return PRODUCT_UI_CALENDAR_ERR_DATA;
        }
        if (product_ui_time_add_one_day(&date) != PRODUCT_UI_TIME_OK) {
            memset(out, 0, sizeof(*out));
            return PRODUCT_UI_CALENDAR_ERR_DATA;
        }
    }
    return PRODUCT_UI_CALENDAR_OK;
}
