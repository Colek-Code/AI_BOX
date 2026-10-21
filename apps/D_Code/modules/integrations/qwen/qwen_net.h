#ifndef __QWEN_NET_H__
#define __QWEN_NET_H__

#include "framework/d_error.h"

#include <stdint.h>

typedef int32_t (*qwen_net_event_cb_t)(uint32_t event, void *param);

d_err_t qwen_net_prepare(qwen_net_event_cb_t event_cb);
d_err_t qwen_net_sync_auth(void);
void qwen_net_deinit(void);
d_err_t qwen_net_deinit_checked(void);
int qwen_net_is_runtime_allowed(void);

int qwen_net_is_registered(void);
int qwen_net_is_token_valid(void);
int qwen_net_is_token_refresh_needed(void);

const char *qwen_net_get_wss_host(void);
const char *qwen_net_get_wss_port(void);
const char *qwen_net_get_wss_api(void);
const char *qwen_net_get_wss_header(void);

#endif
