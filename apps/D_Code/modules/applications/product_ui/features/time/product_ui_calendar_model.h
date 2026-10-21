/**
 * @file product_ui_calendar_model.h
 * @brief Bounded Beijing civil calendar queries, independent of clock and LVGL.
 */
#ifndef __PRODUCT_UI_CALENDAR_MODEL_H__
#define __PRODUCT_UI_CALENDAR_MODEL_H__

#include "modules/applications/product_ui/features/time/product_ui_time_domain.h"

#define PRODUCT_UI_CALENDAR_MIN_YEAR 1900U
#define PRODUCT_UI_CALENDAR_MAX_YEAR 2099U
#define PRODUCT_UI_CALENDAR_BROWSE_ANCHOR_YEAR 2000U
#define PRODUCT_UI_CALENDAR_CELL_COUNT 42U

typedef enum {
    PRODUCT_UI_CALENDAR_OK = 0,
    PRODUCT_UI_CALENDAR_ERR_ARGUMENT = -1,
    PRODUCT_UI_CALENDAR_ERR_RANGE = -2,
    PRODUCT_UI_CALENDAR_ERR_DATA = -3,
} product_ui_calendar_result_e;

typedef enum {
    PRODUCT_UI_CALENDAR_NEW_YEAR = 1U << 0,
    PRODUCT_UI_CALENDAR_WOMENS_DAY = 1U << 1,
    PRODUCT_UI_CALENDAR_LABOUR_DAY = 1U << 2,
    PRODUCT_UI_CALENDAR_CHILDRENS_DAY = 1U << 3,
    PRODUCT_UI_CALENDAR_TEACHERS_DAY = 1U << 4,
    PRODUCT_UI_CALENDAR_NATIONAL_DAY = 1U << 5,
    PRODUCT_UI_CALENDAR_SPRING_FESTIVAL = 1U << 6,
    PRODUCT_UI_CALENDAR_LANTERN_FESTIVAL = 1U << 7,
    PRODUCT_UI_CALENDAR_DRAGON_BOAT = 1U << 8,
    PRODUCT_UI_CALENDAR_QIXI = 1U << 9,
    PRODUCT_UI_CALENDAR_MID_AUTUMN = 1U << 10,
    PRODUCT_UI_CALENDAR_DOUBLE_NINTH = 1U << 11,
    PRODUCT_UI_CALENDAR_LABA = 1U << 12,
    PRODUCT_UI_CALENDAR_NEW_YEARS_EVE = 1U << 13,
    PRODUCT_UI_CALENDAR_QINGMING = 1U << 14,
} product_ui_calendar_festival_e;

/* Chronological order in each Gregorian year; zero means no solar term. */
typedef enum {
    PRODUCT_UI_CALENDAR_TERM_NONE = 0,
    PRODUCT_UI_CALENDAR_MINOR_COLD, PRODUCT_UI_CALENDAR_MAJOR_COLD,
    PRODUCT_UI_CALENDAR_START_SPRING, PRODUCT_UI_CALENDAR_RAIN_WATER,
    PRODUCT_UI_CALENDAR_AWAKENING, PRODUCT_UI_CALENDAR_SPRING_EQUINOX,
    PRODUCT_UI_CALENDAR_CLEAR_BRIGHT, PRODUCT_UI_CALENDAR_GRAIN_RAIN,
    PRODUCT_UI_CALENDAR_START_SUMMER, PRODUCT_UI_CALENDAR_GRAIN_BUDS,
    PRODUCT_UI_CALENDAR_GRAIN_EAR, PRODUCT_UI_CALENDAR_SUMMER_SOLSTICE,
    PRODUCT_UI_CALENDAR_MINOR_HEAT, PRODUCT_UI_CALENDAR_MAJOR_HEAT,
    PRODUCT_UI_CALENDAR_START_AUTUMN, PRODUCT_UI_CALENDAR_END_HEAT,
    PRODUCT_UI_CALENDAR_WHITE_DEW, PRODUCT_UI_CALENDAR_AUTUMN_EQUINOX,
    PRODUCT_UI_CALENDAR_COLD_DEW, PRODUCT_UI_CALENDAR_FROST,
    PRODUCT_UI_CALENDAR_START_WINTER, PRODUCT_UI_CALENDAR_MINOR_SNOW,
    PRODUCT_UI_CALENDAR_MAJOR_SNOW, PRODUCT_UI_CALENDAR_WINTER_SOLSTICE,
} product_ui_calendar_term_e;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t is_leap;
} product_ui_calendar_lunar_t;

typedef struct {
    product_ui_date_t solar;
    product_ui_calendar_lunar_t lunar;
    uint16_t festivals;
    uint8_t weekday; /* Monday=0 through Sunday=6. */
    uint8_t term;
} product_ui_calendar_day_t;

typedef enum {
    PRODUCT_UI_CALENDAR_CELL_OUT_OF_RANGE = 0,
    PRODUCT_UI_CALENDAR_CELL_ADJACENT,
    PRODUCT_UI_CALENDAR_CELL_CURRENT,
} product_ui_calendar_cell_state_e;

typedef struct {
    product_ui_calendar_day_t day;
    uint8_t state;
} product_ui_calendar_cell_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    product_ui_calendar_cell_t cells[PRODUCT_UI_CALENDAR_CELL_COUNT];
} product_ui_calendar_month_t;

/** Invalid arguments leave output unchanged; data errors zero affected output. */
product_ui_calendar_result_e product_ui_calendar_get_day(
    const product_ui_date_t *date, product_ui_calendar_day_t *out);
product_ui_calendar_result_e product_ui_calendar_build_month(
    uint16_t year, uint8_t month, product_ui_calendar_month_t *out);

#endif
