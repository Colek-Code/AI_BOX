/**
 * @file product_ui_settings_details_common.c
 * @brief Private settings detail page helpers.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/settings/product_ui_settings_details_common.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"

lv_obj_t *product_ui_settings_detail_frame_create(lv_obj_t *parent,
                                                  const char *title,
                                                  product_ui_page_view_t *view)
{
    product_ui_page_spec_t spec = {
        .title = title,
        .back_title = "设置",
        .show_back = 1,
        .role = PRODUCT_UI_PAGE_ROLE_SETTINGS,
    };
    lv_obj_t *content;

    if ((parent == NULL) || (title == NULL) || (view == NULL)) {
        return NULL;
    }
    if (product_ui_page_frame_create_ex(parent, &spec, view) != 0) {
        return NULL;
    }
    content = view->content;
    if (content == NULL) {
        return NULL;
    }
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, PRODUCT_UI_SPACE_SM, 0);
    return content;
}

int product_ui_settings_detail_feedback(lv_obj_t *content,
                                        product_ui_feedback_state_e state,
                                        const char *title,
                                        const char *message)
{
    if ((content == NULL) || (title == NULL) || (message == NULL)) {
        return -1;
    }
    return product_ui_feedback_create(content, state, title, message) == NULL ?
           -1 : 0;
}

int product_ui_settings_detail_snapshot(
    product_ui_settings_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return -1;
    }
    if (!product_ui_service_is_ready(PRODUCT_UI_SERVICE_SETTINGS)) {
        return -1;
    }
    return product_ui_settings_service_snapshot(snapshot);
}

uint8_t product_ui_settings_detail_value_ready(
    const product_ui_settings_snapshot_t *snapshot, uint32_t capability)
{
    if (snapshot == NULL) {
        return 0U;
    }
    return product_ui_service_is_ready(PRODUCT_UI_SERVICE_SETTINGS) &&
           product_ui_service_command_available(
               PRODUCT_UI_SERVICE_SETTINGS) &&
           ((snapshot->capability_mask & capability) != 0U) &&
           ((snapshot->valid_mask & capability) != 0U);
}

int product_ui_settings_detail_value_row(
    lv_obj_t *content, const product_ui_settings_value_row_t *row_spec)
{
    product_ui_list_item_t item;
    product_ui_row_spec_t list_spec;
    lv_obj_t *row;

    if ((content == NULL) || (row_spec == NULL) ||
        (row_spec->title == NULL) || (row_spec->value == NULL)) {
        return -1;
    }
    item = (product_ui_list_item_t){
        .item_id = "settings_detail_value",
        .title = row_spec->title,
        .icon_id = row_spec->icon_id,
        .accent = row_spec->accent,
    };
    list_spec = (product_ui_row_spec_t) {
        .item = &item,
        .subtitle = "",
        .is_single_line = 1U,
        .status_text = row_spec->value,
        .interaction = PRODUCT_UI_ROW_INTERACTION_NONE,
        .trailing = PRODUCT_UI_ROW_TRAILING_STATUS,
        .is_enabled = 1U,
    };
    row = product_ui_list_row_create(content, &list_spec);
    if (row_spec->row_out != NULL) {
        *row_spec->row_out = row;
    }
    return row != NULL ? 0 : -1;
}

void product_ui_settings_detail_format_u8(uint8_t value, char output[4])
{
    uint8_t hundreds;
    uint8_t tens;
    uint8_t offset = 0U;

    if (output == NULL) {
        return;
    }
    hundreds = value / 100U;
    tens = (value / 10U) % 10U;
    if (hundreds > 0U) {
        output[offset++] = (char)('0' + hundreds);
    }
    if ((hundreds > 0U) || (tens > 0U)) {
        output[offset++] = (char)('0' + tens);
    }
    output[offset++] = (char)('0' + (value % 10U));
    output[offset] = '\0';
}

static void product_ui_settings_detail_format_two(uint8_t value,
                                                  char output[2])
{
    output[0] = (char)('0' + ((value / 10U) % 10U));
    output[1] = (char)('0' + (value % 10U));
}

void product_ui_settings_detail_format_date(uint16_t year, uint8_t month,
                                            uint8_t day, char output[11])
{
    if (output == NULL) {
        return;
    }
    output[0] = (char)('0' + ((year / 1000U) % 10U));
    output[1] = (char)('0' + ((year / 100U) % 10U));
    output[2] = (char)('0' + ((year / 10U) % 10U));
    output[3] = (char)('0' + (year % 10U));
    output[4] = '-';
    product_ui_settings_detail_format_two(month, &output[5]);
    output[7] = '-';
    product_ui_settings_detail_format_two(day, &output[8]);
    output[10] = '\0';
}

void product_ui_settings_detail_format_time(uint8_t hour, uint8_t minute,
                                            uint8_t second, char output[9])
{
    if (output == NULL) {
        return;
    }
    product_ui_settings_detail_format_two(hour, &output[0]);
    output[2] = ':';
    product_ui_settings_detail_format_two(minute, &output[3]);
    output[5] = ':';
    product_ui_settings_detail_format_two(second, &output[6]);
    output[8] = '\0';
}

void product_ui_settings_detail_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
