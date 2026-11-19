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

#ifndef __C_MMI_CMD_MUSIC_RADIO__
#define __C_MMI_CMD_MUSIC_RADIO__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define C_MMI_CMD_MUSIC_RADIO_PARAM_LEN     64
#define C_MMI_CMD_MUSIC_RADIO_URL_LEN       256

// event 音乐相关命令的枚举定义
enum {
    C_MMI_CMD_MUSIC_RADIO_UNKNOWN = 0,
    C_MMI_CMD_MUSIC_RADIO_PLAY,
    C_MMI_CMD_MUSIC_RADIO_EXIT
};

// params 音乐相关命令的参数定义
typedef struct {
    char name[C_MMI_CMD_MUSIC_RADIO_PARAM_LEN];
    char url[C_MMI_CMD_MUSIC_RADIO_URL_LEN];
} c_mmi_cmd_music_radio_param_t;

int32_t c_mmi_cmd_music_radio_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
