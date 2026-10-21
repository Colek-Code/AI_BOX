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

#ifndef __C_MMI_CMD_H__
#define __C_MMI_CMD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "c_utils.h"
#include "c_mmi_cmd_string.h"

#include "cJSON.h"

#define C_MMI_CMD_VALUE_UNDEFINED       0x7FFFFFFF

typedef int32_t(*c_mmi_cmd_func)(char *param);
typedef int32_t(*c_mmi_cmd_event_callback)(uint32_t event, void *params);

int32_t c_mmi_cmd_analyze(cJSON *extra_info);
int32_t c_mmi_cmd_register(char *cmd, c_mmi_cmd_func func);

int32_t c_mm_cmd_register(char *domain_name, char *cmd, c_mmi_cmd_func func);
int32_t c_ao_cmd_register(char *name, c_mmi_cmd_func func);

#ifdef __cplusplus
}
#endif

#endif
