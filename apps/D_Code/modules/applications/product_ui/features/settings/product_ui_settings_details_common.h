/**
 * @file product_ui_settings_details_common.h
 * @brief Private helpers shared by hidden settings detail pages.
 */

#ifndef __PRODUCT_UI_SETTINGS_DETAILS_COMMON_H__
#define __PRODUCT_UI_SETTINGS_DETAILS_COMMON_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"

typedef struct {
    const char *title;
    const char *value;
    product_ui_icon_id_t icon_id;
    product_ui_color_e accent;
    lv_obj_t **row_out;
} product_ui_settings_value_row_t;

lv_obj_t *product_ui_settings_detail_frame_create(lv_obj_t *parent,
                                                  const char *title,
                                                  product_ui_page_view_t *view);
int product_ui_settings_detail_feedback(lv_obj_t *content,
                                        product_ui_feedback_state_e state,
                                        const char *title,
                                        const char *message);
int product_ui_settings_detail_snapshot(
    product_ui_settings_snapshot_t *snapshot);
uint8_t product_ui_settings_detail_value_ready(
    const product_ui_settings_snapshot_t *snapshot, uint32_t capability);
int product_ui_settings_detail_value_row(
    lv_obj_t *content, const product_ui_settings_value_row_t *row_spec);
void product_ui_settings_detail_format_u8(uint8_t value, char output[4]);
void product_ui_settings_detail_format_date(uint16_t year, uint8_t month,
                                            uint8_t day, char output[11]);
void product_ui_settings_detail_format_time(uint8_t hour, uint8_t minute,
                                            uint8_t second, char output[9]);
void product_ui_settings_detail_page_update(uint32_t elapsed_ms);

#endif
#endif /* __PRODUCT_UI_SETTINGS_DETAILS_COMMON_H__ */
