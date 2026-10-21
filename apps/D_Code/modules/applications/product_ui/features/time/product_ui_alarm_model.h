/**
 * @file product_ui_alarm_model.h
 * @brief Bounded alarm rules and instances, independent of UI, SDK and storage.
 * @author D_Code
 * @date 2026-09-21
 */
#ifndef PRODUCT_UI_ALARM_MODEL_H
#define PRODUCT_UI_ALARM_MODEL_H
#include "product_ui_clock_model.h"
#include "framework/d_error.h"

#define PRODUCT_UI_ALARM_COUNT 16U
#define PRODUCT_UI_ALARM_RECORD_SIZE 1536U
#define PRODUCT_UI_ALARM_LATE_SECONDS 120U
#define PRODUCT_UI_ALARM_RING_MS 300000U
#define PRODUCT_UI_ALARM_SNOOZE_MINUTES 5U
#define PRODUCT_UI_ALARM_SNOOZE_SECONDS (PRODUCT_UI_ALARM_SNOOZE_MINUTES * 60U)
#define PRODUCT_UI_ALARM_SNOOZE_MS (PRODUCT_UI_ALARM_SNOOZE_SECONDS * 1000U)
#define PRODUCT_UI_ALARM_SNOOZE_LIMIT 3U
#define PRODUCT_UI_ALARM_DEFAULT_VOLUME 60U
/* Preserve records from newer firmware; distinct from corrupt or absent data. */
#define PRODUCT_UI_ALARM_ERR_VERSION ((d_err_t)-40)

typedef enum {
    PRODUCT_UI_ALARM_IDLE = 0,
    PRODUCT_UI_ALARM_WAIT_AUDIO,
    PRODUCT_UI_ALARM_RINGING,
    PRODUCT_UI_ALARM_STOPPING,
    PRODUCT_UI_ALARM_SNOOZED,
    PRODUCT_UI_ALARM_MISSED,
    PRODUCT_UI_ALARM_DONE,
    PRODUCT_UI_ALARM_TIME_CONFLICT,
} product_ui_alarm_instance_e;

typedef struct {
    product_ui_alarm_t rule;
    uint32_t rule_revision;
    uint32_t instance_generation;
    int64_t occurrence_utc;
    int64_t last_handled_utc;
    int64_t snooze_utc;
    int64_t next_regular_utc;
    uint8_t label_kind;
    uint8_t state;
    uint8_t snooze_count;
    uint8_t stop_action;
    uint8_t notice;
} product_ui_alarm_record_t;

typedef struct {
    product_ui_alarm_record_t records[PRODUCT_UI_ALARM_COUNT];
    uint32_t next_id;
    uint8_t volume;
} product_ui_alarm_saved_t;

typedef struct {
    product_ui_alarm_saved_t saved;
    product_ui_clock_snapshot_t clock;
    uint64_t deadlines[PRODUCT_UI_ALARM_COUNT];
    uint32_t revision;
    uint32_t group_generation;
    uint16_t restore_mask;
    uint8_t has_clock;
} product_ui_alarm_model_t;

typedef enum {
    PRODUCT_UI_ALARM_EDIT = 0,
    PRODUCT_UI_ALARM_DELETE,
    PRODUCT_UI_ALARM_ENABLE,
    PRODUCT_UI_ALARM_STOP_GROUP,
    PRODUCT_UI_ALARM_SNOOZE_GROUP,
    PRODUCT_UI_ALARM_CLEAR_NOTICE,
    PRODUCT_UI_ALARM_VOLUME,
} product_ui_alarm_action_e;

typedef struct {
    product_ui_alarm_action_e action;
    product_ui_alarm_t rule;
    uint32_t id;
    uint32_t expected_revision;
    uint32_t group_generation;
    uint8_t enabled;
    uint8_t volume;
    uint8_t resolve_once;
    uint8_t label_kind;
} product_ui_alarm_command_t;

void product_ui_alarm_model_init(product_ui_alarm_model_t *model, const product_ui_alarm_saved_t *saved);
/** Populate a confirmed fresh, empty model once; storage absence is the caller's responsibility. */
d_err_t product_ui_alarm_model_seed_defaults(product_ui_alarm_model_t *model);
void product_ui_alarm_model_advance(product_ui_alarm_model_t *model, const product_ui_clock_snapshot_t *clock);
d_err_t product_ui_alarm_model_submit(product_ui_alarm_model_t *model,
    const product_ui_alarm_command_t *command, uint32_t *affected_id);
void product_ui_alarm_model_audio_started(product_ui_alarm_model_t *model, uint32_t group_generation);
void product_ui_alarm_model_audio_stopped(product_ui_alarm_model_t *model);
uint16_t product_ui_alarm_model_ring_mask(const product_ui_alarm_model_t *model);
uint16_t product_ui_alarm_model_stop_mask(const product_ui_alarm_model_t *model);
int product_ui_alarm_model_find(const product_ui_alarm_model_t *model, uint32_t id);
/** Shared bounded validation used by the command path and persistent decoder. */
uint8_t product_ui_alarm_record_valid(const product_ui_alarm_record_t *record);
int64_t product_ui_alarm_next_occurrence(const product_ui_alarm_t *rule, int64_t after_utc);
/** Shared UI/scheduler phase: zero is week one, one is week two. */
d_err_t product_ui_alarm_week_index(const product_ui_alarm_t *rule,
    const product_ui_date_t *date, uint8_t *week);
/** Resolve this week's selection (0/1) to a fixed week-one Monday. */
d_err_t product_ui_alarm_anchor_for_week(const product_ui_date_t *date,
    uint8_t week, product_ui_date_t *anchor);
const char *product_ui_alarm_label(uint8_t kind);
void product_ui_alarm_encode(const product_ui_alarm_saved_t *saved, uint32_t sequence, uint8_t *record);
d_err_t product_ui_alarm_decode(const uint8_t *record, product_ui_alarm_saved_t *saved, uint32_t *sequence);
uint8_t product_ui_alarm_sequence_after(uint32_t left, uint32_t right);
#endif
