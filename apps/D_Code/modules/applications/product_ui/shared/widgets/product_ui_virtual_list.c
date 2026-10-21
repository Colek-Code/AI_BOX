/**
 * @file product_ui_virtual_list.c
 * @brief Six-slot static virtual list for bounded Product UI collections.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include <limits.h>
#include <string.h>

static void product_ui_virtual_row_event(lv_event_t *event)
{
    product_ui_virtual_list_slot_t *slot = lv_event_get_user_data(event);
    product_ui_virtual_list_t *list;
    lv_event_code_t code = lv_event_get_code(event);

    if ((slot == NULL) || (slot->owner == NULL)) {
        return;
    }
    list = slot->owner;
    if (code == LV_EVENT_PRESSED) {
        slot->pressed_version = slot->binding_version;
        slot->is_armed = 1U;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_DEFOCUSED) {
        slot->is_armed = 0U;
    } else if (code == LV_EVENT_PRESSING && slot->is_armed) {
        lv_indev_t *input = lv_event_get_indev(event);
        lv_point_t point;
        lv_area_t area;

        if (input != NULL) {
            lv_indev_get_point(input, &point);
            lv_obj_get_coords(slot->row, &area);
            if (point.x < area.x1 || point.x > area.x2 ||
                point.y < area.y1 || point.y > area.y2) {
                slot->is_armed = 0U;
            }
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (slot->is_armed && !list->render_pending &&
            slot->interaction != PRODUCT_UI_ROW_INTERACTION_NONE &&
            !lv_obj_has_state(slot->row, LV_STATE_DISABLED) &&
            (slot->pressed_version == slot->binding_version) &&
            (slot->item_index < list->item_count) && (list->action_cb != NULL)) {
            slot->is_armed = 0U;
            list->action_cb(slot->item_index, list->user_data);
        } else {
            slot->is_armed = 0U;
        }
    }
}

static void product_ui_virtual_track_event(lv_event_t *event)
{
    /* The renderer already registers the CLICKED action from its spec. */
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) product_ui_virtual_row_event(event);
}

static int product_ui_virtual_source_get(
    product_ui_virtual_list_t *list, uint16_t item_index, uint8_t slot_index,
    product_ui_row_spec_t *spec)
{
    memset(spec, 0, sizeof(*spec));
    if (list->bind_cb(item_index, slot_index, spec, list->user_data) != 0) {
        return -1;
    }
    spec->is_single_line = list->row_height == PRODUCT_UI_ROW_SINGLE_H;
    if (spec->interaction != PRODUCT_UI_ROW_INTERACTION_NONE) {
        if (list->action_cb == NULL) {
            return -1;
        }
        spec->action_cb = product_ui_virtual_row_event;
        spec->user_data = &list->slots[slot_index];
    }
    return 0;
}

static int product_ui_virtual_slot_bind(product_ui_virtual_list_t *list,
                                        uint8_t slot_index, uint16_t item_index)
{
    product_ui_virtual_list_slot_t *slot = &list->slots[slot_index];
    product_ui_row_spec_t spec;

    if (item_index >= list->item_count) {
        slot->is_armed = 0U;
        slot->binding_version++;
        slot->item_key = 0U;
        slot->item_index = UINT16_MAX;
        if (slot->row != NULL) {
            lv_obj_add_flag(slot->row, LV_OBJ_FLAG_HIDDEN);
        }
        return 0;
    }
    if (product_ui_virtual_source_get(list, item_index, slot_index, &spec) != 0) {
        slot->is_armed = 0U;
        return -1;
    }
    if (spec.item_key == 0U || slot->item_key != spec.item_key || slot->item_index != item_index) {
        slot->is_armed = 0U;
        slot->binding_version++;
    }
    if (slot->row == NULL) {
        slot->owner = list;
        slot->row = list->row_create_cb(list->root, &spec);
        if (slot->row == NULL) {
            return -1;
        }
        lv_obj_add_flag(slot->row, LV_OBJ_FLAG_IGNORE_LAYOUT);
        if (spec.interaction == PRODUCT_UI_ROW_INTERACTION_NONE) {
            lv_obj_add_event_cb(slot->row, product_ui_virtual_row_event, LV_EVENT_CLICKED, slot);
        }
        lv_obj_set_width(slot->row, LV_PCT(100));
        if (list->row_create_cb == product_ui_list_row_create || spec.interaction == PRODUCT_UI_ROW_INTERACTION_NONE)
            lv_obj_add_event_cb(slot->row, product_ui_virtual_track_event, LV_EVENT_ALL, slot);
    } else if (list->row_rebind_cb(slot->row, &spec) != 0) {
        return -1;
    }
    slot->item_index = item_index;
    slot->item_key = spec.item_key;
    slot->interaction = spec.interaction;
    lv_obj_set_pos(slot->row, 0, list->header_height + (int32_t)item_index * list->row_pitch);
    lv_obj_clear_flag(slot->row, LV_OBJ_FLAG_HIDDEN);
    return 0;
}

uint16_t product_ui_virtual_list_first_visible(const product_ui_virtual_list_t *list)
{
    int32_t scroll_y;

    if ((list == NULL) || (list->root == NULL) || (list->row_pitch == 0U)) {
        return 0U;
    }
    scroll_y = lv_obj_get_scroll_y(list->root) - list->header_height;
    return scroll_y > 0 ? (uint16_t)(scroll_y / list->row_pitch) : 0U;
}

static int product_ui_virtual_restore_anchor(product_ui_virtual_list_t *list,
                                             int32_t old_scroll, int32_t old_header)
{
    uint16_t visible;
    uint16_t target;
    uint8_t slot_index;
    uint32_t key = 0U;
    product_ui_row_spec_t spec;

    if (old_scroll < old_header || list->item_count == 0U) {
        return 0;
    }
    visible = (uint16_t)((old_scroll - old_header) / list->row_pitch);
    for (slot_index = 0U; slot_index < PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT; slot_index++) {
        if (list->slots[slot_index].item_index == visible) {
            key = list->slots[slot_index].item_key;
            break;
        }
    }
    if (key == 0U) {
        return 0;
    }
    for (target = 0U; target < list->item_count; target++) {
        memset(&spec, 0, sizeof(spec));
        if (list->bind_cb(target, 0U, &spec, list->user_data) != 0) {
            return -1;
        }
        if (spec.item_key == key) {
            break;
        }
    }
    if (target == list->item_count) {
        target = LV_MIN(visible, list->item_count - 1U);
    }
    lv_obj_scroll_to_y(list->root, list->header_height + (int32_t)target * list->row_pitch +
                        (old_scroll - old_header) % list->row_pitch, LV_ANIM_OFF);
    return 0;
}

static int product_ui_virtual_list_refresh_ex(product_ui_virtual_list_t *list,
                                              uint8_t force)
{
    uint16_t first_visible;
    uint16_t first_bound;
    uint16_t max_first;
    uint8_t slot_index;
    int32_t header_height;
    int32_t extent;
    int32_t old_scroll;
    int32_t old_header;

    if ((list == NULL) || (list->root == NULL) || (list->bind_cb == NULL)) {
        return -1;
    }
    if (list->is_refreshing) {
        return 0;
    }
    list->is_refreshing = 1U;
    old_scroll = lv_obj_get_scroll_y(list->root);
    old_header = list->header_height;
    lv_obj_update_layout(list->root);
    header_height = lv_obj_get_height(list->header);
    if (header_height > 0) {
        header_height += PRODUCT_UI_SPACE_SM;
    }
    if (header_height != list->header_height) {
        list->header_height = header_height;
        force = 1U;
    }
    extent = list->header_height + (int32_t)list->item_count * list->row_pitch;
    if (list->item_count > 0U) {
        extent -= PRODUCT_UI_SPACE_XS;
    }
    lv_obj_set_height(list->extent, LV_MAX(extent, 1));
    if (force) {
        list->render_pending = 1U;
        lv_obj_update_layout(list->root);
        if (product_ui_virtual_restore_anchor(list, old_scroll, old_header) != 0) {
            list->is_refreshing = 0U;
            return -1;
        }
    }
    first_visible = product_ui_virtual_list_first_visible(list);
    first_bound = first_visible > 0U ? (uint16_t)(first_visible - 1U) : 0U;
    max_first = list->item_count > PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT ?
        (uint16_t)(list->item_count - PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) : 0U;
    first_bound = LV_MIN(first_bound, max_first);
    if (!force && !list->render_pending && first_bound == list->first_bound) {
        list->is_refreshing = 0U;
        return 0;
    }
    list->render_pending = 1U;
    for (slot_index = 0U; slot_index < PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT; slot_index++) {
        if (product_ui_virtual_slot_bind(list, slot_index,
                                         (uint16_t)(first_bound + slot_index)) != 0) {
            list->is_refreshing = 0U;
            return -1;
        }
    }
    list->first_bound = first_bound;
    list->render_pending = 0U;
    list->is_refreshing = 0U;
    return 0;
}

int product_ui_virtual_list_refresh(product_ui_virtual_list_t *list)
{
    return product_ui_virtual_list_refresh_ex(list, 1U);
}

int product_ui_virtual_list_set_count(product_ui_virtual_list_t *list, uint16_t count)
{
    if ((list == NULL) || (list->root == NULL)) {
        return -1;
    }
    list->item_count = count;
    list->render_pending = 1U;
    return product_ui_virtual_list_refresh(list);
}

void product_ui_virtual_list_update(product_ui_virtual_list_t *list, uint32_t elapsed_ms)
{
    if ((list == NULL) || (list->root == NULL) || !list->render_pending) {
        return;
    }
    if (elapsed_ms < 100U - list->retry_elapsed_ms) {
        list->retry_elapsed_ms += (uint16_t)elapsed_ms;
        return;
    }
    list->retry_elapsed_ms = 0U;
    (void)product_ui_virtual_list_refresh(list);
}

static void product_ui_virtual_scroll_event(lv_event_t *event)
{
    product_ui_virtual_list_t *list = lv_event_get_user_data(event);
    (void)product_ui_virtual_list_refresh_ex(list, 0U);
}

static void product_ui_virtual_delete_event(lv_event_t *event)
{
    product_ui_virtual_list_t *list = lv_event_get_user_data(event);
    memset(list, 0, sizeof(*list));
}

int product_ui_virtual_list_create(lv_obj_t *parent,
                                      product_ui_virtual_list_t *list,
                                      const product_ui_virtual_list_spec_t *spec)
{
    int32_t width;
    int32_t height;

    if ((parent == NULL) || (list == NULL) || (spec == NULL) ||
        (spec->bind_cb == NULL) ||
        ((spec->row_create_cb == NULL) != (spec->row_rebind_cb == NULL)) ||
        ((spec->row_height != PRODUCT_UI_ROW_SINGLE_H) &&
         (spec->row_height != PRODUCT_UI_ROW_DOUBLE_H))) {
        return -1;
    }
    lv_obj_update_layout(parent);
    width = lv_obj_get_content_width(parent);
    height = lv_obj_get_content_height(parent);
    if ((width <= 0) || (height <= 0)) {
        return -1;
    }
    memset(list, 0, sizeof(*list));
    list->item_count = spec->item_count;
    list->row_height = spec->row_height;
    list->row_pitch = spec->row_height + PRODUCT_UI_SPACE_XS;
    list->bind_cb = spec->bind_cb;
    list->action_cb = spec->action_cb;
    list->user_data = spec->user_data;
    list->row_create_cb = spec->row_create_cb ? spec->row_create_cb : product_ui_list_row_create;
    list->row_rebind_cb = spec->row_rebind_cb ? spec->row_rebind_cb : product_ui_list_row_rebind;
    list->first_bound = UINT16_MAX;
    list->root = lv_obj_create(parent);
    if (list->root == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(list->root);
    lv_obj_set_size(list->root, width, height);
    lv_obj_add_flag(list->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(list->root, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(list->root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list->root, LV_SCROLLBAR_MODE_AUTO);
    list->header = lv_obj_create(list->root);
    list->extent = lv_obj_create(list->root);
    if ((list->header == NULL) || (list->extent == NULL)) {
        lv_obj_delete(list->root);
        memset(list, 0, sizeof(*list));
        return -1;
    }
    lv_obj_remove_style_all(list->header);
    lv_obj_clear_flag(list->header, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(list->header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list->header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list->header, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_remove_style_all(list->extent);
    lv_obj_set_size(list->extent, 1, 1);
    lv_obj_clear_flag(list->extent, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(list->root, product_ui_virtual_scroll_event, LV_EVENT_SCROLL, list);
    lv_obj_add_event_cb(list->root, product_ui_virtual_delete_event, LV_EVENT_DELETE, list);
    list->render_pending = 1U;
    return 0;
}


void product_ui_virtual_list_scroll_to(product_ui_virtual_list_t *list,
                                       uint16_t item_index)
{
    int32_t offset;

    if ((list == NULL) || (list->root == NULL)) {
        return;
    }
    if (product_ui_virtual_list_refresh_ex(list, 0U) != 0) {
        return;
    }
    if ((item_index >= list->item_count) && (list->item_count > 0U)) {
        item_index = (uint16_t)(list->item_count - 1U);
    }
    offset = item_index == 0U ? 0 :
        list->header_height + (int32_t)item_index * list->row_pitch;
    lv_obj_update_layout(list->root);
    lv_obj_scroll_to_y(list->root, offset, LV_ANIM_OFF);
    (void)product_ui_virtual_list_refresh_ex(list, 0U);
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
