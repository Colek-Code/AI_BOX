/**
 * @file    d_event_payloads.h
 * @brief   Shared D_Code event payloads and emotion values.
 */

#ifndef D_EVENT_PAYLOADS_H
#define D_EVENT_PAYLOADS_H

#include <stdint.h>

//*********************************************************************************//
//                              情绪枚举（全局唯一定义）                            //
//*********************************************************************************//

typedef enum {
    EMOTION_NEUTRAL  = 0,   /* 中性 */
    EMOTION_ANGRY    = 1,   /* 生气 */
    EMOTION_SAD      = 2,   /* 难过 */
    EMOTION_HAPPY    = 3,   /* 开心 */
    EMOTION_SLEEPY   = 4,   /* 困倦 */
    EMOTION_DOUBTFUL = 5,   /* 疑惑 */
    EMOTION_COLD     = 6,   /* 冷 */
    EMOTION_HOT      = 7,   /* 热 */
    EMOTION_MAX
} d_emotion_t;

//*********************************************************************************//
//                              事件数据结构                                        //
//*********************************************************************************//

/* AI 请求 */
typedef struct {
    const char *msg;
} ai_request_t;

/* AI 响应（sizeof > D_EVENT_DATA_MAX，只能用 d_event_publish 同步投递） */
typedef struct {
    char        text[512];
    d_emotion_t emotion;
} ai_response_t;

/* WiFi 配网 */
typedef struct {
    char ssid[33];
    char pwd[65];
} wifi_cfg_t;

/* WiFi IP 信息 */
typedef struct {
    char ip[16];
} ip_info_t;

/* 照片信息 */
typedef struct {
    const char *path;
    uint32_t    size;
} photo_info_t;

/* 错误信息 */
typedef struct {
    int         code;
    const char *msg;
} d_error_info_t;

#endif /* D_EVENT_PAYLOADS_H */
