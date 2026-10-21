/**
 * @file product_ui_time_domain_internal.h
 * @brief Private Gregorian calendar helpers for the Product UI time domain.
 */

#ifndef __PRODUCT_UI_TIME_DOMAIN_INTERNAL_H__
#define __PRODUCT_UI_TIME_DOMAIN_INTERNAL_H__

#include "modules/applications/product_ui/features/time/product_ui_time_domain.h"

int product_ui_time_compare_datetime(const product_ui_datetime_t *left,
                                     const product_ui_datetime_t *right);
product_ui_time_result_e product_ui_time_add_one_day(product_ui_date_t *date);
uint8_t product_ui_time_weekday(const product_ui_date_t *date);
uint8_t product_ui_time_days_in_month(uint16_t year, uint8_t month);
/** UTC epoch seconds to Beijing civil time; no time_t or process timezone. */
product_ui_time_result_e product_ui_time_utc_to_beijing(
    int64_t utc_seconds, product_ui_datetime_t *out);
/** Beijing civil time to UTC seconds; inverse of utc_to_beijing, no time_t. */
product_ui_time_result_e product_ui_time_beijing_to_utc(
    const product_ui_datetime_t *value, int64_t *utc_seconds);

#endif /* __PRODUCT_UI_TIME_DOMAIN_INTERNAL_H__ */
