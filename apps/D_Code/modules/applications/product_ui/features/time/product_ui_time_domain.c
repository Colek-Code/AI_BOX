/**
 * @file product_ui_time_domain.c
 * @brief Static implementation of the Product UI shared time domain.
 */

#include "product_ui_time_domain.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include "modules/applications/product_ui/shared/text/product_ui_text.h"
#include <string.h>

typedef struct {
    uint8_t used;
    product_ui_calendar_event_t value;
} product_ui_event_slot_t;
typedef struct {
    uint8_t initialized;
    uint8_t wall_time_valid;
    uint32_t generation;
    uint32_t next_event_id;
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
    product_ui_datetime_t wall_time;
    product_ui_event_slot_t events[PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS];
} product_ui_time_domain_state_t;
static product_ui_time_domain_state_t s_time_domain;
static const product_ui_time_alarm_backend_t *s_alarm_backend;
static uint32_t s_alarm_generation;

void product_ui_time_domain_bind_alarms(const product_ui_time_alarm_backend_t *backend)
{
    s_alarm_backend = backend;
    s_alarm_generation = 0U;
}

static void product_ui_time_domain_clear(uint32_t generation)
{
    memset(&s_time_domain, 0, sizeof(s_time_domain));
    s_time_domain.initialized = 1U;
    s_time_domain.generation = generation;
    s_time_domain.next_event_id = 1U;
    s_time_domain.capability = PRODUCT_UI_TIME_CAPABILITY_UNKNOWN;
    s_time_domain.unavailable_reason = PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED;
}

void product_ui_time_domain_init(void)
{
    product_ui_time_domain_clear(1U);
}

static void product_ui_time_domain_ensure_initialized(void)
{
    if (!s_time_domain.initialized) {
        product_ui_time_domain_init();
    }
}

static void product_ui_time_domain_mark_changed(void)
{
    s_time_domain.generation++;
    if (s_time_domain.generation == 0U) {
        s_time_domain.generation = 1U;
    }
}

static int product_ui_time_find_event(uint32_t id)
{
    uint32_t index;
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (s_time_domain.events[index].used &&
            (s_time_domain.events[index].value.id == id)) {
            return (int)index;
        }
    }
    return -1;
}

static int product_ui_time_find_free_event(void)
{
    uint32_t index;
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (!s_time_domain.events[index].used) {
            return (int)index;
        }
    }
    return -1;
}

static uint32_t product_ui_time_allocate_event_id(void)
{
    uint32_t attempt;
    uint32_t candidate;
    for (attempt = 0U; attempt <= PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; attempt++) {
        candidate = s_time_domain.next_event_id++;
        if (s_time_domain.next_event_id == 0U) {
            s_time_domain.next_event_id = 1U;
        }
        if ((candidate != 0U) && (product_ui_time_find_event(candidate) < 0)) {
            return candidate;
        }
    }
    return 0U;
}

static product_ui_time_result_e product_ui_time_upsert_event(
    const product_ui_calendar_event_t *input, uint32_t *affected_id)
{
    product_ui_calendar_event_t value;
    int index;
    if (product_ui_time_domain_validate_event(input) != PRODUCT_UI_TIME_OK) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    value = *input;
    if (value.id == 0U) {
        value.id = product_ui_time_allocate_event_id();
        if (value.id == 0U) {
            return PRODUCT_UI_TIME_ERR_CAPACITY;
        }
    }
    index = product_ui_time_find_event(value.id);
    if (index < 0) {
        index = product_ui_time_find_free_event();
        if (index < 0) {
            return PRODUCT_UI_TIME_ERR_CAPACITY;
        }
    }
    product_ui_utf8_copy_n(
        value.title, sizeof(value.title), input->title, sizeof(input->title));
    product_ui_utf8_copy_n(value.location, sizeof(value.location),
                           input->location, sizeof(input->location));
    s_time_domain.events[index].used = 1U;
    s_time_domain.events[index].value = value;
    *affected_id = value.id;
    return PRODUCT_UI_TIME_OK;
}

static product_ui_time_result_e product_ui_time_set_capability(
    const product_ui_time_capability_command_t *command)
{
    if (command->capability == PRODUCT_UI_TIME_CAPABILITY_AVAILABLE) {
        if (command->unavailable_reason != PRODUCT_UI_TIME_UNAVAILABLE_NONE) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
    } else if (command->capability == PRODUCT_UI_TIME_CAPABILITY_UNKNOWN) {
        if ((command->unavailable_reason != PRODUCT_UI_TIME_UNAVAILABLE_NONE) &&
            (command->unavailable_reason != PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED)) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
    } else if (command->capability == PRODUCT_UI_TIME_CAPABILITY_UNAVAILABLE) {
        if ((command->unavailable_reason == PRODUCT_UI_TIME_UNAVAILABLE_NONE) ||
            (command->unavailable_reason == PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED) ||
            (command->unavailable_reason > PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID)) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
    } else {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }

    s_time_domain.capability = command->capability;
    s_time_domain.unavailable_reason = command->unavailable_reason;
    if (command->capability == PRODUCT_UI_TIME_CAPABILITY_UNKNOWN) {
        s_time_domain.unavailable_reason = PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED;
    }
    if (command->capability != PRODUCT_UI_TIME_CAPABILITY_AVAILABLE) {
        s_time_domain.wall_time_valid = 0U;
        memset(&s_time_domain.wall_time, 0, sizeof(s_time_domain.wall_time));
    }
    return PRODUCT_UI_TIME_OK;
}

static product_ui_time_result_e product_ui_time_finish_command(
    product_ui_time_domain_command_result_t *result,
    product_ui_time_result_e status,
    uint32_t affected_id)
{
    if (result != NULL) {
        result->result = status;
        result->generation = s_time_domain.generation;
        result->affected_id = affected_id;
    }
    return status;
}

static product_ui_time_result_e product_ui_time_execute_system(
    const product_ui_time_domain_command_t *command, uint8_t *handled)
{
    product_ui_time_result_e status;
    uint32_t generation;

    *handled = 1U;
    switch (command->type) {
    case PRODUCT_UI_TIME_COMMAND_RESET:
        generation = s_time_domain.generation + 1U;
        product_ui_time_domain_clear(generation == 0U ? 1U : generation);
        return PRODUCT_UI_TIME_OK;
    case PRODUCT_UI_TIME_COMMAND_SET_CAPABILITY:
        return product_ui_time_set_capability(&command->data.capability);
    case PRODUCT_UI_TIME_COMMAND_SET_WALL_TIME:
        if (s_time_domain.capability != PRODUCT_UI_TIME_CAPABILITY_AVAILABLE) {
            return PRODUCT_UI_TIME_ERR_UNAVAILABLE;
        }
        status = product_ui_time_domain_validate_datetime(&command->data.wall_time);
        if (status == PRODUCT_UI_TIME_OK) {
            s_time_domain.wall_time = command->data.wall_time;
            s_time_domain.wall_time_valid = 1U;
        }
        return status;
    case PRODUCT_UI_TIME_COMMAND_INVALIDATE_WALL_TIME:
        s_time_domain.wall_time_valid = 0U;
        memset(&s_time_domain.wall_time, 0, sizeof(s_time_domain.wall_time));
        return PRODUCT_UI_TIME_OK;
    default:
        *handled = 0U;
        return PRODUCT_UI_TIME_OK;
    }
}

static product_ui_time_result_e product_ui_time_execute_alarm(
    const product_ui_time_domain_command_t *command, uint32_t *affected_id,
    uint8_t *handled)
{
    *handled = command->type == PRODUCT_UI_TIME_COMMAND_UPSERT_ALARM ||
        command->type == PRODUCT_UI_TIME_COMMAND_REMOVE_ALARM ||
        command->type == PRODUCT_UI_TIME_COMMAND_SET_ALARM_ENABLED;
    if (!*handled) return PRODUCT_UI_TIME_OK;
    return s_alarm_backend != NULL && s_alarm_backend->execute != NULL ?
        s_alarm_backend->execute(command, affected_id) : PRODUCT_UI_TIME_ERR_UNAVAILABLE;
}

static product_ui_time_result_e product_ui_time_execute_event(
    const product_ui_time_domain_command_t *command, uint32_t *affected_id,
    uint8_t *handled)
{
    int index;

    *handled = 1U;
    if (command->type == PRODUCT_UI_TIME_COMMAND_UPSERT_EVENT) {
        return product_ui_time_upsert_event(&command->data.event, affected_id);
    }
    if ((command->type != PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT) &&
        (command->type != PRODUCT_UI_TIME_COMMAND_SET_EVENT_COMPLETED)) {
        *handled = 0U;
        return PRODUCT_UI_TIME_OK;
    }
    *affected_id = command->type == PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT ?
                   command->data.id.id : command->data.event_completed.id;
    index = product_ui_time_find_event(*affected_id);
    if ((*affected_id == 0U) || (index < 0)) {
        return PRODUCT_UI_TIME_ERR_NOT_FOUND;
    }
    if (command->type == PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT) {
        memset(&s_time_domain.events[index], 0, sizeof(s_time_domain.events[index]));
        return PRODUCT_UI_TIME_OK;
    }
    if (command->type == PRODUCT_UI_TIME_COMMAND_SET_EVENT_COMPLETED) {
        if (command->data.event_completed.completed > 1U) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
        s_time_domain.events[index].value.completed =
            command->data.event_completed.completed;
        return PRODUCT_UI_TIME_OK;
    }
    *handled = 0U;
    return PRODUCT_UI_TIME_OK;
}

product_ui_time_result_e product_ui_time_domain_execute(
    const product_ui_time_domain_command_t *command,
    product_ui_time_domain_command_result_t *result)
{
    product_ui_time_result_e status = PRODUCT_UI_TIME_OK;
    uint32_t affected_id = 0U;
    uint8_t handled;
    product_ui_time_domain_ensure_initialized();
    if (command == NULL) {
        return product_ui_time_finish_command(
            result, PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT, 0U);
    }

    status = product_ui_time_execute_system(command, &handled);
    if (!handled) {
        status = product_ui_time_execute_alarm(command, &affected_id, &handled);
    }
    if (!handled) {
        status = product_ui_time_execute_event(command, &affected_id, &handled);
    }
    if (!handled) {
        status = PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }

    if (status == PRODUCT_UI_TIME_OK) {
        if (command->type == PRODUCT_UI_TIME_COMMAND_RESET) {
            affected_id = 0U;
        } else {
            product_ui_time_domain_mark_changed();
        }
    }
    return product_ui_time_finish_command(result, status, affected_id);
}

product_ui_time_result_e product_ui_time_domain_get_summary(
    product_ui_time_domain_summary_t *summary)
{
    uint32_t index;
    product_ui_time_alarm_summary_t alarms = {0};

    if (summary == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    memset(summary, 0, sizeof(*summary));
    summary->generation = s_time_domain.generation;
    summary->capability = s_time_domain.capability;
    summary->unavailable_reason = s_time_domain.unavailable_reason;
    summary->wall_time_valid = s_time_domain.wall_time_valid;
    summary->wall_time = s_time_domain.wall_time;
    if (s_alarm_backend != NULL && s_alarm_backend->copy != NULL &&
        s_alarm_backend->copy(NULL, &alarms) == PRODUCT_UI_TIME_OK) {
        summary->alarm_runtime_available = alarms.runtime_available && summary->wall_time_valid &&
            summary->capability == PRODUCT_UI_TIME_CAPABILITY_AVAILABLE;
        summary->alarm_count = alarms.count;
        if (summary->alarm_runtime_available) {
            summary->next_alarm_valid = alarms.next_valid;
            summary->next_alarm_id = alarms.next_id;
            summary->next_alarm_at = alarms.next_at;
        }
        if (s_alarm_generation != alarms.generation) {
            s_alarm_generation = alarms.generation;
            product_ui_time_domain_mark_changed();
            summary->generation = s_time_domain.generation;
        }
    }
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        summary->event_count += s_time_domain.events[index].used ? 1U : 0U;
    }
    return PRODUCT_UI_TIME_OK;
}

product_ui_time_result_e product_ui_time_domain_get_alarm(
    uint8_t logical_index, product_ui_alarm_t *alarm)
{
    if (alarm == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    return s_alarm_backend != NULL && s_alarm_backend->get != NULL ?
        s_alarm_backend->get(logical_index, alarm) : PRODUCT_UI_TIME_ERR_UNAVAILABLE;
}

product_ui_time_result_e product_ui_time_domain_get_event(
    uint8_t logical_index, product_ui_calendar_event_t *event)
{
    uint8_t current = 0U;
    uint32_t index;

    if (event == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (!s_time_domain.events[index].used) {
            continue;
        }
        if (current == logical_index) {
            *event = s_time_domain.events[index].value;
            return PRODUCT_UI_TIME_OK;
        }
        current++;
    }
    return PRODUCT_UI_TIME_ERR_NOT_FOUND;
}

product_ui_time_result_e product_ui_time_domain_get_snapshot(
    product_ui_time_domain_snapshot_t *snapshot)
{
    product_ui_time_domain_summary_t summary;
    uint32_t index;
    if (snapshot == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    memset(snapshot, 0, sizeof(*snapshot));
    product_ui_time_domain_get_summary(&summary);
    snapshot->generation = summary.generation;
    snapshot->capability = summary.capability;
    snapshot->unavailable_reason = summary.unavailable_reason;
    snapshot->wall_time_valid = summary.wall_time_valid;
    snapshot->alarm_runtime_available = summary.alarm_runtime_available;
    snapshot->wall_time = summary.wall_time;
    snapshot->next_alarm_valid = summary.next_alarm_valid;
    snapshot->next_alarm_id = summary.next_alarm_id;
    snapshot->next_alarm_at = summary.next_alarm_at;

    if (s_alarm_backend != NULL && s_alarm_backend->copy != NULL) {
        product_ui_time_alarm_summary_t alarms = {0};
        if (s_alarm_backend->copy(snapshot->alarms, &alarms) == PRODUCT_UI_TIME_OK) {
            snapshot->alarm_count = alarms.count;
            snapshot->alarm_runtime_available = alarms.runtime_available && snapshot->wall_time_valid &&
                snapshot->capability == PRODUCT_UI_TIME_CAPABILITY_AVAILABLE;
            if (snapshot->alarm_runtime_available) {
                snapshot->next_alarm_valid = alarms.next_valid;
                snapshot->next_alarm_id = alarms.next_id;
                snapshot->next_alarm_at = alarms.next_at;
            }
        }
    }
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (s_time_domain.events[index].used) {
            snapshot->events[snapshot->event_count++] =
                s_time_domain.events[index].value;
        }
    }
    return PRODUCT_UI_TIME_OK;
}
