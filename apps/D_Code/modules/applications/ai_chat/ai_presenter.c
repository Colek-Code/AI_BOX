/**
 * @file    ai_presenter.c
 * @brief   AI Chat Presenter — 事件 ↔ UI 胶水层
 *
 * 职责：
 *   ✅ 订阅事件总线（EVT_AI_RESPONSE 等）
 *   ✅ 调用 View API 更新 UI
 *   ✅ 将用户操作转化为事件发布
 *   ❌ 禁止直接调用 HTTP/网络
 *   ❌ 禁止直接访问 AI 服务内部状态
 *
 */

#include "config/app_config.h"

#if D_FEATURE_AI_CHAT

#include "ai_view.h"
#include "framework/d_event_bus.h"
#include "framework/d_event_payloads.h"
#include "framework/d_log.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include "modules/services/wifi/wifi_service.h"
#include <string.h>

#define TAG "AI_PRES"

//*********************************************************************************//
//                              预设消息                                            //
//*********************************************************************************//

static const char *s_preset_msgs[] = {
    "\xe4\xbd\xa0\xe5\xa5\xbd",                                                         /* 你好 */
    "\xe8\xae\xb2\xe4\xb8\xaa\xe7\xac\x91\xe8\xaf\x9d",                                 /* 讲个笑话 */
    "\xe4\xbb\x8a\xe5\xa4\xa9\xe5\xa4\xa9\xe6\xb0\x94\xe6\x80\x8e\xe4\xb9\x88\xe6\xa0\xb7", /* 今天天气怎么样 */
    "\xe6\x88\x91\xe5\xa5\xbd\xe5\x9b\xb0\xe5\x95\x8a",                                 /* 我好困啊 */
    "\xe6\x88\x91\xe5\xbe\x88\xe4\xb8\x8d\xe5\xbc\x80\xe5\xbf\x83",                     /* 我很不开心 */
    "\xe5\x88\xab\xe6\x83\xb9\xe6\x88\x91",                                             /* 别惹我 */
    "\xe7\x9c\x9f\xe7\x9a\x84\xe5\x90\x97",                                             /* 真的吗 */
    "\xe5\xa5\xbd\xe5\x86\xb7\xe5\x95\x8a",                                             /* 好冷啊 */
    "\xe5\xa5\xbd\xe7\x83\xad\xe5\x95\x8a",                                             /* 好热啊 */
};
#define PRESET_MSG_COUNT  (sizeof(s_preset_msgs) / sizeof(s_preset_msgs[0]))

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

static lv_obj_t *s_container = NULL;
static int s_msg_index = 0;
static int s_waiting_reply = 0;
static int s_is_active = 0;
static volatile int s_network_ok = 0;

/* 延迟 UI 更新 — 事件回调在非 LVGL 线程，不能直接操作 LVGL 控件 */
#define PENDING_TEXT_SIZE 256
static volatile int s_pending_response = 0;
static char s_pending_text[PENDING_TEXT_SIZE];
static int  s_pending_emotion = 0;
static volatile int s_pending_error = 0;
static volatile int s_pending_busy = 0;
static volatile int s_pending_net_disconnect = 0;

//*********************************************************************************//
//                              View → Presenter：用户点击麦克风                    //
//*********************************************************************************//

/**
 * @brief 在页面可受理且联网时轮换预设消息，先更新 UI 再发布 AI 请求。
 * @param[in] e 麦克风事件，仅处理 CLICKED。
 */
static void on_mic_click(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (s_waiting_reply) {
        LOGD(TAG, "AI 正在处理，请等待");
        return;
    }

    if (!s_network_ok) {
        ai_view_add_ai_msg("[Network not ready]");
        return;
    }

    int idx = s_msg_index % PRESET_MSG_COUNT;
    const char *msg = s_preset_msgs[idx];
    s_msg_index++;

    /* 更新 View */
    ai_view_add_user_msg(msg);
    ai_view_set_thinking();

    /* 发布事件给 Service */
    ai_request_t req = { .msg = msg };
    d_event_publish(EVT_AI_REQUEST, &req, sizeof(req));

    ai_view_show_typing(1);
    s_waiting_reply = 1;
    LOGI(TAG, "发送: %s", msg);
}

//*********************************************************************************//
//                              Service → Presenter：事件回调                       //
//*********************************************************************************//

/**
 * @brief 页面活跃时有界缓存回复及情绪，最后置待 UI 呈现标志。
 * @param[in] evt 事件总线借用的 AI 响应事件及载荷。
 * @param[in] ctx 订阅上下文，当前未使用。
 * @note 回调可能不在 LVGL 线程，只缓存；volatile 标志不等同跨线程原子快照保证。
 */
static void on_ai_response(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_AI_RESPONSE) return;
    const ai_response_t *rsp = (const ai_response_t *)evt->data;
    if (!rsp) return;
    if (!s_is_active) return;

    /* 缓存回复文本，由 ai_page_update 在 LVGL 线程中处理 */
    strncpy(s_pending_text, rsp->text, PENDING_TEXT_SIZE - 1);
    s_pending_text[PENDING_TEXT_SIZE - 1] = '\0';
    s_pending_emotion = rsp->emotion;
    s_pending_response = 1;  /* 最后设置标志 */

    s_waiting_reply = 0;
    LOGI(TAG, "显示回复 (emotion=%d)", rsp->emotion);
}

/**
 * @brief 页面活跃且 AI 请求失败时置错误标志，UI 更新留给页面周期。
 * @param[in] evt 借用事件。
 * @param[in] ctx 订阅上下文，当前未使用。
 */
static void on_ai_error(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_AI_ERROR) return;
    if (!s_is_active) return;
    s_pending_error = 1;
    s_waiting_reply = 0;
    LOGW(TAG, "AI 请求失败");
}

/**
 * @brief 页面活跃且收到 AI 忙事件时置提示标志并解除本地等待。
 * @param[in] evt 借用事件。
 * @param[in] ctx 订阅上下文，当前未使用。
 */
static void on_ai_busy(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_AI_BUSY) return;
    if (!s_is_active) return;
    s_pending_busy = 1;
    s_waiting_reply = 0;
}

/**
 * @brief 更新网络标志，断网且页面活跃时排队显示断开提示。
 * @param[in] evt 借用的连接或断开事件。
 * @param[in] ctx 订阅上下文，当前未使用。
 */
static void on_wifi_state(const d_event_t *evt, void *ctx)
{
    if (evt->id == EVT_WIFI_CONNECTED) {
        s_network_ok = 1;
    } else if (evt->id == EVT_WIFI_DISCONNECTED) {
        s_network_ok = 0;
        if (s_is_active) {
            s_pending_net_disconnect = 1;
        }
    }
}

//*********************************************************************************//
//                              页面生命周期                                        //
//*********************************************************************************//

/**
 * @brief 创建纯视图并追加麦克风回调。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 父对象为空返回 -1，否则调用 void 视图创建后返回 0，不单独证明全部子对象分配成功。
 */
static int ai_page_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return -1;
    }

    s_container = parent;
    ai_view_create(parent);
    ai_view_set_mic_callback(on_mic_click, NULL);
    LOGI(TAG, "AI 页面创建完成");
    return 0;
}

/**
 * @brief 标记页面活跃并请求父容器重绘。
 */
static void ai_page_enter(void)
{
    s_is_active = 1;
    if (s_container) {
        lv_obj_invalidate(s_container);
    }
    LOGD(TAG, "AI 页面进入");
}

/**
 * @brief 撤销页面活跃标志，后台事件不再发布新的页面响应缓存。
 */
static void ai_page_exit(void)
{
    s_is_active = 0;
    LOGD(TAG, "AI 页面退出");
}

/**
 * @brief 在 LVGL 周期消费延后回复／错误／忙／断网标志，再推进表情动画。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 */
static void ai_page_update(uint32_t elapsed_ms)
{
    if (!s_is_active) return;

    /* 在 LVGL 安全线程中处理所有延迟的 UI 更新 */
    if (s_pending_response) {
        s_pending_response = 0;
        ai_view_show_typing(0);
        ai_view_add_ai_msg(s_pending_text);
        ai_view_set_emotion(s_pending_emotion);
        ai_view_start_speaking();
    }
    if (s_pending_error) {
        s_pending_error = 0;
        ai_view_show_typing(0);
        ai_view_add_ai_msg("[Request failed]");
        ai_view_set_emotion(EMOTION_SAD);
        ai_view_set_idle();
    }
    if (s_pending_busy) {
        s_pending_busy = 0;
        ai_view_add_ai_msg("[AI busy, try later]");
    }
    if (s_pending_net_disconnect) {
        s_pending_net_disconnect = 0;
        ai_view_add_ai_msg("[\xe7\xbd\x91\xe7\xbb\x9c\xe5\xb7\xb2\xe6\x96\xad\xe5\xbc\x80]");
    }

    ai_view_update(elapsed_ms);
}

//*********************************************************************************//
//                              Presenter 初始化                                    //
//*********************************************************************************//

/**
 * @brief 依次订阅事件，失败逆序撤销，成功后读 WiFi 缓存补齐晚订阅状态。
 * @return 全部订阅成功为 D_OK，任一失败为 IO。
 */
static d_err_t ai_presenter_init(void)
{
    wifi_service_status_t status = {0};

    if (d_event_subscribe(EVT_AI_RESPONSE, on_ai_response, NULL) != 0) {
        goto subscribe_failed;
    }
    if (d_event_subscribe(EVT_AI_ERROR, on_ai_error, NULL) != 0) {
        goto unsubscribe_ai_response;
    }
    if (d_event_subscribe(EVT_AI_BUSY, on_ai_busy, NULL) != 0) {
        goto unsubscribe_ai_error;
    }
    if (d_event_subscribe(EVT_WIFI_CONNECTED, on_wifi_state, NULL) != 0) {
        goto unsubscribe_ai_busy;
    }
    if (d_event_subscribe(EVT_WIFI_DISCONNECTED, on_wifi_state, NULL) != 0) {
        goto unsubscribe_wifi_connected;
    }

    /* 先订阅再读当前缓存，补齐订阅晚于联网完成时的状态缺口。 */
    wifi_service_get_cached_status(&status);
    s_network_ok = (status.state == WIFI_SERVICE_CONN_CONNECTED) ? 1 : 0;
    LOGD(TAG, "Presenter 初始化完成");
    return D_OK;

unsubscribe_wifi_connected:
    (void)d_event_unsubscribe(EVT_WIFI_CONNECTED, on_wifi_state);
unsubscribe_ai_busy:
    (void)d_event_unsubscribe(EVT_AI_BUSY, on_ai_busy);
unsubscribe_ai_error:
    (void)d_event_unsubscribe(EVT_AI_ERROR, on_ai_error);
unsubscribe_ai_response:
    (void)d_event_unsubscribe(EVT_AI_RESPONSE, on_ai_response);
subscribe_failed:
    s_network_ok = 0;
    LOGE(TAG, "Presenter 事件订阅失败");
    return D_ERR_IO;
}

//*********************************************************************************//
//                              模块注册                                            //
//*********************************************************************************//

static const page_ops_t s_ai_chat_page_ops = {
    .create  = ai_page_create,
    .enter   = ai_page_enter,
    .exit    = ai_page_exit,
    .destroy = NULL,
    .update  = ai_page_update,
    .gesture = NULL,
};

static page_desc_t s_ai_chat_page_desc = {
    .page_type = 0, .name = "ai_chat",
    .ops = &s_ai_chat_page_ops,
    .container = NULL, .is_created = 0, .is_active = 0,
};

/**
 * @brief 注册静态旧 AI Chat 页面描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int ai_chat_register_page(int page_type)
{
    s_ai_chat_page_desc.page_type = page_type;
    return page_manager_register(&s_ai_chat_page_desc);
}

/**
 * @brief 把模块初始化交给 Presenter 事件装配。
 * @return 原样返回 Presenter 初始化结果。
 */
static d_err_t ai_chat_module_init(void)
{
    return ai_presenter_init();
}

D_UI_MODULE_DEFINE(ai_chat, "AI Chat", LV_SYMBOL_EDIT, ai_chat_register_page, ai_chat_module_init);

#endif /* D_FEATURE_AI_CHAT */
