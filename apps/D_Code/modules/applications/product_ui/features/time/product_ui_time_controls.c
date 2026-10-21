/**
 * @file product_ui_time_controls.c
 * @brief Reusable Product UI time controls; PRESS_LOST never commits a roller.
 * @author D_Code
 * @date 2026-09-22
 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_time_controls.h"
#include "platform/screen/transition/d_page_transition.h"

const char product_ui_time_hours[] = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
const char product_ui_time_minutes[] = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59";
const char product_ui_time_months[] = "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12";
const char product_ui_time_years[] = "2024\n2025\n2026\n2027\n2028\n2029\n2030\n2031\n2032\n2033\n2034\n2035\n2036\n2037\n2038\n2039\n2040\n2041\n2042\n2043\n2044\n2045\n2046\n2047\n2048\n2049\n2050\n2051\n2052\n2053\n2054\n2055\n2056\n2057\n2058\n2059\n2060\n2061\n2062\n2063\n2064\n2065\n2066\n2067\n2068\n2069\n2070\n2071\n2072\n2073\n2074\n2075\n2076\n2077\n2078\n2079\n2080\n2081\n2082\n2083\n2084\n2085\n2086\n2087\n2088\n2089\n2090\n2091\n2092\n2093\n2094\n2095\n2096\n2097\n2098\n2099";
static const char *const s_day_options[] = {
    "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28",
    "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29",
    "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30",
    "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31"
};

const char *product_ui_time_day_options(uint8_t count)
{ return count >= 28U && count <= 31U ? s_day_options[count - 28U] : s_day_options[3]; }

void product_ui_time_control_clean(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
        LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
}

lv_obj_t *product_ui_time_label(lv_obj_t *parent, const char *text, product_ui_style_e style)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label != NULL) {
        lv_obj_add_style(label, product_ui_style(style), 0);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_label_set_text_static(label, text);
    }
    return label;
}

static lv_obj_t *s_button_pressed;
static lv_point_t s_button_origin;
static uint8_t s_button_clickable;

static const lv_style_prop_t s_button_geometry_props[] = {LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_ALIGN};
static const lv_style_const_prop_t s_button_geometry[] = {
    {&s_button_geometry_props[0], {.num = 92}},
    {&s_button_geometry_props[1], {.num = PRODUCT_UI_TOUCH_MIN}}, LV_STYLE_CONST_PROPS_END
};
static const lv_style_const_prop_t s_button_label_geometry[] = {
    {&s_button_geometry_props[2], {.num = LV_ALIGN_CENTER}}, LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_button_geometry_style, (void *)s_button_geometry);
static LV_STYLE_CONST_INIT(s_button_label_style, (void *)s_button_label_geometry);

static const lv_style_prop_t s_roller_geometry_properties[] = {
    LV_STYLE_WIDTH, LV_STYLE_BG_OPA, LV_STYLE_RADIUS, LV_STYLE_TEXT_LINE_SPACE, LV_STYLE_ANIM_DURATION,
};
static const lv_style_const_prop_t s_roller_geometry[] = {
    {&s_roller_geometry_properties[0], {.num = 88}},
    {&s_roller_geometry_properties[1], {.num = LV_OPA_COVER}},
    {&s_roller_geometry_properties[2], {.num = 8}},
    {&s_roller_geometry_properties[3], {.num = 10}},
    {&s_roller_geometry_properties[4], {.num = 0}}, LV_STYLE_CONST_PROPS_END
};
static LV_STYLE_CONST_INIT(s_roller_geometry_style, (void *)s_roller_geometry);

static void product_ui_time_button_guard(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *input = lv_indev_active();
    lv_point_t point;
    if (code == LV_EVENT_PRESSED) {
        s_button_pressed = object; s_button_clickable = input != NULL;
        if (input) lv_indev_get_point(input, &s_button_origin);
    } else if (code == LV_EVENT_PRESSING && s_button_pressed == object && input) {
        lv_indev_get_point(input, &point);
        if (LV_ABS(point.x - s_button_origin.x) > 8 || LV_ABS(point.y - s_button_origin.y) > 8) s_button_clickable = 0U;
    } else if (code == LV_EVENT_INDEV_RESET || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DEFOCUSED || code == LV_EVENT_DELETE) {
        if (s_button_pressed == object) { s_button_pressed = NULL; s_button_clickable = 0U; }
    } else if (code == LV_EVENT_CLICKED) {
        uint8_t accepted = s_button_pressed == object && s_button_clickable;
        s_button_pressed = NULL; s_button_clickable = 0U;
        if (!accepted) lv_event_stop_processing(event);
    }
}

lv_obj_t *product_ui_time_button(lv_obj_t *parent, const char *text, product_ui_action_cb_t callback, void *context)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    if (button == NULL) return NULL;
    product_ui_time_control_clean(button);
    /* Retain the initial target until its parent captures scrolling. The guard
     * rejects movement instead of losing the scroll when crossing a child edge. */
    lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_style(button, &s_button_geometry_style, 0);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_style(button, product_ui_style(PRODUCT_UI_STYLE_DISABLED), LV_STATE_DISABLED);
    label = product_ui_time_label(button, text, PRODUCT_UI_STYLE_BODY);
    if (label == NULL) { lv_obj_delete(button); return NULL; }
    lv_obj_add_style(label, &s_button_label_style, 0);
    lv_obj_add_event_cb(button, product_ui_time_button_guard, LV_EVENT_ALL, NULL);
    if (callback != NULL) lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, context);
    return button;
}

void product_ui_time_roller_cancel(product_ui_time_roller_t *binding)
{
    if (binding == NULL || !binding->armed) return;
    binding->armed = 0U;
    if (binding->object != NULL) {
        lv_roller_set_selected(binding->object, binding->original, LV_ANIM_OFF);
        d_screen_request_composed_frame();
    }
}

static void product_ui_time_roller_event(lv_event_t *event)
{
    product_ui_time_roller_t *binding = lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (binding == NULL || binding->object != target) return;
    if (code == LV_EVENT_PRESSED) {
        binding->original = lv_roller_get_selected(target);
        binding->pressed_generation = binding->generation;
        binding->armed = 1U;
    } else if (code == LV_EVENT_PRESSING && binding->armed) {
        lv_indev_t *input = lv_indev_active();
        lv_point_t motion;
        if (input != NULL) {
            lv_indev_get_vect(input, &motion);
            if (motion.y != 0) d_screen_request_composed_frame();
        }
    } else if (code == LV_EVENT_RELEASED) {
        uint8_t accepted = binding->armed && binding->generation == binding->pressed_generation;
        binding->armed = 0U;
        if (accepted) {
            if (binding->changed != NULL) binding->changed(binding, lv_roller_get_selected(target));
            d_screen_request_composed_frame();
        }
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DEFOCUSED || code == LV_EVENT_INDEV_RESET) {
        product_ui_time_roller_cancel(binding);
    } else if (code == LV_EVENT_DELETE) {
        binding->object = NULL;
        binding->armed = 0U;
    }
    /* LVGL also emits VALUE_CHANGED from PRESS_LOST. Only RELEASED commits. */
}

lv_obj_t *product_ui_time_roller_create(lv_obj_t *parent, const char *options, product_ui_time_roller_t *binding)
{
    lv_obj_t *roller = lv_roller_create(parent);
    if (roller == NULL || options == NULL || binding == NULL) {
        if (roller != NULL) lv_obj_delete(roller);
        return NULL;
    }
    product_ui_time_control_clean(roller);
    /* A roller owns the full drag, including motion beyond its visible rows.
     * Losing the target at the edge emits PRESS_LOST and restores the old value. */
    lv_obj_add_flag(roller, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_style(roller, product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
    lv_obj_add_style(roller, &s_roller_geometry_style, 0);
    lv_obj_set_style_bg_color(roller, product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT), 0);
    lv_obj_set_style_bg_color(roller, product_ui_color(PRODUCT_UI_COLOR_ACCENT), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, product_ui_color(PRODUCT_UI_COLOR_ON_PRIMARY), LV_PART_SELECTED);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3U);
    lv_obj_add_event_cb(roller, product_ui_time_roller_event, LV_EVENT_ALL, binding);
    return roller;
}

void product_ui_time_roller_bind(product_ui_time_roller_t *binding, lv_obj_t *object, uint16_t selected, uint32_t generation)
{
    if (binding == NULL || object == NULL) return;
    if (binding->object != object || binding->generation != generation) product_ui_time_roller_cancel(binding);
    binding->object = object;
    binding->generation = generation;
    if (!binding->armed) lv_roller_set_selected(object, selected, LV_ANIM_OFF);
}
#endif
