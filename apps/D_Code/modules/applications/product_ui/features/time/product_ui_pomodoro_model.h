/** @file product_ui_pomodoro_model.h @brief Bounded, clock-injected Pomodoro domain. */
#ifndef PRODUCT_UI_POMODORO_MODEL_H
#define PRODUCT_UI_POMODORO_MODEL_H
#include <stdint.h>
#include "framework/d_error.h"

#define PRODUCT_UI_POMODORO_DAYS 7U
#define PRODUCT_UI_POMODORO_RECORD_SIZE 128U

typedef enum {
    PRODUCT_UI_POMODORO_FOCUS = 0,
    PRODUCT_UI_POMODORO_SHORT_BREAK,
    PRODUCT_UI_POMODORO_LONG_BREAK,
} product_ui_pomodoro_phase_e;

typedef enum {
    PRODUCT_UI_POMODORO_WAITING = 0,
    PRODUCT_UI_POMODORO_RUNNING,
    PRODUCT_UI_POMODORO_PAUSED,
} product_ui_pomodoro_state_e;

typedef enum {
    PRODUCT_UI_POMODORO_START = 0,
    PRODUCT_UI_POMODORO_PAUSE,
    PRODUCT_UI_POMODORO_RESUME,
    PRODUCT_UI_POMODORO_SKIP,
    PRODUCT_UI_POMODORO_END,
} product_ui_pomodoro_command_e;

typedef struct {
    uint8_t focus_minutes;
    uint8_t short_minutes;
    uint8_t long_minutes;
    uint8_t interval;
    uint8_t auto_focus;
    uint8_t auto_break;
    uint8_t sound;
} product_ui_pomodoro_settings_t;

typedef struct {
    uint32_t date; /* YYYYMMDD; zero is an empty slot. */
    uint32_t count;
    uint32_t minutes;
} product_ui_pomodoro_day_t;

typedef struct {
    product_ui_pomodoro_settings_t settings;
    product_ui_pomodoro_day_t days[PRODUCT_UI_POMODORO_DAYS];
    uint32_t undated_count;
    uint32_t undated_minutes;
} product_ui_pomodoro_saved_t;

typedef struct {
    product_ui_pomodoro_saved_t saved;
    product_ui_pomodoro_phase_e phase;
    product_ui_pomodoro_state_e state;
    product_ui_pomodoro_phase_e completed_phase;
    uint32_t generation;
    uint32_t revision;
    uint32_t saved_revision;
    uint32_t remaining_ms;
    uint32_t duration_ms;
    uint32_t sampled_ms;
    uint32_t event_id;
    uint32_t acknowledged_event;
    uint32_t event_ms;
    uint8_t completed_in_cycle;
    uint8_t cycle_interval;
    uint8_t cycle_active;
    uint8_t auto_next;
} product_ui_pomodoro_model_t;

/** Initialize a fresh boot; only validated completed data is restored. */
void product_ui_pomodoro_model_init(product_ui_pomodoro_model_t *model,
                                   const product_ui_pomodoro_saved_t *saved);
uint8_t product_ui_pomodoro_settings_valid(const product_ui_pomodoro_settings_t *settings);
uint8_t product_ui_pomodoro_date_valid(uint32_t date);
/** Advance at most one stage; date zero attributes a completion to undated. */
void product_ui_pomodoro_model_advance(product_ui_pomodoro_model_t *model,
                                      uint32_t now_ms, uint32_t date);
/** Caller serializes access; stale generations cannot affect a new button action. */
d_err_t product_ui_pomodoro_model_command(product_ui_pomodoro_model_t *model,
    product_ui_pomodoro_command_e command, uint32_t generation, uint32_t now_ms);
d_err_t product_ui_pomodoro_model_settings(product_ui_pomodoro_model_t *model,
    const product_ui_pomodoro_settings_t *settings);
void product_ui_pomodoro_model_clear_stats(product_ui_pomodoro_model_t *model);
/** Merge a delayed initial load into session-only data once; preserve current stage. */
void product_ui_pomodoro_model_recover(product_ui_pomodoro_model_t *model,
    const product_ui_pomodoro_saved_t *loaded, uint8_t keep_settings, uint8_t cleared_stats);
/** Fixed little-endian 128-byte wire format; CRC covers bytes 0..123. */
void product_ui_pomodoro_encode(const product_ui_pomodoro_saved_t *saved,
    uint32_t sequence, uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE]);
d_err_t product_ui_pomodoro_decode(const uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE],
    product_ui_pomodoro_saved_t *saved, uint32_t *sequence);
/** Modular ordering is valid for adjacent A/B generations, including wrap. */
uint8_t product_ui_pomodoro_sequence_after(uint32_t left, uint32_t right);
#endif
