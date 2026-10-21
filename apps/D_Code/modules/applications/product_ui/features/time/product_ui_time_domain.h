/**
 * @file product_ui_time_domain.h
 * @brief LVGL-free bounded time, alarm and calendar event domain.
 */

#ifndef PRODUCT_UI_TIME_DOMAIN_H
#define PRODUCT_UI_TIME_DOMAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRODUCT_UI_TIME_DOMAIN_MAX_ALARMS             16U
#define PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS              64U
#define PRODUCT_UI_TIME_DOMAIN_ALARM_LABEL_SIZE        24U
#define PRODUCT_UI_TIME_DOMAIN_EVENT_TITLE_SIZE        40U
#define PRODUCT_UI_TIME_DOMAIN_EVENT_LOCATION_SIZE     24U
#define PRODUCT_UI_TIME_DOMAIN_MAX_YEAR               9999U

#define PRODUCT_UI_TIME_WEEKDAY_SUNDAY    (1U << 0)
#define PRODUCT_UI_TIME_WEEKDAY_MONDAY    (1U << 1)
#define PRODUCT_UI_TIME_WEEKDAY_TUESDAY   (1U << 2)
#define PRODUCT_UI_TIME_WEEKDAY_WEDNESDAY (1U << 3)
#define PRODUCT_UI_TIME_WEEKDAY_THURSDAY  (1U << 4)
#define PRODUCT_UI_TIME_WEEKDAY_FRIDAY    (1U << 5)
#define PRODUCT_UI_TIME_WEEKDAY_SATURDAY  (1U << 6)
#define PRODUCT_UI_TIME_WEEKDAY_ALL       0x7FU

typedef enum {
    PRODUCT_UI_TIME_OK = 0,
    PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT = -1,
    PRODUCT_UI_TIME_ERR_UNAVAILABLE = -2,
    PRODUCT_UI_TIME_ERR_NOT_FOUND = -3,
    PRODUCT_UI_TIME_ERR_CAPACITY = -4,
    PRODUCT_UI_TIME_ERR_CONFLICT = -5,
} product_ui_time_result_e;

typedef enum {
    PRODUCT_UI_TIME_CAPABILITY_UNKNOWN = 0,
    PRODUCT_UI_TIME_CAPABILITY_UNAVAILABLE,
    PRODUCT_UI_TIME_CAPABILITY_AVAILABLE,
} product_ui_time_capability_e;

typedef enum {
    PRODUCT_UI_TIME_UNAVAILABLE_NONE = 0,
    PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED,
    PRODUCT_UI_TIME_UNAVAILABLE_NOT_SUPPORTED,
    PRODUCT_UI_TIME_UNAVAILABLE_NOT_READY,
    PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID,
} product_ui_time_unavailable_reason_e;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
} product_ui_date_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} product_ui_time_of_day_t;

typedef struct {
    product_ui_date_t date;
    product_ui_time_of_day_t time;
} product_ui_datetime_t;

typedef enum {
    PRODUCT_UI_ALARM_SCHEDULE_ONCE = 0,
    PRODUCT_UI_ALARM_SCHEDULE_WEEKLY,
    PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT,
} product_ui_alarm_schedule_e;

/* Persistent identities; never use a view's menu index as the stored value. */
typedef enum {
    PRODUCT_UI_ALARM_ROUTINE_BASIC = 0,
    PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST = 1,
    PRODUCT_UI_ALARM_ROUTINE_SINGLE_REST = 2,
    PRODUCT_UI_ALARM_ROUTINE_ALTERNATING = 3,
} product_ui_alarm_routine_e;

typedef struct {
    uint32_t id;
    uint8_t enabled;
    uint8_t alternate_mask;
    uint8_t routine_template;
    product_ui_alarm_schedule_e schedule;
    /* ONCE: execution date; FORTNIGHT: week-one Monday; WEEKLY: ignored. */
    product_ui_date_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t repeat_mask;
    char label[PRODUCT_UI_TIME_DOMAIN_ALARM_LABEL_SIZE];
} product_ui_alarm_t;

typedef struct {
    uint32_t id;
    product_ui_datetime_t start;
    product_ui_datetime_t end;
    uint8_t all_day;
    uint8_t completed;
    char title[PRODUCT_UI_TIME_DOMAIN_EVENT_TITLE_SIZE];
    char location[PRODUCT_UI_TIME_DOMAIN_EVENT_LOCATION_SIZE];
} product_ui_calendar_event_t;

/**
 * Snapshot contract:
 * - The snapshot is always structurally readable.
 * - wall_time_valid is zero unless capability is AVAILABLE.
 * - alarm_runtime_available requires both AVAILABLE and wall_time_valid.
 * - Alarm/event records remain readable while wall-clock capability is absent.
 * - next_alarm fields are invalid when alarm_runtime_available is zero.
 */
typedef struct {
    uint32_t generation;
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
    uint8_t wall_time_valid;
    uint8_t alarm_runtime_available;
    product_ui_datetime_t wall_time;
    uint8_t next_alarm_valid;
    uint32_t next_alarm_id;
    product_ui_datetime_t next_alarm_at;
    uint8_t alarm_count;
    uint8_t event_count;
    product_ui_alarm_t alarms[PRODUCT_UI_TIME_DOMAIN_MAX_ALARMS];
    product_ui_calendar_event_t events[PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS];
} product_ui_time_domain_snapshot_t;

typedef struct {
    uint32_t generation;
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
    uint8_t wall_time_valid;
    uint8_t alarm_runtime_available;
    product_ui_datetime_t wall_time;
    uint8_t next_alarm_valid;
    uint32_t next_alarm_id;
    product_ui_datetime_t next_alarm_at;
    uint8_t alarm_count;
    uint8_t event_count;
} product_ui_time_domain_summary_t;

typedef enum {
    PRODUCT_UI_TIME_COMMAND_RESET = 0,
    PRODUCT_UI_TIME_COMMAND_SET_CAPABILITY,
    PRODUCT_UI_TIME_COMMAND_SET_WALL_TIME,
    PRODUCT_UI_TIME_COMMAND_INVALIDATE_WALL_TIME,
    PRODUCT_UI_TIME_COMMAND_UPSERT_ALARM,
    PRODUCT_UI_TIME_COMMAND_REMOVE_ALARM,
    PRODUCT_UI_TIME_COMMAND_SET_ALARM_ENABLED,
    PRODUCT_UI_TIME_COMMAND_UPSERT_EVENT,
    PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT,
    PRODUCT_UI_TIME_COMMAND_SET_EVENT_COMPLETED,
} product_ui_time_command_type_e;

typedef struct {
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
} product_ui_time_capability_command_t;

typedef struct {
    uint32_t id;
} product_ui_time_id_command_t;

typedef struct {
    uint32_t id;
    uint8_t enabled;
} product_ui_alarm_enabled_command_t;

typedef struct {
    uint32_t id;
    uint8_t completed;
} product_ui_event_completed_command_t;

typedef struct {
    product_ui_time_command_type_e type;
    union {
        product_ui_time_capability_command_t capability;
        product_ui_datetime_t wall_time;
        product_ui_alarm_t alarm;
        product_ui_time_id_command_t id;
        product_ui_alarm_enabled_command_t alarm_enabled;
        product_ui_calendar_event_t event;
        product_ui_event_completed_command_t event_completed;
    } data;
} product_ui_time_domain_command_t;

typedef struct {
    product_ui_time_result_e result;
    uint32_t generation;
    uint32_t affected_id;
} product_ui_time_domain_command_result_t;

/** Alarm storage belongs to the runtime. This pure-C bridge owns no shadow records. */
typedef struct {
    uint32_t generation;
    uint32_t next_id;
    product_ui_datetime_t next_at;
    uint8_t count;
    uint8_t runtime_available;
    uint8_t next_valid;
} product_ui_time_alarm_summary_t;

typedef struct {
    product_ui_time_result_e (*copy)(product_ui_alarm_t *alarms, product_ui_time_alarm_summary_t *summary);
    product_ui_time_result_e (*get)(uint8_t index, product_ui_alarm_t *alarm);
    product_ui_time_result_e (*execute)(const product_ui_time_domain_command_t *command, uint32_t *affected_id);
} product_ui_time_alarm_backend_t;

/** UI/bootstrap thread only; callbacks and their static owner outlive all pages. */
void product_ui_time_domain_bind_alarms(const product_ui_time_alarm_backend_t *backend);

/**
 * Initialize the UI-owned wall/event cache. Bound persistent alarms are not reset.
 * The owner must serialize command and snapshot calls; no internal lock is used.
 */
void product_ui_time_domain_init(void);

/**
 * Execute one bounded state mutation.
 * Alarm/event CRUD remains available without a wall clock. SET_WALL_TIME and
 * next-alarm scheduling require capability AVAILABLE.
 */
product_ui_time_result_e product_ui_time_domain_execute(
    const product_ui_time_domain_command_t *command,
    product_ui_time_domain_command_result_t *result);

/** Copy the current fixed-capacity state into caller-owned storage. */
product_ui_time_result_e product_ui_time_domain_get_snapshot(
    product_ui_time_domain_snapshot_t *snapshot);
product_ui_time_result_e product_ui_time_domain_get_summary(
    product_ui_time_domain_summary_t *summary);
product_ui_time_result_e product_ui_time_domain_get_alarm(
    uint8_t index, product_ui_alarm_t *alarm);
product_ui_time_result_e product_ui_time_domain_get_event(
    uint8_t index, product_ui_calendar_event_t *event);

product_ui_time_result_e product_ui_time_domain_validate_datetime(
    const product_ui_datetime_t *value);
product_ui_time_result_e product_ui_time_domain_validate_alarm(
    const product_ui_alarm_t *alarm);
product_ui_time_result_e product_ui_time_domain_validate_event(
    const product_ui_calendar_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_UI_TIME_DOMAIN_H */
