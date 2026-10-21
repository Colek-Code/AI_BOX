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

#define TAG "KEY"

//*********************************************************************************//
//                              配置                                               //
//*********************************************************************************//

/* 最大可注册按键数（与 D_board.c ADKey 表对齐） */
#define D_KEY_MAX_HANDLERS  10

//*********************************************************************************//
//                              内部数据结构                                        //
//*********************************************************************************//

typedef struct {
    uint8_t          key_value;   /* SDK key_value */
    uint8_t          registered;  /* 1=已注册, 0=空槽 */
    d_key_handler_t  handler;     /* 回调函数 */
    void            *user_data;   /* 用户数据 */
} d_key_slot_t;

static d_key_slot_t s_key_table[D_KEY_MAX_HANDLERS];
static uint8_t      s_initialized;

//*********************************************************************************//
//                              内部函数                                           //
//*********************************************************************************//

/**
 * @brief 在注册表中查找 key_value 对应的槽位
 * @return 槽位索引，-1=未找到
 */
static int d_key_find_slot(uint8_t key_value)
{
    int idx;

    for (idx = 0; idx < D_KEY_MAX_HANDLERS; idx++) {
        if ((s_key_table[idx].registered != 0U) &&
            (s_key_table[idx].key_value == key_value)) {
            return idx;
        }
    }
    return -1;
}

/**
 * @brief 查找第一个空闲槽位
 * @return 槽位索引，-1=表满
 */
static int d_key_find_free_slot(void)
{
    int idx;

    for (idx = 0; idx < D_KEY_MAX_HANDLERS; idx++) {
        if (s_key_table[idx].registered == 0U) {
            return idx;
        }
    }
    return -1;
}

//*********************************************************************************//
//                              API 实现                                            //
//*********************************************************************************//

d_err_t d_key_service_init(void)
{
    int idx;

    for (idx = 0; idx < D_KEY_MAX_HANDLERS; idx++) {
        s_key_table[idx].registered = 0U;
        s_key_table[idx].handler    = NULL;
        s_key_table[idx].user_data  = NULL;
    }
    s_initialized = 1U;
    LOGI(TAG, "key service init, max_slots=%d", D_KEY_MAX_HANDLERS);
    return D_OK;
}

d_err_t d_key_register(uint8_t key_value, d_key_handler_t handler, void *user_data)
{
    int slot;

    if (handler == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (s_initialized == 0U) {
        LOGE(TAG, "register before init! key=%u", (unsigned)key_value);
        return D_ERR_NOT_READY;
    }

    /* 检查是否已注册 */
    slot = d_key_find_slot(key_value);
    if (slot >= 0) {
        LOGW(TAG, "key=%u already registered, overwriting", (unsigned)key_value);
        s_key_table[slot].handler   = handler;
        s_key_table[slot].user_data = user_data;
        return D_OK;
    }

    /* 查找空闲槽 */
    slot = d_key_find_free_slot();
    if (slot < 0) {
        LOGE(TAG, "register table full, key=%u rejected", (unsigned)key_value);
        return D_ERR_FULL;
    }

    s_key_table[slot].key_value  = key_value;
    s_key_table[slot].handler    = handler;
    s_key_table[slot].user_data  = user_data;
    s_key_table[slot].registered = 1U;
    LOGI(TAG, "key=%u registered at slot=%d", (unsigned)key_value, slot);
    return D_OK;
}

d_err_t d_key_unregister(uint8_t key_value)
{
    int slot;

    slot = d_key_find_slot(key_value);
    if (slot < 0) {
        return D_ERR_NOT_FOUND;
    }

    s_key_table[slot].registered = 0U;
    s_key_table[slot].handler    = NULL;
    s_key_table[slot].user_data  = NULL;
    LOGI(TAG, "key=%u unregistered", (unsigned)key_value);
    return D_OK;
}

int d_key_service_dispatch(const struct key_event *key)
{
    int slot;
    int consumed = 0;

    if (key == NULL) {
        return 0;
    }

    /* 日志打印（HOLD 动作过滤，避免刷屏） */
    if (key->action != KEY_EVENT_HOLD) {
        LOGI(TAG, "key: value=%u action=%u type=%u",
             (unsigned)key->value, (unsigned)key->action, (unsigned)key->type);
    }

    /* 查回调表，派发给已注册 handler */
    slot = d_key_find_slot(key->value);
    if (slot >= 0) {
        consumed = s_key_table[slot].handler(
            key->value, key->action, s_key_table[slot].user_data);
    }

    /* 非 HOLD 动作广播到事件总线（被动观察者：UI/日志） */
    if (key->action != KEY_EVENT_HOLD) {
        d_key_event_t evt;

        evt.key_value  = key->value;
        evt.key_action = key->action;
        d_event_post(EVT_KEY_ACTION, &evt, sizeof(evt));
    }

    return consumed;
}
