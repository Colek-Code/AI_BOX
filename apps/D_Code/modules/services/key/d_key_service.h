/**
 * @file    d_key_service.h
 * @brief   ADKey 统一管理模块 — 按键注册/派发/事件总线广播
 * @author  D_Code
 * @date    2026-06-02
 *
 * 使用方式：
 *   1. app_main Phase 4 最先调用 d_key_service_init()
 *   2. 各消费模块在 init 时调用 d_key_register() 注册关心的 key_value
 *   3. app_main 的 SYS_KEY_EVENT 分支调用 d_key_service_dispatch()
 *   4. 被动观察者（UI/日志）通过 d_event_bus 订阅 EVT_KEY_ACTION
 */

#ifndef D_KEY_SERVICE_H
#define D_KEY_SERVICE_H

#include <stdint.h>
#include "framework/d_error.h"

/* SDK 按键事件结构体前向声明 */
struct key_event;

//*********************************************************************************//
//                              类型定义                                           //
//*********************************************************************************//

/**
 * @brief 按键回调函数类型
 * @param key_value  SDK 按键值（KEY_POWER / KEY_OK / ...）
 * @param key_action SDK 按键动作（KEY_EVENT_CLICK / LONG / HOLD / UP / ...）
 * @param user_data  注册时传入的用户数据
 * @return 1=已消费（其他模块不再处理），0=未消费
 */
typedef int (*d_key_handler_t)(uint8_t key_value, uint8_t key_action, void *user_data);

/**
 * @brief 事件总线广播的按键事件数据
 * @note  通过 EVT_KEY_ACTION 事件分发，HOLD 动作不广播
 */
typedef struct {
    uint8_t key_value;   /* SDK key_value (KEY_POWER/KEY_OK/...) */
    uint8_t key_action;  /* KEY_EVENT_CLICK/LONG/UP/... */
} d_key_event_t;

//*********************************************************************************//
//                              API                                                //
//*********************************************************************************//

/**
 * @brief 初始化按键服务（必须在所有消费模块 init 之前调用）
 * @return D_OK=成功
 */
d_err_t d_key_service_init(void);

/**
 * @brief 注册按键处理器
 * @param key_value  SDK 按键值（每个 key_value 只允许一个 handler）
 * @param handler    回调函数
 * @param user_data  传给回调的用户数据（可为 NULL）
 * @return D_OK=成功，D_ERR_INVALID_ARG=参数无效，D_ERR_FULL=注册表满
 */
d_err_t d_key_register(uint8_t key_value, d_key_handler_t handler, void *user_data);

/**
 * @brief 注销按键处理器
 * @param key_value  要注销的按键值
 * @return D_OK=成功，D_ERR_NOT_FOUND=未找到
 */
d_err_t d_key_unregister(uint8_t key_value);

/**
 * @brief SDK 按键事件派发入口
 * @param key  SDK 按键事件指针（来自 SYS_KEY_EVENT payload）
 * @return 1=已被某个 handler 消费，0=无人消费
 * @note   从 d_app_event_handler 调用；内部处理：
 *         1. 日志打印（HOLD 除外）
 *         2. 查回调表，找到则调用 handler
 *         3. 非 HOLD 动作 post EVT_KEY_ACTION 到事件总线
 */
int d_key_service_dispatch(const struct key_event *key);

#endif /* D_KEY_SERVICE_H */
