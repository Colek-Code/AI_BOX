/**
 * @file    d_event_bus.h
 * @brief   D_Code 发布-订阅事件总线
 * @note    用于跨模块事件通知；服务命令、快照查询和后端注册仍使用各自接口。
 *
 * 设计要点：
 *   - 静态订阅表，不使用动态内存
 *   - d_event_publish()：同步派发（在发布者线程上下文）
 *   - d_event_post()：异步投递（事件分发任务处理）
 *   - 事件 ID 按模块分段（每段 256），避免冲突
 */

#ifndef D_EVENT_BUS_H
#define D_EVENT_BUS_H

#include <stdint.h>

#include "d_error.h"

//*********************************************************************************//
//                              配置                                                //
//*********************************************************************************//

#ifndef D_EVENT_MAX_SUBSCRIBERS
#define D_EVENT_MAX_SUBSCRIBERS     16       /* 每个事件 ID 最大订阅者数 */
#endif

#ifndef D_EVENT_QUEUE_SIZE
#define D_EVENT_QUEUE_SIZE          16      /* 异步事件队列深度 */
#endif

#ifndef D_EVENT_DATA_MAX
#define D_EVENT_DATA_MAX            128     /* 异步事件最大数据拷贝字节数 */
#endif

//*********************************************************************************//
//                              事件 ID（按模块分段）                               //
//*********************************************************************************//

typedef enum {
    /* ─── 系统事件 (0x0000 - 0x00FF) ─── */
    EVT_SYSTEM_READY            = 0x0001,
    EVT_LOW_BATTERY             = 0x0002,

    /* ─── 网络事件 (0x0100 - 0x01FF) ─── */
    EVT_WIFI_CONNECTED          = 0x0100,   /* data: ip_info_t* */
    EVT_WIFI_DISCONNECTED       = 0x0101,
    EVT_WIFI_SSID_NOT_FOUND     = 0x0102,
    EVT_WIFI_CONNECT_FAIL       = 0x0103,

    /* ─── BLE 事件 (0x0200 - 0x02FF) ─── */
    EVT_BLE_CONNECTED           = 0x0200,
    EVT_BLE_DISCONNECTED        = 0x0201,
    EVT_BLE_WIFI_CFG_REQUEST    = 0x0210,   /* data: wifi_cfg_t* */
    EVT_BLE_CMD_RECEIVED        = 0x0211,

    /* ─── AI 事件 (0x0300 - 0x03FF) ─── */
    EVT_AI_REQUEST              = 0x0300,   /* data: ai_request_t* */
    EVT_AI_RESPONSE             = 0x0301,   /* data: ai_response_t* */
    EVT_AI_ERROR                = 0x0302,   /* data: d_error_info_t* */
    EVT_AI_BUSY                 = 0x0303,
    EVT_AI_STREAM_START         = 0x0304,   /* chat.accepted 翻译后 */
    EVT_AI_STREAM_DONE          = 0x0305,   /* chat.done 翻译后 */

    /* ─── 存储事件 (0x0400 - 0x04FF) ─── */
    EVT_SD_INSERTED             = 0x0400,
    EVT_SD_REMOVED              = 0x0401,
    EVT_PHOTO_SAVED             = 0x0402,   /* data: photo_info_t* */

    /* ─── UI 事件 (0x0500 - 0x05FF) ─── */
    EVT_PAGE_ENTER              = 0x0500,
    EVT_PAGE_EXIT               = 0x0501,
    EVT_USER_ACTION             = 0x0502,

    /* ─── NTP 事件 (0x0600 - 0x06FF) ─── */
    EVT_NTP_SYNCED              = 0x0600,   /* NTP 校时完成 */

    /* ─── WebSocket 事件 (0x0700 - 0x07FF) ─── */
    EVT_WS_CONNECTED            = 0x0700,   /* data: ws_connected_info_t* */
    EVT_WS_DISCONNECTED         = 0x0701,
    EVT_WS_MESSAGE_RECV         = 0x0702,   /* data: ws_raw_msg_t*（通用） */
    EVT_WS_ERROR                = 0x0703,   /* data: d_error_info_t* */
    EVT_WS_HANDSHAKE_FAILED     = 0x0704,   /* 连续握手失败超限，可能需重新首配 */
    EVT_WS_CHAT_ACCEPTED        = 0x0710,   /* chat.accepted */
    EVT_WS_CHAT_DELTA           = 0x0711,   /* chat.delta（增量文本） */
    EVT_WS_CHAT_DONE            = 0x0712,   /* chat.done */
    EVT_WS_COMMAND_MSG          = 0x0720,   /* 云端下发命令 */
    EVT_WS_WORKFLOW_MSG         = 0x0730,   /* 工作流消息 */

    /* ─── 首配事件 (0x0800 - 0x08FF) ─── */
    EVT_PROVISION_OK            = 0x0800,   /* 首配成功 */
    EVT_PROVISION_FAIL          = 0x0801,   /* 首配失败 */

    /* ─── RTC 音频事件 (0x0900 - 0x09FF) ─── */
    EVT_RTC_AUDIO_STARTED       = 0x0900,   /* 音频采集/播放已启动 */
    EVT_RTC_AUDIO_STOPPED       = 0x0901,   /* 音频采集/播放已停止 */
    EVT_RTC_AUDIO_VAD_START     = 0x0910,   /* VAD 检测到用户开始说话 */
    EVT_RTC_AUDIO_VAD_STOP      = 0x0911,   /* VAD 检测到用户停止说话 */
    EVT_RTC_AUDIO_ERROR         = 0x09F0,   /* 音频错误 */

    /* ─── BRTC 智能体事件 (0x0A00 - 0x0AFF) ─── */
    EVT_BRTC_LOGIN_OK           = 0x0A00,   /* 引擎登录成功 */
    EVT_BRTC_LOGIN_FAIL         = 0x0A01,   /* 引擎登录失败 */
    EVT_BRTC_CALL_READY         = 0x0A02,   /* RTC 呼叫成功，媒体就绪 (onMediaSetup) */
    EVT_BRTC_CALL_FAIL          = 0x0A03,   /* RTC 呼叫失败 */
    EVT_BRTC_CONNECTED          = 0x0A10,   /* RTC 连接已建立 */
    EVT_BRTC_DISCONNECTED       = 0x0A11,   /* RTC 连接断开 */
    EVT_BRTC_RECONNECTING       = 0x0A12,   /* RTC 重连中 */
    EVT_BRTC_ASR_USER           = 0x0A20,   /* 用户 ASR 字幕 data: brtc_asr_data_t* */
    EVT_BRTC_ASR_AGENT          = 0x0A21,   /* AI 回复字幕 data: brtc_asr_data_t* */
    EVT_BRTC_AI_SPEAKING        = 0x0A30,   /* AI 开始/结束说话 data: bool* (true=开始) */
    EVT_BRTC_FUNCTION_CALL      = 0x0A40,   /* FunctionCall data: brtc_func_call_t* */
    EVT_BRTC_PLAYER_OP          = 0x0A50,   /* 音频播放控制 data: brtc_player_op_t* */
    EVT_BRTC_LICENSE_OK         = 0x0A60,   /* License 验证通过 */
    EVT_BRTC_LICENSE_FAIL       = 0x0A61,   /* License 验证失败 */
    EVT_BRTC_ERROR              = 0x0AF0,   /* BRTC 错误 data: d_error_info_t* */

    EVT_QWEN_STATE_CHANGED      = 0x0B00,
    EVT_QWEN_ASR_RESULT         = 0x0B01,
    EVT_QWEN_LLM_RESULT         = 0x0B02,
    EVT_QWEN_TTS_START          = 0x0B03,
    EVT_QWEN_TTS_END            = 0x0B04,
    EVT_QWEN_CMD_RECEIVED       = 0x0B05,
    EVT_QWEN_TTS_LOCAL_END      = 0x0B06,
    EVT_QWEN_WSS_DISCONNECTED   = 0x0B07,
    EVT_QWEN_LLM_PARTIAL_RESULT = 0x0B08,
    EVT_QWEN_ERROR              = 0x0BF0,

    /* ─── 按键事件 (0x0C00 - 0x0CFF) ─── */
    EVT_KEY_ACTION              = 0x0C00,   /* data: d_key_event_t*（HOLD 不广播） */

    /* ─── XiaoLe 唤醒事件 (0x0D00 - 0x0DFF) ─── */
    EVT_XIAOLE_WAKEUP_STARTED   = 0x0D00,
    EVT_XIAOLE_WAKEUP_STOPPED   = 0x0D01,
    EVT_XIAOLE_WAKEUP_DETECTED  = 0x0D02, /* data: uint32_t caller session generation */
    EVT_XIAOLE_WAKEUP_ERROR     = 0x0DF0,

    /* ─── OTA 升级事件 (0x0E00 - 0x0EFF) ─── */
    EVT_OTA_START               = 0x0E00,
    EVT_OTA_SUCCESS             = 0x0E02,
    EVT_OTA_FAIL                = 0x0EF0,

    EVT_MAX
} d_event_id_t;

//*********************************************************************************//
//                              事件数据结构                                        //
//*********************************************************************************//

typedef struct {
    d_event_id_t  id;           /* 事件类型 */
    const void   *data;         /* 回调期间借用的数据指针（可为 NULL） */
    uint16_t      data_len;     /* 数据长度 */
} d_event_t;

//*********************************************************************************//
//                              回调类型                                            //
//*********************************************************************************//

/**
 * @brief 事件处理回调
 * @param event     事件数据
 * @param user_data 订阅时传入的用户数据
 * @note event 及其 data 指针只在本次回调期间有效，处理者不得长期保存。
 *       同步回调在发布者任务中执行，异步回调在 d_evt_disp 中执行。
 *       总线在订阅锁外调用回调，不提供处理者之间或多次发布之间的互斥。
 */
typedef void (*d_event_handler_t)(const d_event_t *event, void *user_data);

//*********************************************************************************//
//                              API                                                 //
//*********************************************************************************//

/**
 * @brief 初始化事件总线（创建异步分发任务）
 * @note  必须在所有 subscribe 之前调用
 * @return D_OK=就绪，其他错误=基础设施未创建，调用方不得继续订阅
 */
d_err_t d_event_bus_init(void);

/**
 * @brief 订阅事件
 * @param id        要订阅的事件 ID
 * @param handler   回调函数
 * @param user_data 传给回调的用户数据指针（可为 NULL，总线不复制指向的对象）
 * @note 同一事件按 handler 判断重复订阅；重复调用不更新原 user_data。
 *       user_data 指向的对象必须在所有在途回调结束前保持有效。
 * @return 0=成功, <0=失败
 */
int d_event_subscribe(d_event_id_t id, d_event_handler_t handler, void *user_data);

/**
 * @brief 取消订阅
 * @return 0=成功, <0=未找到
 * @note 只移除订阅表项，不等待已经取得订阅者快照的回调结束；
 *       调用方需要自行确认在途回调结束后才能释放 user_data。
 */
int d_event_unsubscribe(d_event_id_t id, d_event_handler_t handler);

/**
 * @brief 同步发布事件（在调用者线程上下文执行所有回调）
 * @param id       事件 ID
 * @param data     事件数据（可为 NULL）
 * @param data_len 数据长度
 * @note 不复制 data；数据及其引用对象必须在本次发布结束前保持有效。
 *       未初始化或没有订阅者时直接返回。void 返回值不表示业务已处理成功。
 */
void d_event_publish(d_event_id_t id, const void *data, uint16_t data_len);

/**
 * @brief 异步投递事件（投入队列，由事件分发任务处理）
 * @note  仅复制 data_len 字节，不深拷贝结构体内的指针及其指向对象。
 *        返回后可释放原始字节缓冲；嵌套指针引用的对象需保持到消费结束，
 *        其生命周期由应用自己的所有权或确认机制管理。
 *        data 为 NULL 或 data_len 为 0 时投递无附带数据的事件。
 *        未初始化、数据超长或队列已满时不投递；无订阅者时也不会回调。
 *        void 返回值不能证明入队、送达或业务处理完成。
 * @param id       事件 ID
 * @param data     事件数据（会被拷贝，可为 NULL）
 * @param data_len 数据长度（不超过 D_EVENT_DATA_MAX）
 */
void d_event_post(d_event_id_t id, const void *data, uint16_t data_len);

#endif /* D_EVENT_BUS_H */
