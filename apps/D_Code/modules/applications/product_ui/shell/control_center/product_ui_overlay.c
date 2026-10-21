/**
 * @file product_ui_overlay.c
 * @brief Drag-driven Product UI control center.
 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/shell/control_center/product_ui_overlay_internal.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "platform/screen/transition/d_page_transition.h"

#define PRODUCT_UI_CAPTURE_PX              PRODUCT_UI_CONTROL_DRAG_CAPTURE
#define PRODUCT_UI_PULL_DISTANCE_PX        PRODUCT_UI_CONTROL_DRAG_RANGE
#define PRODUCT_UI_OPEN_THRESHOLD          PRODUCT_UI_CONTROL_OPEN_AT
#define PRODUCT_UI_CLOSE_THRESHOLD         PRODUCT_UI_CONTROL_CLOSE_AT
#define PRODUCT_UI_FLICK_PX_PER_SEC        PRODUCT_UI_CONTROL_VELOCITY
#define PRODUCT_UI_SNAP_MIN_MS             PRODUCT_UI_MOTION_RELEASE_MS
#define PRODUCT_UI_SNAP_RANGE_MS           \
    (PRODUCT_UI_MOTION_SNAP_MS - PRODUCT_UI_MOTION_RELEASE_MS)

typedef enum {
    PRODUCT_UI_OVERLAY_CLOSED,
    PRODUCT_UI_OVERLAY_DRAG_OPEN,
    PRODUCT_UI_OVERLAY_SNAP_OPEN,
    PRODUCT_UI_OVERLAY_OPEN,
    PRODUCT_UI_OVERLAY_DRAG_CLOSE,
    PRODUCT_UI_OVERLAY_SNAP_CLOSED,
} product_ui_overlay_state_e;

typedef struct {
    int16_t start_x;
    int16_t start_y;
    int16_t last_y;
    uint32_t start_ms;
    uint32_t last_ms;
    uint16_t start_progress;
    uint8_t is_tracking;
    uint8_t is_captured;
    uint8_t is_opening;
    uint8_t resume_open;
} product_ui_drag_t;

static lv_obj_t *s_overlay;
static lv_obj_t *s_status;
static product_ui_status_view_t s_status_view;
static lv_obj_t *s_panel;
static product_ui_control_center_content_t s_content;
static product_ui_overlay_state_e s_state = PRODUCT_UI_OVERLAY_CLOSED;
static product_ui_drag_t s_pull_drag;
static uint16_t s_pull_block_count;
static product_ui_drag_t s_close_drag;
static uint16_t s_progress;
static product_ui_motion_t s_snap_motion;
static uint8_t s_scrim_click_blocked;
uint8_t product_ui_control_center_is_busy(void)
{
    return s_state != PRODUCT_UI_OVERLAY_CLOSED || s_pull_block_count != 0U || s_pull_drag.is_tracking;
}

static uint16_t product_ui_progress_clamp(int32_t progress)
{
    if (progress < 0) {
        return 0;
    }
    if (progress > 1000) {
        return 1000;
    }
    return (uint16_t)progress;
}

static void product_ui_overlay_apply_progress(uint16_t progress)
{
    uint16_t previous_progress = s_progress;

    s_progress = product_ui_progress_clamp(progress);
    if (s_overlay != NULL) {
        if (s_progress == 0U) {
            lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (s_status != NULL) {
        if (s_progress == 0U) {
            lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        }
    }
    product_ui_control_center_content_set_progress(&s_content, s_progress);
    if (s_overlay != NULL && previous_progress != s_progress) {
        d_screen_request_composed_frame();
    }
}

static void product_ui_progress_anim_cb(void *var, int32_t value)
{
    (void)var;
    product_ui_overlay_apply_progress((uint16_t)value);
}

static void product_ui_snap_done_cb(void *context)
{
    (void)context;
    if (s_progress == 1000U) {
        s_state = PRODUCT_UI_OVERLAY_OPEN;
        product_ui_control_center_content_refresh(&s_content);
        PRODUCT_UI_TRACE("panel_snap_done state=open progress=%u\n", s_progress);
    } else {
        s_state = PRODUCT_UI_OVERLAY_CLOSED;
        product_ui_overlay_apply_progress(0);
        PRODUCT_UI_TRACE("panel_snap_done state=closed progress=%u\n", s_progress);
    }
}

static void product_ui_overlay_snap(uint16_t target)
{
    uint16_t distance;
    uint32_t duration;

    distance = (target > s_progress) ? (target - s_progress) : (s_progress - target);
    duration = PRODUCT_UI_SNAP_MIN_MS +
               ((uint32_t)distance * PRODUCT_UI_SNAP_RANGE_MS / 1000U);
    PRODUCT_UI_TRACE("panel_snap from=%u to=%u duration=%u\n",
                     s_progress, target, duration);
    s_state = (target != 0U) ? PRODUCT_UI_OVERLAY_SNAP_OPEN :
                              PRODUCT_UI_OVERLAY_SNAP_CLOSED;
    product_ui_motion_start(&s_snap_motion, target, duration);
}

static void product_ui_drag_begin(lv_event_t *event, product_ui_drag_t *drag,
                                  uint8_t is_opening)
{
    lv_indev_t *indev;
    lv_point_t point;

    if (is_opening && s_pull_block_count != 0U) return;
    indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }
    drag->resume_open = s_state == PRODUCT_UI_OVERLAY_OPEN ||
                        s_state == PRODUCT_UI_OVERLAY_SNAP_OPEN;
    product_ui_motion_cancel(&s_snap_motion, 0U);
    lv_indev_get_point(indev, &point);
    drag->start_x = (int16_t)point.x;
    drag->start_y = (int16_t)point.y;
    drag->last_y = drag->start_y;
    drag->start_ms = lv_tick_get();
    drag->last_ms = drag->start_ms;
    drag->start_progress = s_progress;
    drag->is_tracking = 1;
    drag->is_captured = 0;
    drag->is_opening = is_opening;
    if (!is_opening) {
        s_scrim_click_blocked = 0;
    }
    PRODUCT_UI_TRACE("panel_press kind=%s x=%d y=%d progress=%u state=%u\n",
                     is_opening ? "open" : "close", point.x, point.y,
                     s_progress, s_state);
}

static void product_ui_drag_update(lv_event_t *event, product_ui_drag_t *drag)
{
    lv_indev_t *indev;
    lv_point_t point;
    int16_t dx;
    int16_t dy;
    int32_t progress;

    if (!drag->is_tracking) {
        return;
    }
    indev = lv_event_get_indev(event);
    if (indev == NULL) {
        return;
    }
    lv_indev_get_point(indev, &point);
    dx = (int16_t)(point.x - drag->start_x);
    dy = (int16_t)(point.y - drag->start_y);
    if (!drag->is_captured) {
        if ((LV_ABS(dy) < PRODUCT_UI_CAPTURE_PX) || (LV_ABS(dy) <= LV_ABS(dx))) {
            return;
        }
        if ((drag->is_opening && (dy < 0)) || (!drag->is_opening && (dy > 0))) {
            drag->is_tracking = 0;
            product_ui_overlay_snap(drag->resume_open ? 1000U : 0U);
            PRODUCT_UI_TRACE("panel_cancel kind=%s reason=wrong_direction dx=%d dy=%d\n",
                             drag->is_opening ? "open" : "close", dx, dy);
            return;
        }
        drag->is_captured = 1;
        if (!drag->is_opening) {
            s_scrim_click_blocked = 1;
        }
        s_state = drag->is_opening ? PRODUCT_UI_OVERLAY_DRAG_OPEN :
                                    PRODUCT_UI_OVERLAY_DRAG_CLOSE;
        PRODUCT_UI_TRACE("panel_capture kind=%s dx=%d dy=%d start=%u\n",
                         drag->is_opening ? "open" : "close", dx, dy,
                         drag->start_progress);
    }
    progress = drag->start_progress +
               ((int32_t)dy * 1000 / PRODUCT_UI_PULL_DISTANCE_PX);
    product_ui_motion_set(&s_snap_motion, product_ui_progress_clamp(progress));
    drag->last_y = (int16_t)point.y;
    drag->last_ms = lv_tick_get();
}

static int32_t product_ui_drag_velocity(const product_ui_drag_t *drag)
{
    uint32_t elapsed;

    elapsed = drag->last_ms - drag->start_ms;
    if (elapsed == 0U) {
        return 0;
    }
    return ((int32_t)(drag->last_y - drag->start_y) * 1000) / (int32_t)elapsed;
}

static void product_ui_drag_finish(product_ui_drag_t *drag)
{
    int32_t velocity;
    uint8_t should_open;

    if (!drag->is_tracking) {
        return;
    }
    velocity = product_ui_drag_velocity(drag);
    if (!drag->is_captured) {
        drag->is_tracking = 0;
        /* A snap can be interrupted before its first frame, still at an endpoint. */
        if (s_state == PRODUCT_UI_OVERLAY_SNAP_OPEN ||
            s_state == PRODUCT_UI_OVERLAY_SNAP_CLOSED ||
            (s_progress != 0U && s_progress != 1000U)) {
            product_ui_overlay_snap(drag->resume_open ? 1000U : 0U);
        }
        PRODUCT_UI_TRACE("panel_release kind=%s captured=0 progress=%u\n",
                         drag->is_opening ? "open" : "close", s_progress);
        return;
    }
    if (drag->is_opening) {
        should_open = (s_progress >= PRODUCT_UI_OPEN_THRESHOLD) ||
                      (velocity >= PRODUCT_UI_FLICK_PX_PER_SEC);
    } else {
        should_open = !((s_progress <= PRODUCT_UI_CLOSE_THRESHOLD) ||
                        (velocity <= -PRODUCT_UI_FLICK_PX_PER_SEC));
    }
    PRODUCT_UI_TRACE("panel_release kind=%s captured=1 progress=%u velocity=%d action=%s\n",
                     drag->is_opening ? "open" : "close", s_progress,
                     velocity, should_open ? "open" : "close");
    drag->is_tracking = 0;
    product_ui_overlay_snap(should_open ? 1000U : 0U);
}

static void product_ui_drag_event_cb(lv_event_t *event)
{
    product_ui_drag_t *drag;
    lv_event_code_t code;

    drag = lv_event_get_user_data(event);
    code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        product_ui_drag_begin(event, drag, drag == &s_pull_drag);
    } else if (code == LV_EVENT_PRESSING) {
        product_ui_drag_update(event, drag);
    } else if (code == LV_EVENT_RELEASED) {
        product_ui_drag_update(event, drag);
        product_ui_drag_finish(drag);
    } else if ((code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) && drag->is_tracking) {
        drag->is_tracking = 0U;
        product_ui_overlay_snap(drag->is_opening ? 0U : 1000U);
    }
}

static void product_ui_overlay_bind_drag(lv_obj_t *obj, product_ui_drag_t *drag)
{
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_PRESSED, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_PRESSING, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_RELEASED, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_PRESS_LOST, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_INDEV_RESET, drag);
}

static void product_ui_overlay_click_cb(lv_event_t *event)
{
    lv_indev_t *indev;
    lv_point_t point;

    if ((lv_event_get_code(event) != LV_EVENT_CLICKED) ||
        (lv_event_get_target(event) != s_overlay)) {
        return;
    }
    if (s_scrim_click_blocked) {
        s_scrim_click_blocked = 0;
        PRODUCT_UI_TRACE("panel_scrim action=ignored_after_drag\n");
        return;
    }
    /*
     * Only fully-open sheet may close via empty scrim. Mid-animation taps
     * must not snap closed (controls are not interactive yet either).
     */
    if ((s_state != PRODUCT_UI_OVERLAY_OPEN) || (s_progress != 1000U)) {
        PRODUCT_UI_TRACE("panel_scrim action=ignored_not_fully_open state=%u p=%u\n",
                         (unsigned)s_state, s_progress);
        return;
    }
    indev = lv_event_get_indev(event);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    /* No point: do not close (cannot prove empty scrim vs control hit). */
    if (indev == NULL) {
        PRODUCT_UI_TRACE("panel_scrim action=ignored_no_indev\n");
        return;
    }
    lv_indev_get_point(indev, &point);
    if (product_ui_control_center_content_hit_controls(
            &s_content, (int16_t)point.x, (int16_t)point.y) != 0U) {
        PRODUCT_UI_TRACE("panel_scrim action=ignored_control_hit x=%d y=%d\n",
                         (int)point.x, (int)point.y);
        return;
    }
    PRODUCT_UI_TRACE("panel_scrim action=close\n");
    product_ui_overlay_snap(0);
}

int product_ui_control_center_init(void)
{
    product_ui_motion_binding_t binding = {
        .apply = product_ui_progress_anim_cb,
        .complete = product_ui_snap_done_cb,
    };
    if (s_overlay != NULL) {
        return 0;
    }
    s_overlay = lv_obj_create(lv_layer_top());
    if (s_overlay == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    s_status = product_ui_control_status_create(s_overlay, &s_status_view);
    if (s_status == NULL) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
        return -1;
    }
    s_panel = lv_obj_create(s_overlay);
    if (s_panel == NULL) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
        s_status = NULL;
        return -1;
    }
    lv_obj_remove_style_all(s_panel);
    /*
     * Scheme A full-screen sheet host.
     * Must stay non-CLICKABLE: hit search still walks into layout children,
     * empty areas resolve to s_overlay (close drag + scrim click).
     * EVENT_BUBBLE cleared intentionally — do not re-enable without checking
     * interactive widgets (they clear bubble so slider drag stays local).
     */
    lv_obj_set_size(s_panel, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    /* Default parent clip keeps half-open sheet from painting outside panel. */
    if (product_ui_control_center_content_create(s_panel, &s_content) != 0) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
        s_status = NULL;
        s_panel = NULL;
        return -1;
    }
    /* Live status covers the design-frame status strip when open. */
    lv_obj_move_foreground(s_status);
    product_ui_overlay_bind_drag(s_overlay, &s_close_drag);
    product_ui_overlay_bind_drag(s_status, &s_close_drag);
    lv_obj_add_event_cb(s_overlay, product_ui_overlay_click_cb, LV_EVENT_CLICKED, NULL);
    binding.owner = s_overlay;
    product_ui_motion_bind(&s_snap_motion, &binding);
    product_ui_motion_set(&s_snap_motion, 0);
    PRODUCT_UI_TRACE("panel_init state=closed mode=cc_solid\n");
    return 0;
}

void product_ui_control_center_show(uint8_t visible)
{
    if (visible && s_pull_block_count != 0U) return;
    PRODUCT_UI_TRACE("panel_api_show visible=%u progress=%u\n", visible,
                     s_progress);
    product_ui_overlay_snap(visible ? 1000U : 0U);
}

uint8_t product_ui_control_center_is_open(void)
{ return s_state == PRODUCT_UI_OVERLAY_OPEN; }

void product_ui_control_center_refresh(void)
{
    /*
     * Strict gate: poll only when fully open (OPEN state).
     * Drag/snap half-open must not snapshot services. First full open is
     * covered by snap_done after the opening gesture has finished.
     */
    if ((s_overlay == NULL) || (s_state != PRODUCT_UI_OVERLAY_OPEN)) {
        return;
    }
    product_ui_control_center_content_refresh(&s_content);
}

void product_ui_control_center_close_for_page_transition(void)
{
    memset(&s_pull_drag, 0, sizeof(s_pull_drag));
    memset(&s_close_drag, 0, sizeof(s_close_drag));
    s_scrim_click_blocked = 0U;
    s_state = PRODUCT_UI_OVERLAY_SNAP_CLOSED;
    product_ui_motion_start(&s_snap_motion, 0, 0U);
    PRODUCT_UI_TRACE("panel_close reason=page_transition\n");
}

void product_ui_control_center_bind_pull(lv_obj_t *obj)
{
    if (obj != NULL) {
        product_ui_overlay_bind_drag(obj, &s_pull_drag);
    }
}

void product_ui_control_center_hold_pull(uint8_t *is_held)
{
    if (is_held == NULL || *is_held) return;
    *is_held = 1U;
    s_pull_block_count++;
    product_ui_control_center_close_for_page_transition();
}

void product_ui_control_center_release_pull(uint8_t *is_held)
{
    if (is_held == NULL || !*is_held) return;
    *is_held = 0U;
    if (s_pull_block_count != 0U) s_pull_block_count--;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
