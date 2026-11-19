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

#ifndef __C_MMI_CMD_APPLICATION__
#define __C_MMI_CMD_APPLICATION__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_mmi_cmd_string.h"

#define C_MM_CMD_APP_LEN               128

//event 应用开关相关命令的枚举定义
enum {
    C_MM_CMD_APP_UNKNOWN = 0,
    C_MM_CMD_APP_OPEN_NOTIFICATION,
    C_MM_CMD_APP_QUIT_NOTIFICATION,
    C_MM_CMD_APP_CLEAN_NOTIFICATION,
    C_MM_CMD_APP_OPEN_PHOTOS,
    C_MM_CMD_APP_QUIT_PHOTOS,
    C_MM_CMD_APP_OPEN_PLAYER,
    C_MM_CMD_APP_QUIT_PLAYER,
    C_MM_CMD_APP_OPEN_APP_CENTER,
    C_MM_CMD_APP_QUIT_APP_CENTER,
    C_MM_CMD_APP_OPEN_PROMPTER,
    C_MM_CMD_APP_QUIT_PROMPTER,
    C_MM_CMD_APP_OPEN_APP,
    C_MM_CMD_APP_QUIT_APP,
    C_MM_CMD_APP_OPEN_SETTING,
    C_MM_CMD_APP_QUIT_SETTING,
    C_MM_CMD_APP_OPEN_SYSTEM_UPDATE,
    C_MM_CMD_APP_QUIT_SYSTEM_UPDATE,
    C_MM_CMD_APP_OPEN_DND_MODE,
    C_MM_CMD_APP_QUIT_DND_MODE,
    C_MM_CMD_APP_OPEN_AUTO_BRIGHTNESS,
    C_MM_CMD_APP_QUIT_AUTO_BRIGHTNESS,
    C_MM_CMD_APP_OPEN_VR_CALIBRATION,
    C_MM_CMD_APP_QUIT_VR_CALIBRATION,
};

//params 应用开关相关命令的参数定义
// for open_app, quit_app
typedef struct {
   char app_name[C_MM_CMD_APP_LEN];
} c_mm_cmd_app_name_param_t;

// for open_setting, quit_setting
typedef struct {
   char type[C_MM_CMD_APP_LEN];
} c_mm_cmd_app_setting_param_t;

int32_t c_mmi_cmd_application_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
