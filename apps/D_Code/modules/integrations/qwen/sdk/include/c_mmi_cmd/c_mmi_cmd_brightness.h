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

#ifndef __C_MMI_CMD_BRIGHTNESS__
#define __C_MMI_CMD_BRIGHTNESS__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// event 亮度相关命令的枚举定义
enum {
    C_MMI_CMD_BRIGHTNESS_UNKNOWN = 0,
    C_MMI_CMD_BRIGHTNESS_INCREASE,
    C_MMI_CMD_BRIGHTNESS_DECREASE,
    C_MMI_CMD_BRIGHTNESS_SET
};

// params 亮度相关命令的参数定义
typedef struct {
    uint32_t value;
} c_mmi_cmd_brightness_param_t;

int32_t c_mmi_cmd_brightness_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
