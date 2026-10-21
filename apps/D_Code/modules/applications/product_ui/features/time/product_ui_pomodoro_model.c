/** @file product_ui_pomodoro_model.c @brief Pure monotonic timing and completed-focus accounting. */
#include "product_ui_pomodoro_model.h"
#include <string.h>

static uint32_t product_ui_pomodoro_next(uint32_t value)
{
    value++;
    return value == 0U ? 1U : value;
}

static uint32_t product_ui_pomodoro_add(uint32_t value, uint32_t increment)
{
    return UINT32_MAX - value < increment ? UINT32_MAX : value + increment;
}

uint8_t product_ui_pomodoro_settings_valid(const product_ui_pomodoro_settings_t *settings)
{
    return settings != NULL && settings->focus_minutes >= 1U && settings->focus_minutes <= 120U &&
        settings->short_minutes >= 1U && settings->short_minutes <= 30U &&
        settings->long_minutes >= 1U && settings->long_minutes <= 60U &&
        settings->interval >= 1U && settings->interval <= 8U &&
        settings->auto_focus <= 1U && settings->auto_break <= 1U && settings->sound <= 1U;
}

uint8_t product_ui_pomodoro_date_valid(uint32_t date)
{
    static const uint8_t lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t year = date / 10000U;
    uint32_t month = (date / 100U) % 100U;
    uint32_t day = date % 100U;
    uint8_t length;
    if (year < 1900U || year > 9999U || month < 1U || month > 12U) return 0U;
    length = lengths[month - 1U];
    if (month == 2U && year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U)) length++;
    return day >= 1U && day <= length;
}

static uint32_t product_ui_pomodoro_duration(const product_ui_pomodoro_model_t *model)
{
    uint8_t minutes = model->phase == PRODUCT_UI_POMODORO_FOCUS ? model->saved.settings.focus_minutes :
        model->phase == PRODUCT_UI_POMODORO_SHORT_BREAK ? model->saved.settings.short_minutes :
        model->saved.settings.long_minutes;
    return (uint32_t)minutes * 60000U;
}

static void product_ui_pomodoro_arm(product_ui_pomodoro_model_t *model, uint32_t now_ms, uint8_t run)
{
    model->duration_ms = product_ui_pomodoro_duration(model);
    model->remaining_ms = model->duration_ms;
    model->sampled_ms = now_ms;
    model->auto_next = model->phase == PRODUCT_UI_POMODORO_FOCUS ?
        model->saved.settings.auto_break : model->saved.settings.auto_focus;
    model->state = run ? PRODUCT_UI_POMODORO_RUNNING : PRODUCT_UI_POMODORO_WAITING;
    model->generation = product_ui_pomodoro_next(model->generation);
    model->revision = product_ui_pomodoro_next(model->revision);
}

void product_ui_pomodoro_model_init(product_ui_pomodoro_model_t *model,
                                   const product_ui_pomodoro_saved_t *saved)
{
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    model->saved.settings = (product_ui_pomodoro_settings_t){25, 5, 15, 4, 1, 1, 1};
    if (saved != NULL && product_ui_pomodoro_settings_valid(&saved->settings)) model->saved = *saved;
    model->cycle_interval = model->saved.settings.interval;
    model->saved_revision = 1U;
    product_ui_pomodoro_arm(model, 0U, 0U);
}

static void product_ui_pomodoro_account(product_ui_pomodoro_saved_t *saved,
    uint32_t date, uint32_t count, uint32_t minutes)
{
    product_ui_pomodoro_day_t *day = NULL;
    unsigned index;
    unsigned oldest = 0U;
    if (product_ui_pomodoro_date_valid(date)) {
        for (index = 0U; index < PRODUCT_UI_POMODORO_DAYS; index++) {
            if (saved->days[index].date == date) { day = &saved->days[index]; break; }
            if (saved->days[index].date < saved->days[oldest].date) oldest = index;
        }
        if (day == NULL && date > saved->days[oldest].date) {
            day = &saved->days[oldest];
            *day = (product_ui_pomodoro_day_t){date, 0U, 0U};
        }
    }
    if (day != NULL) {
        day->count = product_ui_pomodoro_add(day->count, count);
        day->minutes = product_ui_pomodoro_add(day->minutes, minutes);
    } else {
        saved->undated_count = product_ui_pomodoro_add(saved->undated_count, count);
        saved->undated_minutes = product_ui_pomodoro_add(saved->undated_minutes, minutes);
    }
}

static void product_ui_pomodoro_count(product_ui_pomodoro_model_t *model, uint32_t date)
{
    product_ui_pomodoro_account(&model->saved, date, 1U, model->duration_ms / 60000U);
    model->saved_revision = product_ui_pomodoro_next(model->saved_revision);
}

void product_ui_pomodoro_model_recover(product_ui_pomodoro_model_t *model,
    const product_ui_pomodoro_saved_t *loaded, uint8_t keep_settings, uint8_t cleared_stats)
{
    product_ui_pomodoro_saved_t session;
    unsigned index;
    if (model == NULL || loaded == NULL) return;
    session = model->saved;
    if (!cleared_stats) {
        model->saved = *loaded;
        for (index = 0U; index < PRODUCT_UI_POMODORO_DAYS; index++) {
            const product_ui_pomodoro_day_t *day = &session.days[index];
            if (day->count != 0U)
                product_ui_pomodoro_account(&model->saved, day->date, day->count, day->minutes);
        }
        product_ui_pomodoro_account(&model->saved, 0U, session.undated_count, session.undated_minutes);
    }
    /* The running stage keeps its frozen duration/auto policy. An explicit
     * session settings save or clear wins over the recovered older record. */
    (void)product_ui_pomodoro_model_settings(model,
        keep_settings ? &session.settings : &loaded->settings);
}

static void product_ui_pomodoro_transition(product_ui_pomodoro_model_t *model,
                                          uint32_t now_ms, uint8_t skipped)
{
    uint8_t run = !skipped && model->auto_next;
    if (model->phase == PRODUCT_UI_POMODORO_FOCUS) {
        model->phase = !skipped && model->completed_in_cycle >= model->cycle_interval ?
            PRODUCT_UI_POMODORO_LONG_BREAK : PRODUCT_UI_POMODORO_SHORT_BREAK;
    } else {
        if (model->phase == PRODUCT_UI_POMODORO_LONG_BREAK) {
            model->completed_in_cycle = 0U;
            model->cycle_active = 0U;
        }
        model->phase = PRODUCT_UI_POMODORO_FOCUS;
    }
    if (run && !model->cycle_active) {
        model->cycle_interval = model->saved.settings.interval;
        model->cycle_active = 1U;
    }
    product_ui_pomodoro_arm(model, now_ms, run);
}

void product_ui_pomodoro_model_advance(product_ui_pomodoro_model_t *model,
                                      uint32_t now_ms, uint32_t date)
{
    uint32_t elapsed;
    if (model == NULL || model->state != PRODUCT_UI_POMODORO_RUNNING) return;
    elapsed = now_ms - model->sampled_ms;
    model->sampled_ms = now_ms;
    if (elapsed < model->remaining_ms) {
        model->remaining_ms -= elapsed;
        return;
    }
    model->completed_phase = model->phase;
    model->event_id = product_ui_pomodoro_next(model->event_id);
    model->event_ms = now_ms;
    if (model->phase == PRODUCT_UI_POMODORO_FOCUS) {
        product_ui_pomodoro_count(model, date);
        model->completed_in_cycle++;
    }
    product_ui_pomodoro_transition(model, now_ms, 0U);
}

d_err_t product_ui_pomodoro_model_command(product_ui_pomodoro_model_t *model,
    product_ui_pomodoro_command_e command, uint32_t generation, uint32_t now_ms)
{
    if (model == NULL || command > PRODUCT_UI_POMODORO_END || command < PRODUCT_UI_POMODORO_START)
        return D_ERR_INVALID_ARG;
    if (generation != model->generation) return D_ERR_BUSY;
    switch (command) {
    case PRODUCT_UI_POMODORO_START:
        if (model->state != PRODUCT_UI_POMODORO_WAITING) return D_ERR_BUSY;
        if (!model->cycle_active) {
            model->cycle_interval = model->saved.settings.interval;
            model->cycle_active = 1U;
        }
        product_ui_pomodoro_arm(model, now_ms, 1U);
        break;
    case PRODUCT_UI_POMODORO_PAUSE:
        if (model->state != PRODUCT_UI_POMODORO_RUNNING) return D_ERR_BUSY;
        model->state = PRODUCT_UI_POMODORO_PAUSED;
        break;
    case PRODUCT_UI_POMODORO_RESUME:
        if (model->state != PRODUCT_UI_POMODORO_PAUSED) return D_ERR_BUSY;
        model->sampled_ms = now_ms;
        model->state = PRODUCT_UI_POMODORO_RUNNING;
        break;
    case PRODUCT_UI_POMODORO_SKIP:
        model->acknowledged_event = model->event_id;
        product_ui_pomodoro_transition(model, now_ms, 1U);
        break;
    case PRODUCT_UI_POMODORO_END:
        model->acknowledged_event = model->event_id;
        model->phase = PRODUCT_UI_POMODORO_FOCUS;
        model->completed_in_cycle = 0U;
        model->cycle_active = 0U;
        model->cycle_interval = model->saved.settings.interval;
        product_ui_pomodoro_arm(model, now_ms, 0U);
        break;
    default:
        return D_ERR_INVALID_ARG;
    }
    model->generation = product_ui_pomodoro_next(model->generation);
    model->revision = product_ui_pomodoro_next(model->revision);
    return D_OK;
}

d_err_t product_ui_pomodoro_model_settings(product_ui_pomodoro_model_t *model,
    const product_ui_pomodoro_settings_t *settings)
{
    if (model == NULL || !product_ui_pomodoro_settings_valid(settings)) return D_ERR_INVALID_ARG;
    model->saved.settings = *settings;
    model->saved_revision = product_ui_pomodoro_next(model->saved_revision);
    if (model->state == PRODUCT_UI_POMODORO_WAITING) product_ui_pomodoro_arm(model, 0U, 0U);
    if (!model->cycle_active) model->cycle_interval = settings->interval;
    model->revision = product_ui_pomodoro_next(model->revision);
    return D_OK;
}

void product_ui_pomodoro_model_clear_stats(product_ui_pomodoro_model_t *model)
{
    if (model == NULL) return;
    memset(model->saved.days, 0, sizeof(model->saved.days));
    model->saved.undated_count = 0U;
    model->saved.undated_minutes = 0U;
    model->saved_revision = product_ui_pomodoro_next(model->saved_revision);
    model->revision = product_ui_pomodoro_next(model->revision);
}
