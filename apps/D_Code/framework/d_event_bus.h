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

    /* 0x0700—0x08FF 为已退役的设备平台事件保留，不重新分配。 */

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
    EVT_BRTC_FUNCTION_CALL      = 0x0A40,   /* 函数调用载荷：brtc_func_call_t 指针。 */
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
    EVT_XIAOLE_WAKEUP_DETECTED  = 0x0D02, /* 载荷：uint32_t，表示调用方会话代次。 */
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
 * @brief 初始化订阅锁、异步队列信号量和事件分发任务。
 * @return D_OK 表示已就绪；D_ERR_IO 表示基础设施创建失败。
 * @note 启动装配阶段须先于所有订阅和发布调用；已初始化时直接成功，不是运行期重置入口。创建失败按已取得资源逆向释放。
 */
d_err_t d_event_bus_init(void);

/**
 * @brief 借用一个事件回调及其上下文并登记订阅。
 * @param[in] id 事件编号。
 * @param[in] handler 同步或异步分发时调用的处理函数。
 * @param[in] user_data 借用的上下文，可为 NULL；总线不复制指向的对象。
 * @return 0 表示新订阅成功或相同回调已订阅；-1 表示未初始化、回调无效或容量不足。
 * @note 同一事件按 handler 判重，重复订阅不更新原 user_data。上下文须保持到所有在途回调结束。
 * @note 订阅表由互斥锁保护；此接口可能等待锁，不供中断调用。
 */
int d_event_subscribe(d_event_id_t id, d_event_handler_t handler, void *user_data);

/**
 * @brief 从订阅表移除指定事件回调。
 * @param[in] id 事件编号。
 * @param[in] handler 要移除的已注册处理函数。
 * @return 0 表示找到并移除；-1 表示未初始化、参数无效或未找到。
 * @note 可能等待订阅互斥锁；只移除注册项，不等待已经取得快照的回调。调用方确认在途回调结束前不得释放上下文。
 */
int d_event_unsubscribe(d_event_id_t id, d_event_handler_t handler);

/**
 * @brief 在调用线程内同步通知本次取得的订阅者快照。
 * @param[in] id 事件编号。
 * @param[in] data 借用的事件载荷，可为 NULL。
 * @param[in] data_len 载荷字节数，由接收方按事件合同解释。
 * @note 不复制 data；数据及其引用对象须保持到本次发布返回。订阅表锁只用于取得快照，调用回调时已解锁。
 * @note 未初始化或没有订阅者时直接返回；void 返回不代表业务成功。总线不串行化回调或多个发布者。
 */
void d_event_publish(d_event_id_t id, const void *data, uint16_t data_len);

/**
 * @brief 尝试浅拷贝载荷到异步队列，并通知分发任务。
 * @param[in] id 事件编号。
 * @param[in] data 待拷贝载荷；为 NULL 时按无载荷事件投递。
 * @param[in] data_len 载荷字节数，不得超过 D_EVENT_DATA_MAX。
 * @note 仅复制载荷字节；其中指针指向的对象不深拷贝，须由业务自己的所有权或完成确认保持到消费结束。
 * @note 未初始化、超长或队列已满时丢弃；无订阅者不回调。void 返回不证明入队、送达或业务完成。
 */
void d_event_post(d_event_id_t id, const void *data, uint16_t data_len);

#endif /* D_EVENT_BUS_H */
