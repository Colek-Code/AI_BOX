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

#ifndef __C_MMI_CMD_NAVIGATE__
#define __C_MMI_CMD_NAVIGATE__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define C_MMI_CMD_NAVIGATE_PARAM_LEN    64

enum {
    C_MMI_CMD_NAVIGATE_UNKNOWN = 0,
    C_MMI_CMD_NAVIGATE_START,
    C_MMI_CMD_NAVIGATE_QUIT
};

typedef struct {
    char to_poi[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char from_poi[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char travel_tool[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char endLoc_city[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char startLoc_province[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char endLoc_poi[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char startLoc_area[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char startLoc_poi[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char startLoc_city[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char endLoc_province[C_MMI_CMD_NAVIGATE_PARAM_LEN];
    char endLoc_area[C_MMI_CMD_NAVIGATE_PARAM_LEN];
} c_mmi_cmd_navigate_param_t;

int32_t c_mmi_cmd_navigate_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
