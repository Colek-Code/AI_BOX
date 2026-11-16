/**
 * @file product_ui_service_registry.h
 * @brief 无 LVGL 依赖的服务快照、能力与同步命令路由合同。
 */

#ifndef PRODUCT_UI_SERVICE_REGISTRY_H
#define PRODUCT_UI_SERVICE_REGISTRY_H

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include <stdint.h>

typedef enum {
    PRODUCT_UI_SERVICE_SYSTEM,
    PRODUCT_UI_SERVICE_CONNECTION,
    PRODUCT_UI_SERVICE_MEDIA,
    PRODUCT_UI_SERVICE_STORAGE,
    PRODUCT_UI_SERVICE_TIME,
    PRODUCT_UI_SERVICE_EVENT,
    PRODUCT_UI_SERVICE_ALARM,
    PRODUCT_UI_SERVICE_POMODORO,
    PRODUCT_UI_SERVICE_SETTINGS,
    PRODUCT_UI_SERVICE_COUNT,
} product_ui_service_id_e;

typedef enum {
    PRODUCT_UI_SERVICE_UNKNOWN,
    PRODUCT_UI_SERVICE_UNAVAILABLE,
    PRODUCT_UI_SERVICE_READY,
    PRODUCT_UI_SERVICE_DEGRADED,
    PRODUCT_UI_SERVICE_ERROR,
} product_ui_service_state_e;

typedef struct {
    product_ui_service_state_e state;
    uint32_t valid_mask;
    uint32_t revision;
    int32_t error_code;
} product_ui_service_snapshot_t;

typedef int (*product_ui_service_command_cb_t)(uint16_t command,
                                               const void *payload,
                                               uint16_t payload_size,
                                               void *user_data);

/** MEDIA 域的传输命令，调用点使用具名枚举。 */
typedef enum {
    PRODUCT_UI_MEDIA_CMD_PREVIOUS = 0,
    PRODUCT_UI_MEDIA_CMD_TOGGLE_PLAY_PAUSE,
    PRODUCT_UI_MEDIA_CMD_NEXT,
} product_ui_media_cmd_e;

/** SYSTEM 域命令；注册处理器前不可用。 */
typedef enum {
    PRODUCT_UI_SYSTEM_CMD_TOGGLE_ROTATION = 0,
} product_ui_system_cmd_e;

/** 服务快照 valid_mask 的运行状态位，不依赖 LVGL。
 * CAP_* 配合 READY 和已注册回调决定控件是否可操作；STATE_* 只驱动选中外观。
 * 媒体暂停时仍可保留 CAP_TRANSPORT，STATE_PLAYING 仅表示正在播放。 */
#define PRODUCT_UI_MEDIA_CAP_TRANSPORT          (1U << 0)
#define PRODUCT_UI_MEDIA_STATE_PLAYING          (1U << 1)
#define PRODUCT_UI_ALARM_CAP_OPEN               (1U << 0)
#define PRODUCT_UI_ALARM_STATE_ACTIVE           (1U << 1)
#define PRODUCT_UI_SYSTEM_CAP_ROTATION          (1U << 0)
#define PRODUCT_UI_SYSTEM_STATE_ROTATION_ON     (1U << 1)

/**
 * @brief 在串行启动阶段重置全部服务快照和命令绑定。
 * @return 成功返回 0；互斥量创建失败返回 -1。
 * @note 重复调用也会清空已注册状态，不用于运行中拆卸或排空回调。
 */
int product_ui_service_registry_init(void);
/**
 * @brief 发布服务可用性、能力位和错误，内容变化时推进代次。
 * @param[in] service 目标服务域。
 * @param[in] state UNKNOWN 至 ERROR 范围内的可用性。
 * @param[in] valid_mask 由各服务域解释的能力位与状态位。
 * @param[in] error_code 该服务发布的错误值。
 * @return 成功或内容未变返回 0；未初始化、非法服务或状态返回 -1。
 */
int product_ui_service_publish(product_ui_service_id_e service,
                               product_ui_service_state_e state,
                               uint32_t valid_mask, int32_t error_code);
/**
 * @brief 为服务内部数据变化单独推进快照代次。
 * @param[in] service 发生数据变化的服务域。
 * @return 成功返回 0；未初始化或非法服务返回 -1。
 */
int product_ui_service_data_changed(product_ui_service_id_e service);
/**
 * @brief 在互斥量内读取服务快照的值副本。
 * @param[in] service 待读取的服务域。
 * @param[out] snapshot 接收状态、能力、代次及错误。
 * @return 成功返回 0；未初始化、非法服务或空输出返回 -1。
 */
int product_ui_service_snapshot(product_ui_service_id_e service,
                                product_ui_service_snapshot_t *snapshot);
/**
 * @brief 查询服务是否明确发布 READY。
 * @param[in] service 目标服务域。
 * @return 读取成功且状态为 READY 返回非零；其余返回零。
 */
uint8_t product_ui_service_is_ready(product_ui_service_id_e service);
/**
 * @brief 查询服务是否同时处于 READY 且绑定命令回调。
 * @param[in] service 目标服务域。
 * @return 满足两个条件返回非零，否则为零。
 * @note 不解释 valid_mask；具体能力位及业务受理由命令域继续检查。
 */
uint8_t product_ui_service_command_available(product_ui_service_id_e service);
/**
 * @brief 原子替换命令回调及其借用上下文，完全相同的绑定不推进代次。
 * @param[in] service 目标服务域。
 * @param[in] callback 借用的处理函数；NULL 表示命令不可用。
 * @param[in] user_data 借用的回调上下文，和回调一起替换。
 * @return 成功返回 0；未初始化或非法服务返回 -1。
 * @note 不校验所有者，也不等待在途调用；释放动态上下文前，调用方须安排静默与排空。当前产品提供者使用静态回调。
 */
int product_ui_service_register_command(product_ui_service_id_e service,
                                        product_ui_service_command_cb_t callback,
                                        void *user_data);
/**
 * @brief 锁内借用 READY 状态及回调绑定，然后锁外同步调用。
 * @param[in] service 目标服务域。
 * @param[in] command 由目标服务解释的命令编号。
 * @param[in] payload 本次调用借用的载荷，不由注册表保留。
 * @param[in] payload_size 载荷字节数；非零时 payload 必须非空。
 * @return 非法参数、不可用或无绑定返回 -1；否则原样返回处理器结果。
 * @note 借用后发生替换、清除或就绪撤销也不取消已借用调用；回调可重入注册表。处理器负责业务受理，返回成功不必表示后台完成。
 */
int product_ui_service_command(product_ui_service_id_e service,
                               uint16_t command, const void *payload,
                               uint16_t payload_size);

#endif
#endif /* PRODUCT_UI_SERVICE_REGISTRY_H */
