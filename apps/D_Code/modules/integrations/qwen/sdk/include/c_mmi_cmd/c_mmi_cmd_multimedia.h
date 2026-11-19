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

#ifndef __C_MMI_CMD_MULTIMEDIA__
#define __C_MMI_CMD_MULTIMEDIA__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define C_MMI_CMD_MULTIMEDIA_PARAM_LEN      64

enum {
    C_MMI_CMD_MULTIMEDIA_UNKNOWN = 0,
    C_MMI_CMD_MULTIMEDIA_PLAY,
    C_MMI_CMD_MULTIMEDIA_START_OVER,
    C_MMI_CMD_MULTIMEDIA_STOP,
    C_MMI_CMD_MULTIMEDIA_RESUME_PLAY,
    C_MMI_CMD_MULTIMEDIA_NEXT,
    C_MMI_CMD_MULTIMEDIA_PREVIOUS,
    C_MMI_CMD_MULTIMEDIA_CHANGE
};

typedef struct {
    char unit[C_MMI_CMD_MULTIMEDIA_PARAM_LEN];
    char device_name[C_MMI_CMD_MULTIMEDIA_PARAM_LEN];
    char operation_type[C_MMI_CMD_MULTIMEDIA_PARAM_LEN];
    char resource_type[C_MMI_CMD_MULTIMEDIA_PARAM_LEN];
    char player[C_MMI_CMD_MULTIMEDIA_PARAM_LEN];
    char name[C_MMI_CMD_MULTIMEDIA_PARAM_LEN];
} c_mmi_cmd_multimedia_param_t;

int32_t c_mmi_cmd_multimedia_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
