/*
 * Copyright 2025 Alibaba Group Holding Ltd.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http: *www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __C_MMI_CMD_VOLUME__
#define __C_MMI_CMD_VOLUME__

#ifdef __cplusplus
extern "C" {
#endif

enum {
    C_MM_CMD_TYPE_UNKNOWN = 0,
    C_MM_CMD_TYPE_SYSTEM,
    C_MM_CMD_TYPE_MEDIA,
    C_MM_CMD_TYPE_CALL
};

//event 音量设置相关命令的枚举定义
enum {
    C_MMI_CMD_VOLUME_UNKNOWN = 0,
    C_MMI_CMD_VOLUME_INCREASE,
    C_MMI_CMD_VOLUME_DECREASE,
    C_MMI_CMD_VOLUME_SET,
    C_MMI_CMD_VOLUME_MUTE,
    C_MMI_CMD_VOLUME_UNMUTE
};

//params 音量设置相关命令的参数定义
typedef struct {
    uint32_t value;
    uint32_t type;
} c_mm_volume_param_t;

int32_t c_mmi_cmd_volume_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
