/** @file product_ui_alarm_view.h @brief Alarm presentation and immutable input identities. */
#ifndef PRODUCT_UI_ALARM_VIEW_H
#define PRODUCT_UI_ALARM_VIEW_H
#include "product_ui_alarm_service.h"
#include "product_ui_time_controls.h"
typedef enum {
    PRODUCT_UI_ALARM_LIST, PRODUCT_UI_ALARM_EDITOR, PRODUCT_UI_ALARM_SETTINGS,
    PRODUCT_UI_ALARM_ROUTINE, PRODUCT_UI_ALARM_MODE_COUNT
} product_ui_alarm_mode_e;
typedef enum {
    PRODUCT_UI_ALARM_REPEAT_ONCE, PRODUCT_UI_ALARM_REPEAT_DAILY, PRODUCT_UI_ALARM_REPEAT_DOUBLE,
    PRODUCT_UI_ALARM_REPEAT_SINGLE, PRODUCT_UI_ALARM_REPEAT_FORTNIGHT, PRODUCT_UI_ALARM_REPEAT_CUSTOM,
    PRODUCT_UI_ALARM_REPEAT_COUNT
} product_ui_alarm_repeat_e;
typedef enum {
    PRODUCT_UI_ALARM_UI_NONE, PRODUCT_UI_ALARM_UI_NEW, PRODUCT_UI_ALARM_UI_EDIT,
    PRODUCT_UI_ALARM_UI_TOGGLE, PRODUCT_UI_ALARM_UI_SETTINGS, PRODUCT_UI_ALARM_UI_BACK,
    PRODUCT_UI_ALARM_UI_SAVE, PRODUCT_UI_ALARM_UI_DELETE, PRODUCT_UI_ALARM_UI_HOUR,
    PRODUCT_UI_ALARM_UI_MINUTE, PRODUCT_UI_ALARM_UI_REPEAT, PRODUCT_UI_ALARM_UI_WEEKDAY,
    PRODUCT_UI_ALARM_UI_LABEL, PRODUCT_UI_ALARM_UI_ENABLED, PRODUCT_UI_ALARM_UI_VOLUME,
    PRODUCT_UI_ALARM_UI_VOLUME_DOWN, PRODUCT_UI_ALARM_UI_VOLUME_UP,
    PRODUCT_UI_ALARM_UI_PREVIEW, PRODUCT_UI_ALARM_UI_RETRY_SAVE, PRODUCT_UI_ALARM_UI_TIME,
    PRODUCT_UI_ALARM_UI_ROUTINE, PRODUCT_UI_ALARM_UI_ALTERNATE_DAY,
    PRODUCT_UI_ALARM_UI_PHASE, PRODUCT_UI_ALARM_UI_DEFAULT_DAYS,
} product_ui_alarm_ui_action_e;
typedef struct {
    product_ui_alarm_ui_action_e action;
    uint32_t id;
    uint32_t revision;
    uint32_t view_generation;
    uint16_t value;
} product_ui_alarm_ui_event_t;
typedef void (*product_ui_alarm_ui_action_fn)(const product_ui_alarm_ui_event_t *event);
typedef struct {
    product_ui_alarm_t rule;
    product_ui_alarm_repeat_e kind;
    uint8_t phase_confirmed;
} product_ui_alarm_routine_draft_t;

typedef struct {
    product_ui_alarm_record_t record;
    product_ui_alarm_routine_draft_t routine;
    uint8_t volume;
    uint8_t dirty;
    uint8_t preview_active;
    uint8_t has_conflict;
} product_ui_alarm_draft_t;

int product_ui_alarm_view_show(lv_obj_t *parent, product_ui_alarm_mode_e mode,
    const product_ui_alarm_draft_t *draft, product_ui_alarm_ui_action_fn action);
void product_ui_alarm_view_refresh(const product_ui_alarm_snapshot_t *snapshot, const product_ui_alarm_draft_t *draft);
void product_ui_alarm_view_update(uint32_t elapsed_ms);
void product_ui_alarm_view_destroy(void);
void product_ui_alarm_view_cancel_input(void);
uint32_t product_ui_alarm_view_generation(void);
int32_t product_ui_alarm_view_scroll(void);
void product_ui_alarm_view_restore_scroll(int32_t y);
lv_obj_t *product_ui_alarm_view_nav_button(lv_obj_t *parent, const char *text, product_ui_alarm_ui_action_e action);
#endif
