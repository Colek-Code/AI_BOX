/** @file product_ui_status_model.h @brief 无 LVGL 依赖的状态规范化；能力与有效位分开，非法字段单独降级。 */

#ifndef __PRODUCT_UI_STATUS_MODEL_H__
#define __PRODUCT_UI_STATUS_MODEL_H__

#include <stdint.h>

typedef enum {
    PRODUCT_UI_LINK_UNKNOWN,
    PRODUCT_UI_LINK_OFF,
    PRODUCT_UI_LINK_ON,
    PRODUCT_UI_LINK_CONNECTED,
} product_ui_link_state_e;

#define PRODUCT_UI_STATUS_CAP_TIME       (1U << 0)
#define PRODUCT_UI_STATUS_CAP_WIFI       (1U << 1)
#define PRODUCT_UI_STATUS_CAP_BLUETOOTH  (1U << 2)
#define PRODUCT_UI_STATUS_CAP_BATTERY    (1U << 3)

#define PRODUCT_UI_STATUS_VALID_TIME       PRODUCT_UI_STATUS_CAP_TIME
#define PRODUCT_UI_STATUS_VALID_WIFI       PRODUCT_UI_STATUS_CAP_WIFI
#define PRODUCT_UI_STATUS_VALID_BLUETOOTH  PRODUCT_UI_STATUS_CAP_BLUETOOTH
#define PRODUCT_UI_STATUS_VALID_BATTERY    PRODUCT_UI_STATUS_CAP_BATTERY

typedef struct {
    uint8_t capability_mask;
    uint8_t valid_mask;
    uint8_t hour;
    uint8_t minute;
    uint8_t wifi_level;
    product_ui_link_state_e wifi_state;
    product_ui_link_state_e bluetooth_state;
    uint8_t battery_percent;
    uint8_t is_charging;
} product_ui_status_t;

typedef enum {
    PRODUCT_UI_STATUS_OK = 0,
    PRODUCT_UI_STATUS_PARTIAL = 1,
    PRODUCT_UI_STATUS_INVALID_ARGUMENT = -1,
} product_ui_status_result_e;

/**
 * @brief 复制输入并过滤不支持的能力、无能力的有效位及范围无效的状态字段。
 * @param[in] input 调用期间借用的状态值，允许与 output 指向同一对象。
 * @param[out] output 规范化值副本；无效时间、连接或电量字段被清零并撤销有效位。
 * @return 全部有效返回 OK；发生字段降级返回 PARTIAL；任一指针为空返回 INVALID_ARGUMENT 且不写输出。
 * @note 布尔充电状态归一为 0 或 1；没有相应有效位的载荷字段不作真实性保证。
 */
product_ui_status_result_e product_ui_status_normalize(
    const product_ui_status_t *input, product_ui_status_t *output);

#endif /* __PRODUCT_UI_STATUS_MODEL_H__ */
