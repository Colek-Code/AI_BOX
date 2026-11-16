/**
 * @file product_ui_connection_read_policy.h
 * @brief 可独立主机验证的连接状态读取决策，无 WiFi 平台依赖。
 */

#ifndef __PRODUCT_UI_CONNECTION_READ_POLICY_H__
#define __PRODUCT_UI_CONNECTION_READ_POLICY_H__

#include <stdint.h>

typedef enum {
    /**< WiFi 关闭或后端未启动，发布 UNAVAILABLE。 */
    PRODUCT_UI_CONN_SYNC_UNAVAILABLE = 0,
    /**< 已有有效快照且状态版本未变，跳过状态读取。 */
    PRODUCT_UI_CONN_SYNC_SKIP_READ = 1,
    /**< 需要读取状态，最多进行两次有限采样。 */
    PRODUCT_UI_CONN_SYNC_FULL_READ = 2,
} product_ui_connection_sync_action_e;

typedef enum {
    /**< 第一次采样前后版本稳定，使用第一次结果。 */
    PRODUCT_UI_CONN_STABLE_USE_FIRST = 0,
    /**< 第一次发生竞争、第二次稳定，使用第二次结果。 */
    PRODUCT_UI_CONN_STABLE_USE_SECOND = 1,
    /**< 两次都发生竞争，不发布、不更新已见版本。 */
    PRODUCT_UI_CONN_STABLE_ABORT = 2,
} product_ui_connection_stable_select_e;

/**
 * @brief 决定当前发布周期是否需要读取连接状态。
 * @param[in] wifi_on 后端是否已启用。
 * @param[in] have_valid_snapshot 是否已取得可复用的有效快照。
 * @param[in] current_version 后端当前状态版本。
 * @param[in] seen_version 最近成功发布的状态版本。
 * @return WiFi 关闭返回 UNAVAILABLE；已有有效且版本未变的快照返回 SKIP_READ；其余返回 FULL_READ。
 * @note 关闭后重新开启前须清除有效快照标志；版本仅用于跳过读取，不证明后端就绪。
 */
product_ui_connection_sync_action_e product_ui_connection_sync_action(
    uint8_t wifi_on,
    uint8_t have_valid_snapshot,
    uint32_t current_version,
    uint32_t seen_version);

/**
 * @brief 从最多两次有版本围栏的状态采样中选取稳定结果。
 * @param[in] v0_before 第一次读取前的版本。
 * @param[in] v0_after 第一次读取后的版本。
 * @param[in] v1_before 第二次读取前的版本，首对稳定时忽略。
 * @param[in] v1_after 第二次读取后的版本，首对稳定时忽略。
 * @return 首对稳定取 FIRST；首对竞争而第二对稳定取 SECOND；两对都竞争返回 ABORT，不发布也不更新已见版本。
 */
product_ui_connection_stable_select_e product_ui_connection_stable_select(
    uint32_t v0_before,
    uint32_t v0_after,
    uint32_t v1_before,
    uint32_t v1_after);

#endif /* __PRODUCT_UI_CONNECTION_READ_POLICY_H__ */
