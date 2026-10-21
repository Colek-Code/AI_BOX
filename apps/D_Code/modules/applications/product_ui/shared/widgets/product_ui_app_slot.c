/** @file product_ui_app_slot.c
 *  @brief One home tile owns its artwork, caption and complete press sequence.
 */
#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/product_ui/shared/widgets/product_ui_app_slot.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/assets/product_ui_color_icons.h"
#include <string.h>

#define PRODUCT_UI_APP_SLOT_SIZE 80
#define PRODUCT_UI_APP_CAPTION_Y 64
#define PRODUCT_UI_APP_CAPTION_H 16

int product_ui_image_fit(lv_obj_t *image, const lv_image_dsc_t *source,
                         int32_t width, int32_t height)
{
    uint32_t scale;

    if ((image == NULL) || (source == NULL) || (source->header.w == 0U) ||
        (source->header.h == 0U) || (width <= 0) || (height <= 0) ||
        (width > PRODUCT_UI_SCREEN_W) || (height > PRODUCT_UI_SCREEN_H)) {
        return -1;
    }
    if ((source->header.cf != LV_COLOR_FORMAT_ARGB8888) &&
        (source->header.cf != LV_COLOR_FORMAT_RGB565) &&
        (source->header.cf != LV_COLOR_FORMAT_RGB888) &&
        (source->header.cf != LV_COLOR_FORMAT_XRGB8888)) {
        return -1;
    }
    scale = LV_MIN((uint32_t)width * LV_SCALE_NONE / source->header.w,
                   (uint32_t)height * LV_SCALE_NONE / source->header.h);
    if (scale == 0U) {
        return -1;
    }
    lv_image_set_src(image, source);
    lv_obj_set_size(image, width, height);
    lv_image_set_align(image, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_rotation(image, 0);
    lv_image_set_pivot(image, source->header.w / 2, source->header.h / 2);
    lv_image_set_scale(image, scale);
    return 0;
}

static void product_ui_app_slot_apply_press(void *context, int32_t progress)
{
    product_ui_app_slot_t *slot = context;
    if (slot->icon == NULL || slot->title == NULL) return;
    lv_image_set_scale(slot->icon, product_ui_motion_is_reduced() ? LV_SCALE_NONE :
                       (uint32_t)(LV_SCALE_NONE - progress * 10 / 1000));
    lv_obj_set_style_image_recolor(slot->icon, lv_color_black(), 0);
    lv_obj_set_style_image_recolor_opa(slot->icon,
                                       (lv_opa_t)(progress * LV_OPA_20 / 1000), 0);
    lv_obj_set_style_text_opa(slot->title,
                              (lv_opa_t)(LV_OPA_COVER - progress * (LV_OPA_COVER - LV_OPA_80) / 1000), 0);
}

static void product_ui_app_slot_pressed(product_ui_app_slot_t *slot, uint8_t is_pressed)
{
    product_ui_motion_start(&slot->press_motion, is_pressed ? 1000 : 0,
        is_pressed ? PRODUCT_UI_MOTION_PRESS_MS : PRODUCT_UI_MOTION_RELEASE_MS);
}

void product_ui_app_slot_dispose(product_ui_app_slot_t *slot)
{
    if (slot == NULL) return;
    product_ui_motion_dispose(&slot->press_motion);
    slot->is_armed = 0U;
}

void product_ui_app_slot_cancel(product_ui_app_slot_t *slot)
{
    if ((slot == NULL) || (slot->root == NULL)) {
        return;
    }
    slot->is_armed = 0U;
    lv_obj_clear_state(slot->root, LV_STATE_PRESSED);
    product_ui_motion_set(&slot->press_motion, 0);
}

static void product_ui_app_slot_event(lv_event_t *event)
{
    product_ui_app_slot_t *slot = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_DELETE) {
        product_ui_app_slot_dispose(slot);
        memset(slot, 0, sizeof(*slot));
    } else if (code == LV_EVENT_PRESSED) {
        slot->is_armed = slot->is_enabled;
        product_ui_app_slot_pressed(slot, slot->is_armed);
    } else if ((code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) && slot->is_armed) {
        lv_indev_t *input = lv_event_get_indev(event);
        lv_point_t point;
        lv_area_t area;

        if (input != NULL) {
            lv_indev_get_point(input, &point);
            lv_obj_get_coords(slot->root, &area);
            if (point.x < area.x1 || point.x > area.x2 ||
                point.y < area.y1 || point.y > area.y2) {
                product_ui_app_slot_cancel(slot);
            }
        }
        if (code == LV_EVENT_RELEASED) product_ui_app_slot_pressed(slot, 0U);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        product_ui_app_slot_cancel(slot);
    } else if (code == LV_EVENT_CLICKED) {
        if (!slot->is_enabled || !slot->is_armed) {
            lv_event_stop_processing(event);
        }
        slot->is_armed = 0U;
    }
}

int product_ui_app_slot_create(lv_obj_t *parent, product_ui_app_slot_t *slot,
                               lv_event_cb_t action, void *user_data)
{
    if ((parent == NULL) || (slot == NULL) || (action == NULL)) {
        return -1;
    }
    memset(slot, 0, sizeof(*slot));
    slot->root = lv_button_create(parent);
    if (slot->root == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(slot->root);
    /* Retain pointer ownership so a sampled jump can still reach the pager. */
    lv_obj_clear_flag(slot->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(slot->root, PRODUCT_UI_APP_SLOT_SIZE, PRODUCT_UI_APP_SLOT_SIZE);
    lv_obj_add_flag(slot->root, LV_OBJ_FLAG_EVENT_BUBBLE);
    slot->icon = lv_image_create(slot->root);
    slot->title = lv_label_create(slot->root);
    if ((slot->icon == NULL) || (slot->title == NULL)) {
        lv_obj_delete(slot->root);
        memset(slot, 0, sizeof(*slot));
        return -1;
    }
    lv_obj_clear_flag(slot->icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(slot->title, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_pos(slot->icon,
                    (PRODUCT_UI_APP_SLOT_SIZE - PRODUCT_UI_HOME_ICON_CANVAS_SIZE) / 2, 1);
    lv_obj_set_pos(slot->title, 0, PRODUCT_UI_APP_CAPTION_Y);
    lv_obj_set_size(slot->title, PRODUCT_UI_APP_SLOT_SIZE, PRODUCT_UI_APP_CAPTION_H);
    lv_obj_set_style_text_font(slot->title, product_ui_font(PRODUCT_UI_FONT_STATUS), 0);
    lv_obj_set_style_text_align(slot->title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(slot->title, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(slot->title, LV_OPA_TRANSP, 0);
    lv_label_set_long_mode(slot->title, LV_LABEL_LONG_DOT);
    {
        const product_ui_motion_binding_t binding = {
            .owner = slot->root, .context = slot, .apply = product_ui_app_slot_apply_press,
        };
        product_ui_motion_bind(&slot->press_motion, &binding);
    }
    lv_obj_add_event_cb(slot->root, product_ui_app_slot_event, LV_EVENT_ALL, slot);
    lv_obj_add_event_cb(slot->root, action, LV_EVENT_CLICKED, user_data);
    return 0;
}

int product_ui_app_slot_bind(product_ui_app_slot_t *slot,
                             const product_ui_app_slot_spec_t *spec)
{
    if ((slot == NULL) || (slot->root == NULL) || (spec == NULL)) {
        return -1;
    }
    if (product_ui_image_fit(slot->icon, spec->image,
                             PRODUCT_UI_HOME_ICON_CANVAS_SIZE,
                             PRODUCT_UI_HOME_ICON_CANVAS_SIZE) != 0) {
        return -1;
    }
    product_ui_app_slot_cancel(slot);
    slot->is_enabled = spec->is_enabled && (spec->title != NULL);
    lv_label_set_text(slot->title, spec->title != NULL ? spec->title : "");
    if (spec->title == NULL) {
        lv_obj_add_flag(slot->title, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(slot->title, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_opa(slot->icon,
                          (spec->title != NULL && !slot->is_enabled) ? LV_OPA_50 : LV_OPA_COVER, 0);
    if (slot->is_enabled) {
        lv_obj_clear_state(slot->root, LV_STATE_DISABLED);
        lv_obj_add_flag(slot->root, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_state(slot->root, LV_STATE_DISABLED);
        lv_obj_clear_flag(slot->root, LV_OBJ_FLAG_CLICKABLE);
    }
    return 0;
}
#endif
