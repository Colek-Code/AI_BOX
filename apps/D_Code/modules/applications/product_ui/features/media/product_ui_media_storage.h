/**
 * @file product_ui_media_storage.h
 * @brief Fixed-capacity media and SD storage state service contract.
 */

#ifndef __PRODUCT_UI_MEDIA_STORAGE_H__
#define __PRODUCT_UI_MEDIA_STORAGE_H__

#include "framework/d_error.h"
#include <stdint.h>

#define PRODUCT_UI_MEDIA_QUEUE_CAPACITY        64U
#define PRODUCT_UI_MEDIA_PAGE_CACHE_CAPACITY   32U
#define PRODUCT_UI_MEDIA_TITLE_MAX             64U
#define PRODUCT_UI_MEDIA_ARTIST_MAX            48U
#define PRODUCT_UI_MEDIA_PATH_MAX              128U
#define PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID   0xFFFFU

typedef enum {
    PRODUCT_UI_SD_CAPABILITY_UNKNOWN = 0,
    PRODUCT_UI_SD_CAPABILITY_UNAVAILABLE,
    PRODUCT_UI_SD_CAPABILITY_READY,
    PRODUCT_UI_SD_CAPABILITY_ERROR,
} product_ui_sd_capability_e;

typedef enum {
    PRODUCT_UI_SD_HOTPLUG_UNKNOWN = 0,
    PRODUCT_UI_SD_HOTPLUG_ABSENT,
    PRODUCT_UI_SD_HOTPLUG_PRESENT,
    PRODUCT_UI_SD_HOTPLUG_INSERTED,
    PRODUCT_UI_SD_HOTPLUG_REMOVED,
    PRODUCT_UI_SD_HOTPLUG_ERROR,
} product_ui_sd_hotplug_state_e;

typedef enum {
    PRODUCT_UI_MEDIA_COMMAND_NONE = 0,
    PRODUCT_UI_MEDIA_COMMAND_SCAN_START,
    PRODUCT_UI_MEDIA_COMMAND_SCAN_CANCEL,
    PRODUCT_UI_MEDIA_COMMAND_QUEUE_SELECT,
    PRODUCT_UI_MEDIA_COMMAND_QUEUE_NEXT,
    PRODUCT_UI_MEDIA_COMMAND_QUEUE_PREVIOUS,
    PRODUCT_UI_MEDIA_COMMAND_PAGE_REQUEST,
} product_ui_media_command_type_e;

typedef struct {
    uint32_t media_id;
    uint32_t duration_ms;
    uint32_t size_bytes;
    char title[PRODUCT_UI_MEDIA_TITLE_MAX];
    char artist[PRODUCT_UI_MEDIA_ARTIST_MAX];
    char path[PRODUCT_UI_MEDIA_PATH_MAX];
} product_ui_media_item_t;

typedef struct {
    uint16_t page_number;
    uint16_t first_queue_index;
    uint16_t item_count;
    uint16_t reserved;
    uint32_t source_version;
} product_ui_media_page_index_t;

typedef struct {
    product_ui_media_command_type_e type;
    uint32_t request_id;
    uint16_t argument;
    uint16_t reserved;
} product_ui_media_command_t;

typedef struct {
    uint32_t version;
    uint32_t queue_version;
    uint32_t page_cache_version;
    uint32_t hotplug_version;
    uint32_t command_version;
    product_ui_sd_capability_e sd_capability;
    product_ui_sd_hotplug_state_e hotplug_state;
    int32_t sd_error;
    uint16_t queue_count;
    uint16_t current_queue_index;
    uint8_t page_cache_count;
    uint8_t media_present;
    uint8_t command_pending;
    uint8_t reserved;
    product_ui_media_command_t pending_command;
    product_ui_media_item_t queue[PRODUCT_UI_MEDIA_QUEUE_CAPACITY];
    product_ui_media_page_index_t page_cache[
        PRODUCT_UI_MEDIA_PAGE_CACHE_CAPACITY];
} product_ui_media_storage_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t queue_version;
    uint32_t page_cache_version;
    uint32_t hotplug_version;
    uint32_t command_version;
    product_ui_sd_capability_e sd_capability;
    product_ui_sd_hotplug_state_e hotplug_state;
    int32_t sd_error;
    uint16_t queue_count;
    uint16_t current_queue_index;
    uint8_t page_cache_count;
    uint8_t media_present;
    uint8_t command_pending;
} product_ui_media_storage_summary_t;

/**
 * @brief Initialize the static service state.
 * @note No driver, scan, filesystem or persistence operation is performed.
 */
d_err_t product_ui_media_storage_init(void);

/**
 * @brief Return the service to its uninitialized static state.
 */
void product_ui_media_storage_deinit(void);

/**
 * @brief Reset snapshots while keeping the service initialized.
 */
d_err_t product_ui_media_storage_reset(void);

/**
 * @brief Copy the complete bounded service snapshot.
 * @note The service skeleton is single-context and is not ISR-safe.
 */
d_err_t product_ui_media_storage_snapshot_get(
    product_ui_media_storage_snapshot_t *snapshot);
d_err_t product_ui_media_storage_summary_get(
    product_ui_media_storage_summary_t *summary);
d_err_t product_ui_media_storage_queue_item_get(
    uint16_t index, product_ui_media_item_t *item);
d_err_t product_ui_media_storage_page_index_get(
    uint8_t index, product_ui_media_page_index_t *page_index);

/**
 * @brief Publish an SD capability result from a future storage adapter.
 * @param capability Explicit unknown/unavailable/ready/error state.
 * @param error_code Diagnostic code used only for the error state.
 */
d_err_t product_ui_media_storage_sd_publish(
    product_ui_sd_capability_e capability, int32_t error_code);

/**
 * @brief Publish a bounded SD hotplug state.
 * @note Removal clears the queue, page cache and pending command.
 */
d_err_t product_ui_media_storage_hotplug_publish(
    product_ui_sd_hotplug_state_e state);

/**
 * @brief Replace the complete static media queue.
 */
d_err_t product_ui_media_storage_queue_replace(
    const product_ui_media_item_t *items, uint16_t count,
    uint16_t current_index);

/**
 * @brief Clear the media queue and page cache.
 */
d_err_t product_ui_media_storage_queue_clear(void);

/**
 * @brief Replace the complete page-index cache.
 */
d_err_t product_ui_media_storage_page_cache_replace(
    const product_ui_media_page_index_t *entries, uint8_t count);

/**
 * @brief Submit one command for a future media/storage adapter.
 * @param argument Queue index for SELECT or page number for PAGE_REQUEST.
 * @param request_id Receives the generated non-zero request ID.
 */
d_err_t product_ui_media_storage_command_submit(
    product_ui_media_command_type_e type, uint16_t argument,
    uint32_t *request_id);

/**
 * @brief Take and remove the pending command.
 */
d_err_t product_ui_media_storage_command_take(
    product_ui_media_command_t *command);

#endif /* __PRODUCT_UI_MEDIA_STORAGE_H__ */
