/** @file product_ui_calendar_lunar.h
 * @brief Private packed calendar decoder; no clock or platform dependency.
 */
#ifndef __PRODUCT_UI_CALENDAR_LUNAR_H__
#define __PRODUCT_UI_CALENDAR_LUNAR_H__

#include "modules/applications/product_ui/features/time/product_ui_calendar_model.h"

product_ui_calendar_result_e product_ui_calendar_to_lunar(
    const product_ui_date_t *solar, product_ui_calendar_lunar_t *out);
product_ui_calendar_result_e product_ui_calendar_to_solar(
    const product_ui_calendar_lunar_t *lunar, product_ui_date_t *out);
uint8_t product_ui_calendar_term_for_date(const product_ui_date_t *solar);
uint8_t product_ui_calendar_is_year_end(const product_ui_date_t *solar);

#endif
