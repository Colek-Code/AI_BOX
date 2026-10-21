/**
 * @file product_ui_media_storage.c
 * @brief Static media and SD storage state service skeleton.
 */

#include "product_ui_media_storage.h"
#include "modules/applications/product_ui/shared/text/product_ui_text.h"
#include <string.h>

static product_ui_media_storage_snapshot_t s_snapshot;
static uint32_t s_next_request_id;
static uint8_t s_initialized;

static void product_ui_media_storage_version_increment(uint32_t *version)
{
    (*version)++;
    if (*version == 0U) {
        *version = 1U;
    }
}

static void product_ui_media_storage_snapshot_changed(void)
{
    product_ui_media_storage_version_increment(&s_snapshot.version);
}

static void product_ui_media_storage_queue_clear_internal(void)
{
    memset(s_snapshot.queue, 0, sizeof(s_snapshot.queue));
    s_snapshot.queue_count = 0U;
    s_snapshot.current_queue_index = PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID;
    product_ui_media_storage_version_increment(&s_snapshot.queue_version);
}

static void product_ui_media_storage_page_cache_clear_internal(void)
{
    memset(s_snapshot.page_cache, 0, sizeof(s_snapshot.page_cache));
    s_snapshot.page_cache_count = 0U;
    product_ui_media_storage_version_increment(&s_snapshot.page_cache_version);
}

static void product_ui_media_storage_command_clear_internal(void)
{
    memset(&s_snapshot.pending_command, 0,
           sizeof(s_snapshot.pending_command));
    s_snapshot.command_pending = 0U;
    product_ui_media_storage_version_increment(&s_snapshot.command_version);
}

static void product_ui_media_storage_state_initialize(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.version = 1U;
    s_snapshot.queue_version = 1U;
    s_snapshot.page_cache_version = 1U;
    s_snapshot.hotplug_version = 1U;
    s_snapshot.command_version = 1U;
    s_snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNKNOWN;
    s_snapshot.hotplug_state = PRODUCT_UI_SD_HOTPLUG_UNKNOWN;
    s_snapshot.current_queue_index = PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID;
    s_next_request_id = 1U;
}

static void product_ui_media_storage_item_copy(
    product_ui_media_item_t *output, const product_ui_media_item_t *input)
{
    *output = *input;
    product_ui_utf8_copy_n(output->title, sizeof(output->title),
                           input->title, sizeof(input->title));
    product_ui_utf8_copy_n(output->artist, sizeof(output->artist),
                           input->artist, sizeof(input->artist));
    product_ui_utf8_copy_n(output->path, sizeof(output->path),
                           input->path, sizeof(input->path));
}

static uint8_t product_ui_media_storage_sd_state_valid(
    product_ui_sd_capability_e capability)
{
    return capability >= PRODUCT_UI_SD_CAPABILITY_UNKNOWN &&
           capability <= PRODUCT_UI_SD_CAPABILITY_ERROR;
}

static uint8_t product_ui_media_storage_hotplug_state_valid(
    product_ui_sd_hotplug_state_e state)
{
    return state >= PRODUCT_UI_SD_HOTPLUG_UNKNOWN &&
           state <= PRODUCT_UI_SD_HOTPLUG_ERROR;
}

static d_err_t product_ui_media_storage_command_validate(
    product_ui_media_command_type_e type, uint16_t argument)
{
    if ((type <= PRODUCT_UI_MEDIA_COMMAND_NONE) ||
        (type > PRODUCT_UI_MEDIA_COMMAND_PAGE_REQUEST)) {
        return D_ERR_INVALID_ARG;
    }
    if (((type == PRODUCT_UI_MEDIA_COMMAND_SCAN_START) ||
         (type == PRODUCT_UI_MEDIA_COMMAND_PAGE_REQUEST)) &&
        (s_snapshot.sd_capability != PRODUCT_UI_SD_CAPABILITY_READY)) {
        return D_ERR_NOT_READY;
    }
    if ((type == PRODUCT_UI_MEDIA_COMMAND_QUEUE_SELECT) &&
        (argument >= s_snapshot.queue_count)) {
        return D_ERR_INVALID_ARG;
    }
    if (((type == PRODUCT_UI_MEDIA_COMMAND_QUEUE_NEXT) ||
         (type == PRODUCT_UI_MEDIA_COMMAND_QUEUE_PREVIOUS)) &&
        (s_snapshot.queue_count == 0U)) {
        return D_ERR_NOT_READY;
    }
    return D_OK;
}

d_err_t product_ui_media_storage_init(void)
{
    product_ui_media_storage_state_initialize();
    s_initialized = 1U;
    return D_OK;
}

void product_ui_media_storage_deinit(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_next_request_id = 0U;
    s_initialized = 0U;
}

d_err_t product_ui_media_storage_reset(void)
{
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    product_ui_media_storage_state_initialize();
    return D_OK;
}

d_err_t product_ui_media_storage_snapshot_get(
    product_ui_media_storage_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    *snapshot = s_snapshot;
    return D_OK;
}

d_err_t product_ui_media_storage_summary_get(
    product_ui_media_storage_summary_t *summary)
{
    if (summary == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    memset(summary, 0, sizeof(*summary));
    summary->version = s_snapshot.version;
    summary->queue_version = s_snapshot.queue_version;
    summary->page_cache_version = s_snapshot.page_cache_version;
    summary->hotplug_version = s_snapshot.hotplug_version;
    summary->command_version = s_snapshot.command_version;
    summary->sd_capability = s_snapshot.sd_capability;
    summary->hotplug_state = s_snapshot.hotplug_state;
    summary->sd_error = s_snapshot.sd_error;
    summary->queue_count = s_snapshot.queue_count;
    summary->current_queue_index = s_snapshot.current_queue_index;
    summary->page_cache_count = s_snapshot.page_cache_count;
    summary->media_present = s_snapshot.media_present;
    summary->command_pending = s_snapshot.command_pending;
    return D_OK;
}

d_err_t product_ui_media_storage_queue_item_get(
    uint16_t index, product_ui_media_item_t *item)
{
    if (item == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if (index >= s_snapshot.queue_count) {
        return D_ERR_INVALID_ARG;
    }
    *item = s_snapshot.queue[index];
    return D_OK;
}

d_err_t product_ui_media_storage_page_index_get(
    uint8_t index, product_ui_media_page_index_t *page_index)
{
    if (page_index == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if (index >= s_snapshot.page_cache_count) {
        return D_ERR_INVALID_ARG;
    }
    *page_index = s_snapshot.page_cache[index];
    return D_OK;
}

d_err_t product_ui_media_storage_sd_publish(
    product_ui_sd_capability_e capability, int32_t error_code)
{
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if (!product_ui_media_storage_sd_state_valid(capability)) {
        return D_ERR_INVALID_ARG;
    }
    if ((capability == PRODUCT_UI_SD_CAPABILITY_READY) &&
        !s_snapshot.media_present) {
        return D_ERR_NOT_READY;
    }
    if ((capability == PRODUCT_UI_SD_CAPABILITY_ERROR) &&
        (error_code == 0)) {
        return D_ERR_INVALID_ARG;
    }

    s_snapshot.sd_capability = capability;
    s_snapshot.sd_error =
        (capability == PRODUCT_UI_SD_CAPABILITY_ERROR) ? error_code : 0;
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

d_err_t product_ui_media_storage_hotplug_publish(
    product_ui_sd_hotplug_state_e state)
{
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if (!product_ui_media_storage_hotplug_state_valid(state)) {
        return D_ERR_INVALID_ARG;
    }

    s_snapshot.hotplug_state = state;
    switch (state) {
    case PRODUCT_UI_SD_HOTPLUG_PRESENT:
    case PRODUCT_UI_SD_HOTPLUG_INSERTED:
        s_snapshot.media_present = 1U;
        s_snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNKNOWN;
        s_snapshot.sd_error = 0;
        break;
    case PRODUCT_UI_SD_HOTPLUG_ABSENT:
    case PRODUCT_UI_SD_HOTPLUG_REMOVED:
        s_snapshot.media_present = 0U;
        s_snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNAVAILABLE;
        s_snapshot.sd_error = 0;
        product_ui_media_storage_queue_clear_internal();
        product_ui_media_storage_page_cache_clear_internal();
        if (s_snapshot.command_pending) {
            product_ui_media_storage_command_clear_internal();
        }
        break;
    case PRODUCT_UI_SD_HOTPLUG_ERROR:
        s_snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_ERROR;
        s_snapshot.sd_error = D_ERR_IO;
        break;
    case PRODUCT_UI_SD_HOTPLUG_UNKNOWN:
    default:
        s_snapshot.media_present = 0U;
        s_snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNKNOWN;
        s_snapshot.sd_error = 0;
        break;
    }
    product_ui_media_storage_version_increment(&s_snapshot.hotplug_version);
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

d_err_t product_ui_media_storage_queue_replace(
    const product_ui_media_item_t *items, uint16_t count,
    uint16_t current_index)
{
    uint16_t index;

    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if ((count > PRODUCT_UI_MEDIA_QUEUE_CAPACITY) ||
        ((count > 0U) && (items == NULL))) {
        return D_ERR_INVALID_ARG;
    }
    if (((count == 0U) &&
         (current_index != PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID)) ||
        ((count > 0U) && (current_index >= count))) {
        return D_ERR_INVALID_ARG;
    }

    memset(s_snapshot.queue, 0, sizeof(s_snapshot.queue));
    if (count > 0U) {
        for (index = 0U; index < count; index++) {
            product_ui_media_storage_item_copy(&s_snapshot.queue[index],
                                               &items[index]);
        }
    }
    s_snapshot.queue_count = count;
    s_snapshot.current_queue_index = current_index;
    product_ui_media_storage_version_increment(&s_snapshot.queue_version);
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

d_err_t product_ui_media_storage_queue_clear(void)
{
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    product_ui_media_storage_queue_clear_internal();
    product_ui_media_storage_page_cache_clear_internal();
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

d_err_t product_ui_media_storage_page_cache_replace(
    const product_ui_media_page_index_t *entries, uint8_t count)
{
    uint8_t index;
    uint32_t range_end;

    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if ((count > PRODUCT_UI_MEDIA_PAGE_CACHE_CAPACITY) ||
        ((count > 0U) && (entries == NULL))) {
        return D_ERR_INVALID_ARG;
    }
    for (index = 0U; index < count; index++) {
        range_end = (uint32_t)entries[index].first_queue_index +
                    entries[index].item_count;
        if (range_end > s_snapshot.queue_count) {
            return D_ERR_INVALID_ARG;
        }
    }

    memset(s_snapshot.page_cache, 0, sizeof(s_snapshot.page_cache));
    if (count > 0U) {
        memcpy(s_snapshot.page_cache, entries,
               (size_t)count * sizeof(s_snapshot.page_cache[0]));
    }
    s_snapshot.page_cache_count = count;
    product_ui_media_storage_version_increment(
        &s_snapshot.page_cache_version);
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

d_err_t product_ui_media_storage_command_submit(
    product_ui_media_command_type_e type, uint16_t argument,
    uint32_t *request_id)
{
    d_err_t result;

    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if (request_id == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (s_snapshot.command_pending) {
        return D_ERR_BUSY;
    }
    result = product_ui_media_storage_command_validate(type, argument);
    if (result != D_OK) {
        return result;
    }

    s_snapshot.pending_command.type = type;
    s_snapshot.pending_command.argument = argument;
    s_snapshot.pending_command.request_id = s_next_request_id;
    s_snapshot.command_pending = 1U;
    *request_id = s_next_request_id;
    product_ui_media_storage_version_increment(&s_next_request_id);
    product_ui_media_storage_version_increment(&s_snapshot.command_version);
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

d_err_t product_ui_media_storage_command_take(
    product_ui_media_command_t *command)
{
    if (command == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return D_ERR_NOT_READY;
    }
    if (!s_snapshot.command_pending) {
        return D_ERR_NOT_FOUND;
    }

    *command = s_snapshot.pending_command;
    product_ui_media_storage_command_clear_internal();
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}
