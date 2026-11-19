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

#ifndef __C_MMI_CMD_TELEPHONE__
#define __C_MMI_CMD_TELEPHONE__

#ifdef __cplusplus
extern "C" {
#endif

#define C_MMI_CMD_TELEPHONE_LEN  64

//event 电话相关命令的枚举定义
enum {
    C_MMI_CMD_TELEPHONE_UNKNOWN = 0,
    C_MMI_CMD_TELEPHONE_CALL,
    C_MMI_CMD_TELEPHONE_CONFIRM,
    C_MMI_CMD_TELEPHONE_CANCEL,
    C_MMI_CMD_TELEPHONE_OPEN_CALL,
    C_MMI_CMD_TELEPHONE_QUIT_CALL,
    C_MMI_CMD_TELEPHONE_ANSWER_CALL,
    C_MMI_CMD_TELEPHONE_REJECT_PHONE,
    C_MMI_CMD_TELEPHONE_UPDATE_CONTACTS,
};


//params 电话相关命令的参数定义
// call 指令参数
typedef struct {
   char contact_name[C_MMI_CMD_TELEPHONE_LEN]; 
   char phone_number[C_MMI_CMD_TELEPHONE_LEN];
   char phone_type[C_MMI_CMD_TELEPHONE_LEN];
   char phone_entity[C_MMI_CMD_TELEPHONE_LEN];
   bool record; //默认false
} c_mmi_cmd_call_param_t;

// confirm 指令参数
typedef struct {
   bool record; //默认false
} c_mmi_cmd_confirm_param_t;

// answer_call 指令参数
typedef struct {
   char contact_name[C_MMI_CMD_TELEPHONE_LEN]; 
   bool record; //默认false
} c_mmi_cmd_answer_call_param_t;

int32_t c_mmi_cmd_telephone_register(c_mmi_cmd_event_callback cb);

#ifdef __cplusplus
}
#endif

#endif
