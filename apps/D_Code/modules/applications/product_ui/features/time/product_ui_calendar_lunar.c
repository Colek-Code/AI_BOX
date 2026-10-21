/**
 * @file product_ui_calendar_lunar.c
 * @brief Checked packed-table conversion adapted from isee15 (MIT).
 * Original copyright (c) 2020 isee15. See calendar_data/LICENSE.isee15.txt.
 * Changes: independent SXTWL tables, bounds, fixed-width types and local symbols.
 */
#include "modules/applications/product_ui/features/time/product_ui_calendar_lunar.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include <stddef.h>

#include "product_ui_calendar_data.inc"

#if PRODUCT_UI_CALENDAR_DATA_FORMAT != 2U || \
    PRODUCT_UI_CALENDAR_DATA_MIN_YEAR != PRODUCT_UI_CALENDAR_MIN_YEAR || \
    PRODUCT_UI_CALENDAR_DATA_MAX_YEAR != PRODUCT_UI_CALENDAR_MAX_YEAR || \
    PRODUCT_UI_CALENDAR_DATA_LUNAR_BASE_YEAR != PRODUCT_UI_CALENDAR_MIN_YEAR - 1U || \
    PRODUCT_UI_CALENDAR_DATA_LUNAR_YEAR_COUNT != PRODUCT_UI_CALENDAR_MAX_YEAR - PRODUCT_UI_CALENDAR_MIN_YEAR + 2U
#error "Calendar range and generated data disagree; regenerate the pinned tables"
#endif

#define PRODUCT_UI_CALENDAR_LUNAR_BASE_YEAR PRODUCT_UI_CALENDAR_DATA_LUNAR_BASE_YEAR
#define PRODUCT_UI_CALENDAR_TABLE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

typedef char product_ui_calendar_table_shape_check[
    PRODUCT_UI_CALENDAR_TABLE_COUNT(s_calendar_lunar_years) == PRODUCT_UI_CALENDAR_DATA_LUNAR_YEAR_COUNT &&
    PRODUCT_UI_CALENDAR_TABLE_COUNT(s_calendar_cny_offsets) == PRODUCT_UI_CALENDAR_DATA_LUNAR_YEAR_COUNT + 1U &&
    PRODUCT_UI_CALENDAR_TABLE_COUNT(s_calendar_term_days_packed) == PRODUCT_UI_CALENDAR_MAX_YEAR - PRODUCT_UI_CALENDAR_MIN_YEAR + 1U
    ? 1 : -1];

static int32_t product_ui_calendar_serial(const product_ui_date_t *date)
{
    int32_t month = (date->month + 9) % 12;
    int32_t year = date->year - month / 10;

    return 365 * year + year / 4 - year / 100 + year / 400 +
           (month * 306 + 5) / 10 + date->day - 1;
}

static int32_t product_ui_calendar_epoch(void)
{
    static const product_ui_date_t epoch = {PRODUCT_UI_CALENDAR_LUNAR_BASE_YEAR, 1U, 1U};
    return product_ui_calendar_serial(&epoch);
}

static void product_ui_calendar_from_serial(int32_t serial,
                                            product_ui_date_t *out)
{
    int32_t year = (int32_t)((10000LL * serial + 14780) / 3652425);
    int32_t ordinal = serial - (365 * year + year / 4 - year / 100 + year / 400);
    int32_t march_month;

    if (ordinal < 0) {
        year--;
        ordinal = serial - (365 * year + year / 4 - year / 100 + year / 400);
    }
    march_month = (100 * ordinal + 52) / 3060;
    out->year = (uint16_t)(year + (march_month + 2) / 12);
    out->month = (uint8_t)((march_month + 2) % 12 + 1);
    out->day = (uint8_t)(ordinal - (march_month * 306 + 5) / 10 + 1);
}

static product_ui_calendar_result_e product_ui_calendar_validate_solar(
    const product_ui_date_t *solar)
{
    if ((solar == NULL) || (solar->year == 0U) || (solar->month == 0U) ||
        (solar->month > 12U) || (solar->day == 0U) ||
        (solar->day > product_ui_time_days_in_month(solar->year, solar->month))) {
        return PRODUCT_UI_CALENDAR_ERR_ARGUMENT;
    }
    if ((solar->year < PRODUCT_UI_CALENDAR_MIN_YEAR) ||
        (solar->year > PRODUCT_UI_CALENDAR_MAX_YEAR)) {
        return PRODUCT_UI_CALENDAR_ERR_RANGE;
    }
    return PRODUCT_UI_CALENDAR_OK;
}

static uint8_t product_ui_calendar_month_length(uint32_t bits, uint8_t index)
{
    return (uint8_t)(29U + ((bits >> index) & 1U));
}

product_ui_calendar_result_e product_ui_calendar_to_lunar(
    const product_ui_date_t *solar, product_ui_calendar_lunar_t *out)
{
    product_ui_calendar_result_e result = product_ui_calendar_validate_solar(solar);
    product_ui_calendar_lunar_t lunar = {0};
    uint16_t year_index;
    uint32_t offset;
    uint32_t bits;
    uint8_t leap;
    uint8_t month_index;
    uint8_t month_count;

    if (out == NULL) {
        return PRODUCT_UI_CALENDAR_ERR_ARGUMENT;
    }
    if (result != PRODUCT_UI_CALENDAR_OK) {
        return result;
    }
    offset = (uint32_t)(product_ui_calendar_serial(solar) - product_ui_calendar_epoch());
    year_index = solar->year - PRODUCT_UI_CALENDAR_LUNAR_BASE_YEAR;
    if (offset < s_calendar_cny_offsets[year_index]) {
        year_index--;
    }
    if ((year_index >= PRODUCT_UI_CALENDAR_DATA_LUNAR_YEAR_COUNT) ||
        (offset < s_calendar_cny_offsets[year_index]) ||
        (offset >= s_calendar_cny_offsets[year_index + 1U])) {
        return PRODUCT_UI_CALENDAR_ERR_DATA;
    }
    offset -= s_calendar_cny_offsets[year_index];
    bits = s_calendar_lunar_years[year_index];
    leap = (uint8_t)((bits >> 13U) & 15U);
    if (leap > 12U) {
        return PRODUCT_UI_CALENDAR_ERR_DATA;
    }
    month_count = leap ? 13U : 12U;
    for (month_index = 0U; month_index < month_count; month_index++) {
        uint8_t length = product_ui_calendar_month_length(bits, month_index);
        if (offset < length) {
            break;
        }
        offset -= length;
    }
    if (month_index >= month_count) {
        return PRODUCT_UI_CALENDAR_ERR_DATA;
    }
    lunar.year = year_index + PRODUCT_UI_CALENDAR_LUNAR_BASE_YEAR;
    lunar.month = month_index + 1U;
    lunar.day = (uint8_t)(offset + 1);
    if ((leap != 0U) && (month_index >= leap)) {
        lunar.month--;
        lunar.is_leap = month_index == leap;
    }
    *out = lunar;
    return PRODUCT_UI_CALENDAR_OK;
}

product_ui_calendar_result_e product_ui_calendar_to_solar(
    const product_ui_calendar_lunar_t *lunar, product_ui_date_t *out)
{
    uint16_t year_index;
    uint32_t bits;
    uint8_t leap;
    uint8_t month_index;
    uint8_t index;
    uint32_t offset;
    product_ui_date_t solar;

    if ((lunar == NULL) || (out == NULL) || (lunar->month == 0U) ||
        (lunar->month > 12U) || (lunar->day == 0U) || (lunar->is_leap > 1U)) {
        return PRODUCT_UI_CALENDAR_ERR_ARGUMENT;
    }
    if ((lunar->year < PRODUCT_UI_CALENDAR_LUNAR_BASE_YEAR) ||
        (lunar->year > PRODUCT_UI_CALENDAR_MAX_YEAR)) {
        return PRODUCT_UI_CALENDAR_ERR_RANGE;
    }
    year_index = lunar->year - PRODUCT_UI_CALENDAR_LUNAR_BASE_YEAR;
    bits = s_calendar_lunar_years[year_index];
    leap = (uint8_t)((bits >> 13U) & 15U);
    if ((leap > 12U) || (lunar->is_leap && (lunar->month != leap))) {
        return PRODUCT_UI_CALENDAR_ERR_ARGUMENT;
    }
    month_index = lunar->month - 1U;
    if ((leap != 0U) && ((lunar->month > leap) || lunar->is_leap)) {
        month_index++;
    }
    if (lunar->day > product_ui_calendar_month_length(bits, month_index)) {
        return PRODUCT_UI_CALENDAR_ERR_ARGUMENT;
    }
    offset = s_calendar_cny_offsets[year_index] + lunar->day - 1;
    for (index = 0U; index < month_index; index++) {
        offset += product_ui_calendar_month_length(bits, index);
    }
    product_ui_calendar_from_serial(product_ui_calendar_epoch() + (int32_t)offset, &solar);
    if (product_ui_calendar_validate_solar(&solar) != PRODUCT_UI_CALENDAR_OK) {
        return PRODUCT_UI_CALENDAR_ERR_RANGE;
    }
    *out = solar;
    return PRODUCT_UI_CALENDAR_OK;
}

uint8_t product_ui_calendar_term_for_date(const product_ui_date_t *solar)
{
    uint16_t year_index;
    uint8_t first;
    uint8_t packed;

    if (product_ui_calendar_validate_solar(solar) != PRODUCT_UI_CALENDAR_OK) {
        return PRODUCT_UI_CALENDAR_TERM_NONE;
    }
    year_index = solar->year - PRODUCT_UI_CALENDAR_MIN_YEAR;
    first = (solar->month - 1U) * 2U + 1U;
    packed = s_calendar_term_days_packed[year_index][solar->month - 1U];
    if ((packed & 15U) == solar->day) {
        return first;
    }
    if ((packed >> 4U) + 16U == solar->day) {
        return first + 1U;
    }
    return PRODUCT_UI_CALENDAR_TERM_NONE;
}

uint8_t product_ui_calendar_is_year_end(const product_ui_date_t *solar)
{
    uint32_t tomorrow;
    uint16_t index;

    if (product_ui_calendar_validate_solar(solar) != PRODUCT_UI_CALENDAR_OK) {
        return 0U;
    }
    tomorrow = (uint32_t)(product_ui_calendar_serial(solar) - product_ui_calendar_epoch()) + 1U;
    index = solar->year - PRODUCT_UI_CALENDAR_LUNAR_BASE_YEAR;
    return tomorrow == s_calendar_cny_offsets[index];
}
