/*
 * Copyright 2025 Alibaba Group Holding Ltd.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __C_MMI_CMD_DEVICE__
#define __C_MMI_CMD_DEVICE__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// event 多媒体相关命令的枚举定义
enum {
    C_MMI_CMD_DEVICE_UNKNOWN = 0,
    C_MMI_CMD_DEVICE_SHUTDOWN,
    C_MMI_CMD_DEVICE_QUIT,
    C_MMI_CMD_DEVICE_BACK,
    C_MMI_CMD_DEVICE_CONFIRM,
    C_MMI_CMD_DEVICE_CANCEL,
    C_MMI_CMD_DEVICE_SELECT,
    C_MMI_CMD_DEVICE_CHECK_BATTERY
};

// confirm 命令参数
typedef struct {
    bool record;
} c_mmi_cmd_device_confirm_param_t;

// select 命令参数
typedef struct {
    uint32_t index;
} c_mmi_cmd_device_select_param_t;

int32_t c_mmi_cmd_device_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
