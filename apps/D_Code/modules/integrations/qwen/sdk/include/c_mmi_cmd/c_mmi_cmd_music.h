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

#ifndef __C_MMI_CMD_MUSIC__
#define __C_MMI_CMD_MUSIC__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define C_MMI_CMD_MUSIC_PARAM_LEN       64

// event 音乐相关命令的枚举定义
enum {
    C_MMI_CMD_MUSIC_UNKNOWN = 0,
    C_MMI_CMD_MUSIC_PLAY,
    C_MMI_CMD_MUSIC_PLAY_DAILY_PLAYLIST,
    C_MMI_CMD_MUSIC_PLAY_MY_COLLECTION,
    C_MMI_CMD_MUSIC_PLAY_RANDOMLY,
    C_MMI_CMD_MUSIC_LIKE,
    C_MMI_CMD_MUSIC_UNLIKE
};

// sort 排序类型枚举定义
enum {
    C_MMI_CMD_MUSIC_SORT_UNKNOWN = 0,
    C_MMI_CMD_MUSIC_SORT_NEW,
    C_MMI_CMD_MUSIC_SORT_HOT
};

// params 音乐相关命令的参数定义
typedef struct {
    char song[C_MMI_CMD_MUSIC_PARAM_LEN];
    char artist[C_MMI_CMD_MUSIC_PARAM_LEN];
    char album[C_MMI_CMD_MUSIC_PARAM_LEN];
    char style[C_MMI_CMD_MUSIC_PARAM_LEN];
    char language[C_MMI_CMD_MUSIC_PARAM_LEN];
    char general_tag[C_MMI_CMD_MUSIC_PARAM_LEN];
    char era[C_MMI_CMD_MUSIC_PARAM_LEN];
    char music_type[C_MMI_CMD_MUSIC_PARAM_LEN];
    char media_name[C_MMI_CMD_MUSIC_PARAM_LEN];
    uint32_t sort;
} c_mmi_cmd_music_param_t;

int32_t c_mmi_cmd_music_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
