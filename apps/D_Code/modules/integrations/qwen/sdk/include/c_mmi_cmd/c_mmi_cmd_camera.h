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

#ifndef __C_MMI_CMD_CAMERA__
#define __C_MMI_CMD_CAMERA__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

enum {
    C_MMI_CMD_CAMERA_UNKNOWN = 0,
    C_MMI_CMD_CAMERA_QUICK_BURST,
    C_MMI_CMD_CAMERA_TAKE_PHOTO,
    C_MMI_CMD_CAMERA_OPEN_CAMERA,
    C_MMI_CMD_CAMERA_QUIT_CAMERA,
    C_MMI_CMD_CAMERA_OPEN_PHOTO_MODE,
    C_MMI_CMD_CAMERA_QUIT_PHOTO_MODE,
    C_MMI_CMD_CAMERA_OPEN_PREVIEW,
    C_MMI_CMD_CAMERA_QUIT_PREVIEW,
    C_MMI_CMD_CAMERA_VIDEO_RECORDING,
    C_MMI_CMD_CAMERA_OPEN_VIDEO_MODE,
    C_MMI_CMD_CAMERA_QUIT_VIDEO_MODE
};

typedef struct {
    uint32_t number;
} c_mmi_cmd_camera_quick_burst_param_t;

int32_t c_mmi_cmd_camera_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
