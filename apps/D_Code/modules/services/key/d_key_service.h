/**
 * @file    d_key_service.h
 * @brief   ADKey 统一管理模块 — 按键注册/派发/事件总线广播
 * @author  D_Code
 * @date    2026-06-02
 *
 * 使用方式：
 *   1. 启动层在事件总线就绪后，通过服务清单调用 d_key_service_init()
 *   2. 应用层统一绑定产品按键，保存返回的所有者 token
 *   3. app_main 的 SYS_KEY_EVENT 分支调用 d_key_service_dispatch()
 *   4. 被动观察者（UI/日志）通过 d_event_bus 订阅 EVT_KEY_ACTION
 */

#ifndef D_KEY_SERVICE_H
#define D_KEY_SERVICE_H

#include <stdint.h>
#include "framework/d_error.h"

/* SDK 按键事件结构体前向声明 */
struct key_event;

typedef enum {
    D_KEY_UNHANDLED = 0,
    D_KEY_CONSUMED = 1,
    D_KEY_REJECTED = 2,
} d_key_result_e;

typedef struct {
    int (*handler)(uint8_t key_value, uint8_t key_action, void *user_data);
    void *user_data;
} d_key_binding_t;

//*********************************************************************************//
//                              类型定义                                           //
//*********************************************************************************//

/**
 * @brief 按键回调函数类型
 * @param key_value  SDK 按键值（KEY_POWER / KEY_OK / ...）
 * @param key_action SDK 按键动作（KEY_EVENT_CLICK / LONG / HOLD / UP / ...）
 * @param user_data  注册时传入的用户数据
 * @return UNHANDLED=0；CONSUMED=1或REJECTED=2均阻止fallback，非法结果按拒绝处理。
 * @note 返回消费不表示异步业务完成；回调在注册表锁外执行，不可从ISR派发。
 */
typedef int (*d_key_handler_t)(uint8_t key_value, uint8_t key_action, void *user_data);

/**
 * @brief 事件总线广播的按键事件数据
 * @note  通过 EVT_KEY_ACTION 事件分发，HOLD 动作不广播
 */
typedef struct {
    uint8_t key_value;   /* SDK 按键值（KEY_POWER、KEY_OK 等）。 */
    uint8_t key_action;  /* KEY_EVENT_CLICK/LONG/UP/... */
} d_key_event_t;

//*********************************************************************************//
//                              API                                                //
//*********************************************************************************//

/**
 * @brief 标记按键注册服务就绪，保留既有所有者、在途计数和凭据代次。
 * @return 当前始终返回 D_OK。
 * @note 可重复调用但不是清表或业务重置入口；产品绑定在此服务就绪后装配。
 */
d_err_t d_key_service_init(void);

/**
 * @brief 为一个 SDK 按键值登记唯一的借用回调所有者。
 * @param[in] key_value SDK 按键值，每个值只允许一个所有者。
 * @param[in] handler 回调函数，必须非 NULL。
 * @param[in] user_data 借用上下文，可为 NULL；须持续到注销完成。
 * @param[out] token 必需的所有者凭据输出；失败置零，幂等登记返回原凭据。
 * @return D_OK 为新登记或同回调及上下文幂等返回；不同所有者或注销中为 D_ERR_BUSY；容量或代次耗尽为 D_ERR_FULL；参数无效或未初始化分别为 D_ERR_INVALID_ARG、D_ERR_NOT_READY。
 * @note 新凭据非零、单调且不回绕，不隐式覆盖。回调在注册锁外执行，调用方仍需保护自身上下文及业务资源。
 */
d_err_t d_key_register(uint8_t key_value, d_key_handler_t handler,
                       void *user_data, uint32_t *token);

/**
 * @brief 持匹配凭据显式替换空闲的已绑定回调。
 * @param[in] key_value 要替换的 SDK 按键值。
 * @param[in] expected_token 当前所有者的非零凭据。
 * @param[in] replacement 新回调与借用上下文；结构体内容被复制，指向对象不转移所有权。
 * @param[out] token 必需的新凭据输出，失败置零。
 * @return D_OK 为成功并分配新凭据；过期凭据为 D_ERR_NOT_FOUND；注销中或回调在途为 D_ERR_BUSY；参数无效、未就绪或代次耗尽返回对应错误。
 * @note 即使回调及上下文相同，成功替换也分配新凭据；失败保留旧所有者。旧所有者必须先封闭业务或输入准入，并确认清理，异步停止已受理不等于完成。
 */
d_err_t d_key_replace(uint8_t key_value, uint32_t expected_token,
                      const d_key_binding_t *replacement, uint32_t *token);

/**
 * @brief 关闭匹配凭据的新回调准入，并尝试非阻塞解除注册。
 * @param[in] key_value 要注销的 SDK 按键值。
 * @param[in] token 注册或替换返回的非零所有者凭据，旧凭据不能移除新所有者。
 * @return D_OK 表示注册槽已清除；仍有回调在途为 D_ERR_BUSY；凭据过期为 D_ERR_NOT_FOUND；零凭据或未就绪返回对应错误。
 * @note BUSY 时保留 RETIRING 槽及上下文，调用方持原凭据重试到完成。D_OK 只证明注册表解除，不证明输入序列、SDK 或业务任务已经清理。
 * @note 先关闭业务准入或开始本所有者注销，再取消并确认业务资源，最后释放上下文；不能清理资源时仍允许新输入。
 */
d_err_t d_key_unregister(uint8_t key_value, uint32_t token);

/**
 * @brief 借用已绑定回调派发 SDK 按键，并尽力广播非 HOLD 观察事件。
 * @param[in] key SYS_KEY_EVENT 的借用载荷，进入时复制其结构。
 * @return D_KEY_UNHANDLED 为未处理；CONSUMED 或 REJECTED 均阻止上游后备处理。NULL 输入为未处理，未初始化为拒绝。
 * @note 由应用事件线程调用，不从中断派发。锁内登记借用及在途计数，锁外调用 handler，注销中或计数饱和会拒绝。
 * @note 回调返回消费不代表异步业务完成；事件广播只供观察，不承担可靠命令或完成确认，HOLD 不广播。
 */
int d_key_service_dispatch(const struct key_event *key);

#endif /* D_KEY_SERVICE_H */
