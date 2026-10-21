/**
 * @file    ws_service.h
 * @brief   WebSocket 云平台通信服务（正式协议）
 * @note    使用签名 Query String 握手、应用层心跳、云平台消息信封
 *          不依赖 ai_service，所有接收消息通过事件总线分发（WS 命名空间）
 */

#ifndef WS_SERVICE_H
#define WS_SERVICE_H

#include "framework/d_event_payloads.h"
#include "framework/d_error.h"

//*********************************************************************************//
//                              事件数据结构                                        //
//*********************************************************************************//

/* EVT_WS_CONNECTED 事件数据 */
typedef struct {
    int device_session_id;
} ws_connected_info_t;

/* EVT_WS_CHAT_DELTA 事件数据 */
typedef struct {
    char text[256];
    char trace_id[64];
} ws_chat_delta_t;

/* EVT_WS_CHAT_DONE / EVT_WS_CHAT_ACCEPTED 事件数据 */
typedef struct {
    char trace_id[64];
} ws_chat_event_t;

/* EVT_WS_COMMAND_MSG / EVT_WS_WORKFLOW_MSG 事件数据 */
typedef struct {
    char type[64];
    char command_id[64];
    char payload[512];
} ws_general_msg_t;

//*********************************************************************************//
//                              服务初始化                                          //
//*********************************************************************************//

/**
 * @brief 初始化 WebSocket 服务（订阅事件、启动后台任务）
 * @note  WiFi + NTP + 凭证就绪后自动连接、心跳、断线重连
 */
d_err_t ws_service_init(void);

//*********************************************************************************//
//                              发送 API（云平台协议）                              //
//*********************************************************************************//

/** 发送应用层心跳 */
d_err_t ws_send_heartbeat(void);

/** 发送事件上报 */
d_err_t ws_send_event_report(const char *event_type, const char *event_code,
                             const char *payload_json);

/** 发送命令应答 (ack) */
d_err_t ws_send_command_ack(const char *command_id);

/** 发送命令结果 */
d_err_t ws_send_command_result(const char *command_id, int success,
                               const char *result_json);

/** 发送工作流恢复 */
d_err_t ws_send_workflow_resume(const char *feedback);

/** 切换 Agent */
d_err_t ws_send_agent_switch(int agent_profile_id);

#endif /* WS_SERVICE_H */
