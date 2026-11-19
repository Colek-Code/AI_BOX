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

#ifndef __C_MMI_CMD_RECORDING__
#define __C_MMI_CMD_RECORDING__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

enum {
    C_MMI_CMD_RECORDING_UNKNOWN = 0,
    C_MMI_CMD_RECORDING_AUDIO,
    C_MMI_CMD_RECORDING_QUIT_AUDIO,
    C_MMI_CMD_RECORDING_STOP_AUDIO
};

enum {
    C_MMI_CMD_AUDIO_TYPE_UNKNOWN = 0,
    C_MMI_CMD_AUDIO_TYPE_INTERNAL,
    C_MMI_CMD_AUDIO_TYPE_EXTERNAL,
    C_MMI_CMD_AUDIO_TYPE_CALL
};

typedef struct {
    uint32_t audio_type;
} c_mmi_cmd_recording_param_t;

int32_t c_mmi_cmd_recording_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
