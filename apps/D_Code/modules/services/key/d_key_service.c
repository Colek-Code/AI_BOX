/**
 * @file    d_key_service.c
 * @brief   ADKey 统一管理模块 — 回调注册表 + 事件总线广播
 * @author  D_Code
 * @date    2026-06-02
 */

#include "d_key_service.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "event/key_event.h"
#include "system/includes.h"

#define TAG "KEY"

//*********************************************************************************//
//                              配置                                               //
//*********************************************************************************//

/* 最大可注册按键数（与 D_board.c ADKey 表对齐） */
#define D_KEY_MAX_HANDLERS  10

//*********************************************************************************//
//                              内部数据结构                                        //
//*********************************************************************************//

typedef enum {
    D_KEY_SLOT_EMPTY,
    D_KEY_SLOT_BOUND,
    D_KEY_SLOT_RETIRING,
} d_key_slot_state_e;

typedef struct {
    uint8_t          key_value;   /* SDK key_value */
    d_key_slot_state_e state;
    uint16_t         in_flight;   /**< 已借出而未返回的回调次数。 */
    uint32_t         token;       /**< 非零所有者凭据；与计数一起由注册锁保护。 */
    d_key_handler_t  handler;     /* 回调函数 */
    void            *user_data;   /* 用户数据 */
} d_key_slot_t;

static d_key_slot_t s_key_table[D_KEY_MAX_HANDLERS];
static uint8_t      s_initialized;
static uint32_t     s_token_generation;
static DEFINE_SPINLOCK(s_key_lock);

//*********************************************************************************//
//                              内部函数                                           //
//*********************************************************************************//

/**
 * @brief 在按键注册表中查找非空槽。
 * @param[in] key_value SDK 按键值。
 * @return 匹配槽索引；不存在时为 -1。
 * @note 调用方必须持有 s_key_lock，注销中的槽仍属于原所有者。
 */
static int d_key_find_slot(uint8_t key_value)
{
    int idx;

    for (idx = 0; idx < D_KEY_MAX_HANDLERS; idx++) {
        if ((s_key_table[idx].state != D_KEY_SLOT_EMPTY) &&
            (s_key_table[idx].key_value == key_value)) {
            return idx;
        }
    }
    return -1;
}

/**
 * @brief 查找首个 EMPTY 槽，不能复用注销中的槽。
 * @return 首个空槽索引；表满时为 -1。
 * @note 调用方持有 s_key_lock。
 */
static int d_key_find_free_slot(void)
{
    int idx;

    for (idx = 0; idx < D_KEY_MAX_HANDLERS; idx++) {
        if (s_key_table[idx].state == D_KEY_SLOT_EMPTY) {
            return idx;
        }
    }
    return -1;
}

//*********************************************************************************//
//                              API 实现                                            //
//*********************************************************************************//

/**
 * @brief 在注册锁内分配不回绕的非零所有者凭据。
 * @param[out] token 有效的凭据接收指针，由内部调用方保证。
 * @return 成功为 D_OK；代次达到 UINT32_MAX 时为 D_ERR_FULL，并保留输出。
 * @note 调用方持有 s_key_lock；凭据耗尽不从 1 重新分配。
 */
static d_err_t d_key_next_token_locked(uint32_t *token)
{
    if (s_token_generation == UINT32_MAX) return D_ERR_FULL;
    *token = ++s_token_generation;
    return D_OK;
}

/**
 * @brief 登记服务就绪；完整合同见 d_key_service.h。
 */
d_err_t d_key_service_init(void)
{
    spin_lock(&s_key_lock);
    s_initialized = 1U;
    spin_unlock(&s_key_lock);
    return D_OK;
}

/**
 * @brief 登记或幂等取得按键所有权；完整合同见 d_key_service.h。
 */
d_err_t d_key_register(uint8_t key_value, d_key_handler_t handler,
                       void *user_data, uint32_t *token)
{
    int slot;
    d_err_t result = D_OK;

    if (token != NULL) *token = 0U;
    if (handler == NULL || token == NULL) return D_ERR_INVALID_ARG;
    spin_lock(&s_key_lock);
    if (!s_initialized) {
        result = D_ERR_NOT_READY;
        goto finish;
    }
    slot = d_key_find_slot(key_value);
    if (slot >= 0) {
        if (s_key_table[slot].state == D_KEY_SLOT_BOUND &&
            s_key_table[slot].handler == handler &&
            s_key_table[slot].user_data == user_data) {
            *token = s_key_table[slot].token;
        } else {
            result = D_ERR_BUSY;
        }
        goto finish;
    }
    slot = d_key_find_free_slot();
    if (slot < 0) {
        result = D_ERR_FULL;
        goto finish;
    }
    result = d_key_next_token_locked(token);
    if (result != D_OK) goto finish;
    s_key_table[slot].key_value = key_value;
    s_key_table[slot].handler = handler;
    s_key_table[slot].user_data = user_data;
    s_key_table[slot].token = *token;
    s_key_table[slot].in_flight = 0U;
    s_key_table[slot].state = D_KEY_SLOT_BOUND;
finish:
    spin_unlock(&s_key_lock);
    return result;
}

/**
 * @brief 在无借用时显式替换所有者；完整合同见 d_key_service.h。
 */
d_err_t d_key_replace(uint8_t key_value, uint32_t expected_token,
                      const d_key_binding_t *replacement, uint32_t *token)
{
    int slot;
    d_err_t result = D_OK;

    if (token != NULL) *token = 0U;
    if (!expected_token || replacement == NULL ||
        replacement->handler == NULL || token == NULL) return D_ERR_INVALID_ARG;
    spin_lock(&s_key_lock);
    if (!s_initialized) {
        result = D_ERR_NOT_READY;
        goto finish;
    }
    slot = d_key_find_slot(key_value);
    if (slot < 0 || s_key_table[slot].token != expected_token) {
        result = D_ERR_NOT_FOUND;
        goto finish;
    }
    if (s_key_table[slot].state != D_KEY_SLOT_BOUND || s_key_table[slot].in_flight) {
        result = D_ERR_BUSY;
        goto finish;
    }
    result = d_key_next_token_locked(token);
    if (result != D_OK) goto finish;
    s_key_table[slot].handler = replacement->handler;
    s_key_table[slot].user_data = replacement->user_data;
    s_key_table[slot].token = *token;
finish:
    spin_unlock(&s_key_lock);
    return result;
}

/**
 * @brief 关闭回调准入并等待借用清零；完整合同见 d_key_service.h。
 */
d_err_t d_key_unregister(uint8_t key_value, uint32_t token)
{
    int slot;
    d_err_t result = D_OK;

    if (!token) return D_ERR_INVALID_ARG;
    spin_lock(&s_key_lock);
    if (!s_initialized) {
        result = D_ERR_NOT_READY;
        goto finish;
    }
    slot = d_key_find_slot(key_value);
    if (slot < 0 || s_key_table[slot].token != token) {
        result = D_ERR_NOT_FOUND;
        goto finish;
    }
    s_key_table[slot].state = D_KEY_SLOT_RETIRING;
    if (s_key_table[slot].in_flight) {
        result = D_ERR_BUSY;
        goto finish;
    }
    s_key_table[slot].handler = NULL;
    s_key_table[slot].user_data = NULL;
    s_key_table[slot].token = 0U;
    s_key_table[slot].state = D_KEY_SLOT_EMPTY;
finish:
    spin_unlock(&s_key_lock);
    return result;
}

/**
 * @brief 借用回调并在锁外派发按键；完整合同见 d_key_service.h。
 */
int d_key_service_dispatch(const struct key_event *key)
{
    int slot;
    int result = D_KEY_UNHANDLED;
    uint32_t token = 0U;
    d_key_binding_t borrowed = {NULL, NULL};
    struct key_event event;

    if (key == NULL) return D_KEY_UNHANDLED;
    event = *key;
    spin_lock(&s_key_lock);
    if (!s_initialized) {
        spin_unlock(&s_key_lock);
        return D_KEY_REJECTED;
    }
    slot = d_key_find_slot(event.value);
    if (slot >= 0) {
        if (s_key_table[slot].state != D_KEY_SLOT_BOUND ||
            s_key_table[slot].in_flight == UINT16_MAX) {
            result = D_KEY_REJECTED;
        } else {
            s_key_table[slot].in_flight++;
            borrowed.handler = s_key_table[slot].handler;
            borrowed.user_data = s_key_table[slot].user_data;
            token = s_key_table[slot].token;
        }
    }
    spin_unlock(&s_key_lock);

    if (event.action != KEY_EVENT_HOLD) {
        LOGI(TAG, "key: value=%u action=%u type=%u",
             (unsigned)event.value, (unsigned)event.action, (unsigned)event.type);
    }
    if (borrowed.handler != NULL) {
        result = borrowed.handler(event.value, event.action, borrowed.user_data);
        if (result < D_KEY_UNHANDLED || result > D_KEY_REJECTED) result = D_KEY_REJECTED;
        spin_lock(&s_key_lock);
        /* 注销中的借用仍属于原槽，只有匹配凭据的回调归还后才能显式确认清槽。 */
        if (s_key_table[slot].token == token && s_key_table[slot].in_flight) {
            s_key_table[slot].in_flight--;
        }
        spin_unlock(&s_key_lock);
    }

    if (event.action != KEY_EVENT_HOLD) {
        const d_key_event_t observation = {event.value, event.action};
        (void)d_event_post(EVT_KEY_ACTION, &observation, sizeof(observation));
    }
    return result;
}
