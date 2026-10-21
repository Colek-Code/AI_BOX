/**
 * @file    qwen_cmd.c
 * @brief   Qwen 业务命令解析实现
 * @author  D_Code
 * @date    2026-05-23
 */

#include "config/app_config.h"

#if D_FEATURE_QWEN

#ifndef CONFIG_PLATFORM_AC7911
#define CONFIG_PLATFORM_AC7911 1
#endif

#include "qwen_cmd.h"
#include "qwen_service.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "sdk/include/c_mmi.h"
typedef int32_t(*c_mmi_cmd_event_callback)(uint32_t event, void *params);
typedef int32_t(*c_mmi_cmd_func)(char *param);
int32_t c_mm_cmd_register(char *domain_name, char *cmd, c_mmi_cmd_func func);
#include "sdk/include/c_mmi_cmd/c_mmi_cmd_application.h"
#include "sdk/include/c_mmi_cmd/c_mmi_cmd_voice_translate.h"
#include "sdk/include/c_mmi_cmd/c_mmi_cmd_volume.h"

#include <string.h>

#define TAG "QWEN_CMD"
#define QWEN_CMD_VOLUME_STEP_PERCENT 10
#define QWEN_CMD_VOLUME_VALUE_UNDEFINED 0x7FFFFFFFU

static char s_qwen_cmd_general_domain[] = "general_command";
static char s_qwen_cmd_general_change[] = "change";

static const char *qwen_cmd_domain_name(qwen_cmd_domain_e domain)
{
    switch (domain) {
    case QWEN_CMD_DOMAIN_VOLUME:
        return "volume";
    case QWEN_CMD_DOMAIN_APPLICATION:
        return "application";
    case QWEN_CMD_DOMAIN_VOICE_TRANSLATE:
        return "voice_translate";
    default:
        return "unknown";
    }
}

static const char *qwen_cmd_volume_event_name(uint32_t event)
{
    switch (event) {
    case C_MMI_CMD_VOLUME_INCREASE:
        return "increase";
    case C_MMI_CMD_VOLUME_DECREASE:
        return "decrease";
    case C_MMI_CMD_VOLUME_SET:
        return "set";
    case C_MMI_CMD_VOLUME_MUTE:
        return "mute";
    case C_MMI_CMD_VOLUME_UNMUTE:
        return "unmute";
    default:
        return "unknown";
    }
}

static const char *qwen_cmd_application_event_name(uint32_t event)
{
    switch (event) {
    case C_MM_CMD_APP_OPEN_NOTIFICATION:
        return "open_notification";
    case C_MM_CMD_APP_QUIT_NOTIFICATION:
        return "quit_notification";
    case C_MM_CMD_APP_CLEAN_NOTIFICATION:
        return "clean_notification";
    case C_MM_CMD_APP_OPEN_PLAYER:
        return "open_player";
    case C_MM_CMD_APP_QUIT_PLAYER:
        return "quit_player";
    case C_MM_CMD_APP_OPEN_APP:
        return "open_app";
    case C_MM_CMD_APP_QUIT_APP:
        return "quit_app";
    case C_MM_CMD_APP_OPEN_SETTING:
        return "open_setting";
    case C_MM_CMD_APP_QUIT_SETTING:
        return "quit_setting";
    case C_MM_CMD_APP_OPEN_DND_MODE:
        return "open_dnd_mode";
    case C_MM_CMD_APP_QUIT_DND_MODE:
        return "quit_dnd_mode";
    default:
        return "other";
    }
}

static const char *qwen_cmd_voice_translate_event_name(uint32_t event)
{
    switch (event) {
    case C_MMI_CMD_VOICE_TRANSLATE_START:
        return "start";
    case C_MMI_CMD_VOICE_TRANSLATE_END:
        return "end";
    default:
        return "unknown";
    }
}

static void qwen_cmd_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t src_len;
    uint32_t copy_len;

    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }

    src_len = (uint32_t)strlen(src);
    copy_len = src_len;
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1U;
    }
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static void qwen_cmd_publish(const qwen_cmd_msg_t *msg, const char *action)
{
    if (msg == NULL) {
        return;
    }

    d_event_post(EVT_QWEN_CMD_RECEIVED, msg, sizeof(*msg));
    LOGI(TAG, "cmd parsed domain=%s event=%u value=%u category=%u text=%s action=%s",
         qwen_cmd_domain_name(msg->domain),
         (unsigned)msg->command_event,
         (unsigned)msg->value,
         (unsigned)msg->category,
         msg->text,
         (action != NULL) ? action : "deferred");
}

static d_err_t qwen_cmd_map_register_result(int32_t ret)
{
    if (ret == UTIL_SUCCESS) {
        return D_OK;
    }

    if (ret == UTIL_ERR_INVALID_PARAM) {
        return D_ERR_INVALID_ARG;
    }

    if (ret == UTIL_ERR_BUSY) {
        return D_ERR_BUSY;
    }

    return D_ERR_IO;
}

static const char *qwen_cmd_apply_volume(uint32_t event, uint32_t value)
{
    d_err_t err;
    uint32_t request_id = 0U;

    switch (event) {
    case C_MMI_CMD_VOLUME_INCREASE:
        err = qwen_service_request_volume(QWEN_VOLUME_ADJUST,
            QWEN_CMD_VOLUME_STEP_PERCENT, &request_id);
        break;
    case C_MMI_CMD_VOLUME_DECREASE:
        err = qwen_service_request_volume(QWEN_VOLUME_ADJUST,
            -QWEN_CMD_VOLUME_STEP_PERCENT, &request_id);
        break;
    case C_MMI_CMD_VOLUME_SET:
        if ((value == QWEN_CMD_VOLUME_VALUE_UNDEFINED) || (value > 100U)) {
            return "ignored";
        }
        if (value == 0U) {
            err = qwen_service_request_volume(QWEN_VOLUME_MUTE, 1, &request_id);
            break;
        }
        err = qwen_service_request_volume(QWEN_VOLUME_SET, (int16_t)value, &request_id);
        break;
    case C_MMI_CMD_VOLUME_MUTE:
        err = qwen_service_request_volume(QWEN_VOLUME_MUTE, 1, &request_id);
        break;
    case C_MMI_CMD_VOLUME_UNMUTE:
        err = qwen_service_request_volume(QWEN_VOLUME_MUTE, 0, &request_id);
        break;
    default:
        return "deferred";
    }

    if (err != D_OK) {
        return "failed";
    }

    LOGI(TAG, "volume request accepted: id=%u", (unsigned)request_id);
    return "accepted";
}

int32_t qwen_cmd_handle_volume(uint32_t event, void *params)
{
    qwen_cmd_msg_t msg;
    c_mm_volume_param_t *volume_param;
    const char *action;

    memset(&msg, 0, sizeof(msg));
    msg.domain = QWEN_CMD_DOMAIN_VOLUME;
    msg.command_event = event;
    msg.value = QWEN_CMD_VOLUME_VALUE_UNDEFINED;
    if (params != NULL) {
        volume_param = (c_mm_volume_param_t *)params;
        msg.value = volume_param->value;
        msg.category = volume_param->type;
    }
    qwen_cmd_copy_text(msg.text, sizeof(msg.text), qwen_cmd_volume_event_name(event));
    action = qwen_cmd_apply_volume(event, msg.value);
    qwen_cmd_publish(&msg, action);
    return UTIL_SUCCESS;
}

int32_t qwen_cmd_handle_application(uint32_t event, void *params)
{
    qwen_cmd_msg_t msg;
    c_mm_cmd_app_name_param_t *app_name_param;
    c_mm_cmd_app_setting_param_t *setting_param;

    memset(&msg, 0, sizeof(msg));
    msg.domain = QWEN_CMD_DOMAIN_APPLICATION;
    msg.command_event = event;
    if ((event == C_MM_CMD_APP_OPEN_APP) || (event == C_MM_CMD_APP_QUIT_APP)) {
        app_name_param = (c_mm_cmd_app_name_param_t *)params;
        if (app_name_param != NULL) {
            qwen_cmd_copy_text(msg.text, sizeof(msg.text), app_name_param->app_name);
        }
    } else if ((event == C_MM_CMD_APP_OPEN_SETTING) || (event == C_MM_CMD_APP_QUIT_SETTING)) {
        setting_param = (c_mm_cmd_app_setting_param_t *)params;
        if (setting_param != NULL) {
            qwen_cmd_copy_text(msg.text, sizeof(msg.text), setting_param->type);
        }
    } else {
        qwen_cmd_copy_text(msg.text, sizeof(msg.text), qwen_cmd_application_event_name(event));
    }
    qwen_cmd_publish(&msg, "deferred");
    return UTIL_SUCCESS;
}

int32_t qwen_cmd_handle_voice_translate(uint32_t event, void *params)
{
    qwen_cmd_msg_t msg;

    (void)params;
    memset(&msg, 0, sizeof(msg));
    msg.domain = QWEN_CMD_DOMAIN_VOICE_TRANSLATE;
    msg.command_event = event;
    qwen_cmd_copy_text(msg.text, sizeof(msg.text), qwen_cmd_voice_translate_event_name(event));
    qwen_cmd_publish(&msg, "deferred");
    return UTIL_SUCCESS;
}

static int32_t qwen_cmd_handle_general_change(char *param)
{
    qwen_cmd_msg_t msg;

    (void)param;
    memset(&msg, 0, sizeof(msg));
    msg.domain = QWEN_CMD_DOMAIN_UNKNOWN;
    qwen_cmd_copy_text(msg.text, sizeof(msg.text), s_qwen_cmd_general_change);
    qwen_cmd_publish(&msg, "deferred");
    return UTIL_SUCCESS;
}

d_err_t qwen_cmd_register_handlers(void)
{
    int32_t ret;
    d_err_t err;
    d_err_t first_err;

    first_err = D_OK;

    ret = c_mmi_cmd_volume_register(qwen_cmd_handle_volume);
    err = qwen_cmd_map_register_result(ret);
    if (err != D_OK) {
        LOGW(TAG, "volume cmd register failed: %d", ret);
        first_err = err;
    }

    ret = c_mmi_cmd_application_register(qwen_cmd_handle_application);
    err = qwen_cmd_map_register_result(ret);
    if (err != D_OK) {
        LOGW(TAG, "application cmd register failed: %d", ret);
        if (first_err == D_OK) {
            first_err = err;
        }
    }

    ret = c_mmi_cmd_voice_translate_register(qwen_cmd_handle_voice_translate);
    err = qwen_cmd_map_register_result(ret);
    if (err != D_OK) {
        LOGW(TAG, "voice translate cmd register failed: %d", ret);
        if (first_err == D_OK) {
            first_err = err;
        }
    }

    ret = c_mm_cmd_register(s_qwen_cmd_general_domain, s_qwen_cmd_general_change, qwen_cmd_handle_general_change);
    err = qwen_cmd_map_register_result(ret);
    if (err != D_OK) {
        LOGW(TAG, "general change cmd register failed: %d", ret);
        if (first_err == D_OK) {
            first_err = err;
        }
    }

    LOGI(TAG, "cmd handlers registered result=%d", first_err);
    return first_err;
}

#endif /* D_FEATURE_QWEN */
