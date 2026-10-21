/**
 * @file product_ui_connection_read_policy.h
 * @brief Pure CONNECTION status-read decisions (host-testable, no wifi headers).
 */

#ifndef __PRODUCT_UI_CONNECTION_READ_POLICY_H__
#define __PRODUCT_UI_CONNECTION_READ_POLICY_H__

#include <stdint.h>

typedef enum {
    /* Wi-Fi off / backend not started → publish UNAVAILABLE. */
    PRODUCT_UI_CONN_SYNC_UNAVAILABLE = 0,
    /* Valid snapshot exists and status_version unchanged → skip get_status. */
    PRODUCT_UI_CONN_SYNC_SKIP_READ = 1,
    /* Need up to two bounded get_status samples. */
    PRODUCT_UI_CONN_SYNC_FULL_READ = 2,
} product_ui_connection_sync_action_e;

typedef enum {
    /* Use first sample (version stable around first get_status). */
    PRODUCT_UI_CONN_STABLE_USE_FIRST = 0,
    /* First raced; use second sample (second pair stable). */
    PRODUCT_UI_CONN_STABLE_USE_SECOND = 1,
    /* Both attempts raced; do not publish / do not update seen. */
    PRODUCT_UI_CONN_STABLE_ABORT = 2,
} product_ui_connection_stable_select_e;

/*
 * Decide sync action for one producer tick.
 * off→on must clear have_valid_snapshot before calling (forces FULL_READ).
 * status_version is never used as readiness — only for skip after valid snap.
 */
product_ui_connection_sync_action_e product_ui_connection_sync_action(
    uint8_t wifi_on,
    uint8_t have_valid_snapshot,
    uint32_t current_version,
    uint32_t seen_version);

/*
 * Bounded stable-read selection (at most two full status samples).
 * v0_* surround the first get_status; v1_* surround the second (only used
 * when the first pair raced).
 */
product_ui_connection_stable_select_e product_ui_connection_stable_select(
    uint32_t v0_before,
    uint32_t v0_after,
    uint32_t v1_before,
    uint32_t v1_after);

#endif /* __PRODUCT_UI_CONNECTION_READ_POLICY_H__ */
