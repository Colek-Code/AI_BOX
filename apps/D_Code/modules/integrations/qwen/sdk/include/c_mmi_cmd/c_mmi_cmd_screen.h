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

#ifndef __C_MMI_CMD_SCREEN__
#define __C_MMI_CMD_SCREEN__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

enum {
    C_MMI_CMD_SCREEN_UNKNOWN = 0,
    C_MMI_CMD_SCREEN_OFF,
    C_MMI_CMD_SCREEN_SHOT,
    C_MMI_CMD_SCREEN_RECORDING,
    C_MMI_CMD_SCREEN_STOP_RECORDING
};

int32_t c_mmi_cmd_screen_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
