/** @file product_ui_virtual_list.c @brief 六个固定复用槽的 UI 有限列表，稳定条目身份与可重试呈现分开。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include <limits.h>
#include <string.h>
#if D_DEBUG_UI_PERFORMANCE
#include "framework/compat/d_stdio_compat.h"
#endif

/**
 * @brief 以绑定版本、条目范围、呈现完成及完整点击资格分发当前逻辑动作。
 * @param[in] event 行事件，用户数据借用固定槽。
 * @note 拖出行、输入重置或旧绑定版本撤销资格；render_pending 期间不执行业务动作。
 */
static void product_ui_virtual_row_event(lv_event_t *event)
{
    product_ui_virtual_list_slot_t *slot = lv_event_get_user_data(event);
    product_ui_virtual_list_t *list;
    lv_event_code_t code = lv_event_get_code(event);

    if ((slot == NULL) || (slot->owner == NULL)) {
        return;
    }
    list = slot->owner;
#if D_DEBUG_UI_PERFORMANCE
    {
        static uint16_t trace_events;
        if (trace_events < 512U && (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
            code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED ||
            code == LV_EVENT_INDEV_RESET || code == LV_EVENT_PRESS_LOST)) {
            lv_indev_t *input = lv_indev_active();
            lv_point_t point = {0};
            if (input != NULL) lv_indev_get_point(input, &point);
            trace_events++;
            printf("[UI-ROW] now=%u event=%u item=%u key=%u bound=%u arm=%u ver=%u/%u x=%ld y=%ld scroll=%u\n",
                lv_tick_get(), (unsigned)code, slot->item_index, slot->item_key,
                list->first_bound, slot->is_armed, slot->pressed_version, slot->binding_version,
                (long)point.x, (long)point.y, input != NULL && lv_indev_get_scroll_obj(input) != NULL);
        }
    }
#endif
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

/**
 * @brief 补充跟踪除 CLICKED 之外的行事件，避免重复执行创建器已绑定的点击动作。
 * @param[in] event 共用行监听事件。
 */
static void product_ui_virtual_track_event(lv_event_t *event)
{
    /* 行创建器已按规格注册 CLICKED 动作，此监听只补其他阶段。 */
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) product_ui_virtual_row_event(event);
}

/**
 * @brief 清空候选规格后调用数据绑定器，再注入行高、物理槽缓存及受控动作回调。
 * @param[in] list 有效列表配置。
 * @param[in] item_index 当前逻辑条目下标。
 * @param[in] slot_index 固定槽下标。
 * @param[out] spec 接收候选行规格，失败不得提交。
 * @return 成功返回 0，数据绑定失败或交互缺少列表动作回调返回 -1。
 */
static int product_ui_virtual_source_get(
    product_ui_virtual_list_t *list, uint16_t item_index, uint8_t slot_index,
    product_ui_row_spec_t *spec)
{
    memset(spec, 0, sizeof(*spec));
    if (list->bind_cb(item_index, slot_index, spec, list->user_data) != 0) {
        return -1;
    }
    spec->is_single_line = list->row_height == PRODUCT_UI_ROW_SINGLE_H;
    if (list->text_caches != NULL) spec->text_cache = &list->text_caches[slot_index];
    if (spec->interaction != PRODUCT_UI_ROW_INTERACTION_NONE) {
        if (list->action_cb == NULL) {
            return -1;
        }
        spec->action_cb = product_ui_virtual_row_event;
        spec->user_data = &list->slots[slot_index];
    }
    return 0;
}

/**
 * @brief 为一个物理槽创建或重绑逻辑条目，身份变化时撤销旧按压及文字缓存。
 * @param[in,out] list 正在刷新且由页面持有的列表。
 * @param[in] slot_index 有效固定槽下标。
 * @param[in] item_index 目标逻辑下标。
 * @return 成功或超出总数而隐藏槽返回 0，数据／对象／重绑失败返回 -1。
 * @note 失败保留控件供重试；滚动输入生命周期由 LVGL 持有，旧条目的按压外观不能转移给新条目。
 */
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
        if (list->text_caches != NULL) memset(&list->text_caches[slot_index], 0,
                                             sizeof(list->text_caches[slot_index]));
        if (slot->row != NULL) {
            lv_obj_remove_state(slot->row, LV_STATE_PRESSED);
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
        if (spec.text_cache != NULL) memset(spec.text_cache, 0, sizeof(*spec.text_cache));
        /* 按压属于旧条目，滚动输入仍有独立生命周期；
         * 不能把旧按压外观转移给新条目。 */
        if (slot->row != NULL) lv_obj_remove_state(slot->row, LV_STATE_PRESSED);
    }
    if (slot->row == NULL) {
        slot->owner = list;
        if (spec.text_cache != NULL) memset(spec.text_cache, 0, sizeof(*spec.text_cache));
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
        /* 标准行按最终视口宽度建立文字缓存，自定义创建器自行拥有初始绑定；
         * 失败保留当前控件，通过既有 pending 呈现路径重试。 */
        if (list->row_create_cb == product_ui_list_row_create &&
            list->row_rebind_cb(slot->row, &spec) != 0) return -1;
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

/**
 * @brief 按滚动像素、页头及行间距计算首个可见逻辑下标。
 */
uint16_t product_ui_virtual_list_first_visible(const product_ui_virtual_list_t *list)
{
    int32_t scroll_y;

    if ((list == NULL) || (list->root == NULL) || (list->row_pitch == 0U)) {
        return 0U;
    }
    scroll_y = lv_obj_get_scroll_y(list->root) - list->header_height;
    return scroll_y > 0 ? (uint16_t)(scroll_y / list->row_pitch) : 0U;
}

/**
 * @brief 以先前可见条目键寻找新逻辑位置，找不到时回退同下标并保留行内偏移。
 * @param[in,out] list 使用新数量及页头尺寸的列表。
 * @param[in] old_scroll 刷新前纵向像素偏移。
 * @param[in] old_header 刷新前页头高度。
 * @return 恢复或无需恢复返回 0，扫描数据绑定失败返回 -1。
 * @note 可能遍历逻辑条目调用 bind_cb 读取键，不应将数据绑定回调解释为一次性可见操作。
 */
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

/**
 * @brief 重算滚动范围及窗口，尽量保留重叠条目控件，再按槽逐项提交。
 * @param[in,out] list 正在使用的列表。
 * @param[in] force 非零强制重绑；零允许复用仍重叠且身份有效的槽。
 * @return 完整刷新、可复用跳过或重入保护为 0；参数及绑定失败为 -1。
 * @note 只有全部槽成功后才清 render_pending；强制刷新或已有失败期间不执行部分控件的业务动作。
 */
static int product_ui_virtual_list_refresh_ex(product_ui_virtual_list_t *list,
                                              uint8_t force)
{
    uint16_t first_visible;
    uint16_t first_bound;
    uint16_t max_first;
    uint8_t slot_index;
    uint8_t window_index;
    uint8_t can_reuse;
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
    can_reuse = !force && !list->render_pending;
#if D_DEBUG_UI_PERFORMANCE
    {
        static uint16_t trace_refreshes;
        if (trace_refreshes < 64U) {
            trace_refreshes++;
            printf("[UI-LIST] now=%u force=%u pending=%u first=%u/%u\n",
                lv_tick_get(), force, list->render_pending, list->first_bound, first_bound);
        }
    }
#endif
    list->render_pending = 1U;
    for (window_index = 0U; window_index < PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT; window_index++) {
        uint16_t item_index = (uint16_t)(first_bound + window_index);
        slot_index = (uint8_t)(item_index % PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT);
        /* 保留重叠条目控件；逐位置回收会把按住的行换成禁用条目，
         * 从而在拖动中途触发 LVGL 输入重置。 */
        if (can_reuse && item_index < list->item_count && list->slots[slot_index].row != NULL &&
            list->slots[slot_index].item_index == item_index && list->slots[slot_index].item_key != 0U &&
            !lv_obj_has_flag(list->slots[slot_index].row, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        if (product_ui_virtual_slot_bind(list, slot_index, item_index) != 0) {
            list->is_refreshing = 0U;
            return -1;
        }
    }
    list->first_bound = first_bound;
    list->render_pending = 0U;
    list->is_refreshing = 0U;
    return 0;
}

/**
 * @brief 强制重绑当前有限窗口，并尽量按稳定条目键恢复视口锚点。
 */
int product_ui_virtual_list_refresh(product_ui_virtual_list_t *list)
{
    return product_ui_virtual_list_refresh_ex(list, 1U);
}

/**
 * @brief 立即更新逻辑数量并强制刷新当前窗口。
 */
int product_ui_virtual_list_set_count(product_ui_virtual_list_t *list, uint16_t count)
{
    if ((list == NULL) || (list->root == NULL)) {
        return -1;
    }
    list->item_count = count;
    list->render_pending = 1U;
    return product_ui_virtual_list_refresh(list);
}

/**
 * @brief 仅在 render_pending 且累计间隔达到一百毫秒时重试强制刷新。
 */
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

/**
 * @brief 滚动时按新视口更新有限窗口，允许保留重叠控件。
 * @param[in] event 列表根滚动事件，用户数据借用列表。
 */
static void product_ui_virtual_scroll_event(lv_event_t *event)
{
    product_ui_virtual_list_t *list = lv_event_get_user_data(event);
    (void)product_ui_virtual_list_refresh_ex(list, 0U);
}

/**
 * @brief 根对象删除时清零调用方列表状态，撤销所有对象及回调借用引用。
 * @param[in] event 列表根删除事件，状态内存须仍有效。
 */
static void product_ui_virtual_delete_event(lv_event_t *event)
{
    product_ui_virtual_list_t *list = lv_event_get_user_data(event);
    memset(list, 0, sizeof(*list));
}

/**
 * @brief 创建六槽复用列表的根、页头及滚动占位，首轮内容留待 refresh。
 */
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
    list->text_caches = spec->text_caches;
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


/**
 * @brief 把指定逻辑行滚入视图，首项回到零偏移并刷新复用窗口。
 */
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
