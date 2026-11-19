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

#ifndef __C_MMI_CMD_STRING_H__
#define __C_MMI_CMD_STRING_H__

#ifdef __cplusplus
extern "C" {
#endif

#define C_MMI_CMD_STRING_MAX_LENGTH     (32)

#define C_MM_DOMAIN_GEN_CMD_STR                 "general_command"
#define C_MM_DOMAIN_MUSIC_STR                   "music"
#define C_MM_DOMAIN_MUSIC_RADIO_STR             "music_radio"
#define C_MM_DOMAIN_TELEPHONE_STR               "telephone"
#define C_MM_DOMAIN_VOICE_TRANS_STR             "voice_translate"
#define C_MM_DOMAIN_DUAL_TRANS_STR              "dual_voice_translate"

#define C_MM_DOMAIN_VISUAL_VQA_STR              "visual_qa"
#define C_MM_DOMAIN_VISUAL_VIDEO_CHAT_STR       "video_chat"
#define C_MM_DOMAIN_VISUAL_OMNI                 "omni"
#define C_MM_DOMAIN_VISUAL_TRANSLATE_STR        "visual_translate"
#define C_MM_DOMAIN_VISUAL_PROACTOR_STR         "proactor"
#define C_MM_DOMAIN_VISUAL_REMINDER_STR         "reminder"
#define C_MM_DOMAIN_VISUAL_GUIDE_STR            "guide_mode"

// #define C_MM_DOMAIN_NEWS_RADIO_STR          "news_radio"

#define C_MM_CMD_APP_OPEN_NOTIFICATION_STR      "open_notification"
#define C_MM_CMD_APP_EXIT_NOTIFICATION_STR      "exit_notification"
#define C_MM_CMD_APP_CLEAN_NOTIFICATION_STR     "clean_notification"
#define C_MM_CMD_APP_OPEN_PHOTOS_STR            "open_photos"
#define C_MM_CMD_APP_QUIT_PHOTOS_STR            "quit_photos"
#define C_MM_CMD_APP_OPEN_PLAYER_STR            "open_player"
#define C_MM_CMD_APP_QUIT_PLAYER_STR            "quit_player"
#define C_MM_CMD_APP_OPEN_APP_CENTER_STR        "open_app_center"
#define C_MM_CMD_APP_QUIT_APP_CENTER_STR        "quit_app_center"
#define C_MM_CMD_APP_OPEN_PROMPTER_STR          "open_prompter"
#define C_MM_CMD_APP_QUIT_PROMPTER_STR          "quit_prompter"
#define C_MM_CMD_APP_OPEN_APP_STR               "open_app"
#define C_MM_CMD_APP_QUIT_APP_STR               "quit_app"
#define C_MM_CMD_APP_OPEN_SETTING_STR           "open_setting"
#define C_MM_CMD_APP_QUIT_SETTING_STR           "quit_setting"
#define C_MM_CMD_APP_OPEN_SYSTEM_UPDATE_STR     "open_system_update"
#define C_MM_CMD_APP_QUIT_SYSTEM_UPDATE_STR     "quit_system_update"
#define C_MM_CMD_APP_OPEN_DND_MODE_STR          "open_dnd_mode"
#define C_MM_CMD_APP_QUIT_DND_MODE_STR          "quit_dnd_mode"
#define C_MM_CMD_APP_OPEN_AUTO_BRIGHTNESS_STR   "open_auto_brightness"
#define C_MM_CMD_APP_QUIT_AUTO_BRIGHTNESS_STR   "quit_auto_brightness"
#define C_MM_CMD_APP_OPEN_VR_CALIBRATION_STR    "open_vr_calibration"
#define C_MM_CMD_APP_QUIT_VR_CALIBRATION_STR    "quit_vr_calibration"

#define C_MM_CMD_VISUAL_VQA_STR                 "visual_qa"
#define C_MM_CMD_VISUAL_VIDEO_CALL_SWITCH_STR   "switch_video_call_success"
#define C_MM_CMD_VISUAL_VIDEO_CALL_EXIT_STR     "exit_video_call_success"
#define C_MM_CMD_VISUAL_VIDEO_CHAT_OPEN_STR     "open_videochat"
#define C_MM_CMD_VISUAL_VIDEO_CHAT_QUIT_STR     "quit_videochat"
#define C_MM_CMD_VISUAL_OMNI_OPEN               "send_video_stream"
#define C_MM_CMD_VISUAL_OMNI_QUIT               "stop_video_stream"
#define C_MM_CMD_VISUAL_PROACTOR_OPEN_STREAM    "open_video_stream"
#define C_MM_CMD_VISUAL_PROACTOR_CLOSE_STREAM   "close_video_stream"

#define C_MM_CMD_DUAL_TRANS_OPEN                "open_dual_voice_translate"
#define C_MM_CMD_DUAL_TRANS_QUIT                "quit_dual_voice_translate"

typedef struct _mm_cmd_list_t {
    char *cmd_domain;
    const char **cmd_list;
} c_mm_cmd_list_t;

extern const char * const c_mm_cmd_application[];
extern const char * const c_mm_cmd_brightness[];
extern const char * const c_mm_cmd_camera[];
extern const char * const c_mm_cmd_device[];
extern const char * const c_mm_cmd_multimedia[];
extern const char * const c_mm_cmd_music[];
extern const char * const c_mm_cmd_navigate[];
extern const char * const c_mm_cmd_recording[];
extern const char * const c_mm_cmd_screen[];
extern const char * const c_mm_cmd_telephone[];
extern const char * const c_mm_cmd_volume[];

char *c_mm_get_domain_name(char *cmd);

extern const char * const c_ao_cmd_application[];
extern const char * const c_ao_cmd_bluetooth[];
extern const char * const c_ao_cmd_brightness[];
extern const char * const c_ao_cmd_camera[];
extern const char * const c_ao_cmd_device[];
extern const char * const c_ao_cmd_map[];
extern const char * const c_ao_cmd_message[];
extern const char * const c_ao_cmd_multimedia[];
extern const char * const c_ao_cmd_music[];
extern const char * const c_ao_cmd_read[];
extern const char * const c_ao_cmd_recording[];
extern const char * const c_ao_cmd_screen[];
extern const char * const c_ao_cmd_temperature[];
extern const char * const c_ao_cmd_video[];
extern const char * const c_ao_cmd_volume[];

// extern const char *c_mm_cmd_omini[] = {
// extern const char *c_mm_cmd_translate[] = {
// extern const char *c_mm_cmd_video_chat[] = {
// extern const char *c_mm_cmd_visual_qa[] = {

#ifdef __cplusplus
}
#endif

#endif