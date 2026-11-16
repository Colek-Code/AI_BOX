/** @file product_ui_overlay.c @brief 拖动驱动的全局控制中心，打开进度、交互资格及模态下拉占用分开。 */

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
/**
 * @brief 查询控制中心非关闭、已有下拉占用或打开手势正在跟踪的综合 UI 忙状态。
 */
uint8_t product_ui_control_center_is_busy(void)
{
    return s_state != PRODUCT_UI_OVERLAY_CLOSED || s_pull_block_count != 0U || s_pull_drag.is_tracking;
}

/**
 * @brief 把控制中心进度限定为零至一千的千分比。
 * @param[in] progress 候选有符号进度。
 * @return 钳制后的进度。
 */
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

/**
 * @brief 应用进度到遮罩点击、状态栏显隐及内容层，变化时请求组合帧。
 * @param[in] progress 候选进度，内部钳制为零至一千。
 */
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

/**
 * @brief 把动画产生的有效千分比值交给浮层进度投影。
 * @param[in] var 动画上下文，当前未使用。
 * @param[in] value 调用链保证零至一千的插值。
 */
static void product_ui_progress_anim_cb(void *var, int32_t value)
{
    (void)var;
    product_ui_overlay_apply_progress((uint16_t)value);
}

/**
 * @brief 完成确认时提交 OPEN 或 CLOSED 终态，打开后立即刷新内容。
 * @param[in] context 动画完成上下文，当前未使用。
 */
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

/**
 * @brief 按剩余距离选择有限吸附时长并记录打开／关闭目标状态。
 * @param[in] target 调用链只传零关闭或一千打开。
 */
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

/**
 * @brief 有有效输入且准入允许时冻结按下坐标、起始进度及中断前目标。
 * @param[in] event 借用按下事件。
 * @param[in,out] drag 全局持久的打开或关闭拖动状态。
 * @param[in] is_opening 非零为下拉打开，零为关闭拖动。
 */
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

/**
 * @brief 以纵向门限及方向捕获手势，再按位移同步进度，反向拖动恢复原目标。
 * @param[in] event 借用移动或松手事件。
 * @param[in,out] drag 持久手势状态。
 * @note 被拒绝的旧关闭拖动可能仍收到 CLICKED，因此单独抑制该旧遮罩点击；下一次按下重新取得资格。
 */
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
            /* LVGL 仍可能为短的被拒拖动生成 CLICKED；只忽略该旧关闭按压，
             * 下一次按下清除此抑制资格。 */
            if (!drag->is_opening) s_scrim_click_blocked = 1U;
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

/**
 * @brief 由按下至最近采样的纵向位移计算平均释放速度。
 * @param[in] drag 有效持久拖动状态。
 * @return 像素每秒，向下为正；没有时间差返回零。
 */
static int32_t product_ui_drag_velocity(const product_ui_drag_t *drag)
{
    uint32_t elapsed;

    elapsed = drag->last_ms - drag->start_ms;
    if (elapsed == 0U) {
        return 0;
    }
    return ((int32_t)(drag->last_y - drag->start_y) * 1000) / (int32_t)elapsed;
}

/**
 * @brief 按完整捕获、进度门限及速度选择终态吸附，并结束跟踪。
 * @param[in,out] drag 有效持久手势状态。
 * @note 吸附首帧前被中断仍可能位于端点，未捕获松手也要恢复原吸附目标。
 */
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
        /* 吸附首帧之前也可能被中断，此时数值仍在端点。 */
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

/**
 * @brief 把按下、移动、释放及丢失／重置交给对应全局手势状态。
 * @param[in] event 对象手势事件，用户数据借用持久 drag。
 */
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

/**
 * @brief 向一个对象追加五类事件监听，共同借用同一持久拖动状态。
 * @param[in,out] obj 有效目标对象。
 * @param[in,out] drag 生命周期覆盖监听的静态拖动状态。
 */
static void product_ui_overlay_bind_drag(lv_obj_t *obj, product_ui_drag_t *drag)
{
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_PRESSED, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_PRESSING, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_RELEASED, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_PRESS_LOST, drag);
    lv_obj_add_event_cb(obj, product_ui_drag_event_cb, LV_EVENT_INDEV_RESET, drag);
}

/**
 * @brief 仅完全打开且可证明命中空白遮罩时关闭控制中心。
 * @param[in] event 遮罩 CLICKED 事件。
 * @note 吸附中、无输入点、命中主动控件或被拒绝拖动后的旧点击都不关闭。
 */
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
    /* 只有完全打开时空白遮罩点击才关闭；吸附中点击不关闭，
     * 此时内容控件也尚未取得交互资格。 */
    if ((s_state != PRODUCT_UI_OVERLAY_OPEN) || (s_progress != 1000U)) {
        PRODUCT_UI_TRACE("panel_scrim action=ignored_not_fully_open state=%u p=%u\n",
                         (unsigned)s_state, s_progress);
        return;
    }
    indev = lv_event_get_indev(event);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    /* 无输入点就无法证明空白命中，保留打开状态。 */
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

/**
 * @brief 在 UI 启动阶段创建全局控制中心、状态栏及内容并绑定吸附动画。
 */
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
    /* 全屏面板 host 保持不可点击，命中仍继续搜索其布局子对象；
     * 空白区域归遮罩处理关闭拖动及点击。刻意清除事件冒泡，
     * 重新启用前须核对控件，尤其滑条拖动必须保持局部所有权。 */
    lv_obj_set_size(s_panel, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    /* 父容器默认裁剪防止半开面板绘出容器边界。 */
    if (product_ui_control_center_content_create(s_panel, &s_content) != 0) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
        s_status = NULL;
        s_panel = NULL;
        return -1;
    }
    /* 打开时真实状态栏覆盖背景图稿中的状态条。 */
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

/**
 * @brief 预约打开或关闭吸附，存在下拉占用时拒绝打开。
 */
void product_ui_control_center_show(uint8_t visible)
{
    if (visible && s_pull_block_count != 0U) return;
    PRODUCT_UI_TRACE("panel_api_show visible=%u progress=%u\n", visible,
                     s_progress);
    product_ui_overlay_snap(visible ? 1000U : 0U);
}

/**
 * @brief 查询控制中心是否到达完全打开终态。
 */
uint8_t product_ui_control_center_is_open(void)
{ return s_state == PRODUCT_UI_OVERLAY_OPEN; }

/**
 * @brief 仅在全局状态明确 OPEN 时读取并刷新服务内容。
 */
void product_ui_control_center_refresh(void)
{
    /* 只有 OPEN 完全打开终态才轮询服务；半开拖动／吸附不取快照，
     * 首轮完整打开由手势完成后的 snap_done 覆盖。 */
    if ((s_overlay == NULL) || (s_state != PRODUCT_UI_OVERLAY_OPEN)) {
        return;
    }
    product_ui_control_center_content_refresh(&s_content);
}

/**
 * @brief 清除打开／关闭拖动与旧遮罩点击资格，并立即收敛为关闭终态。
 */
void product_ui_control_center_close_for_page_transition(void)
{
    memset(&s_pull_drag, 0, sizeof(s_pull_drag));
    memset(&s_close_drag, 0, sizeof(s_close_drag));
    s_scrim_click_blocked = 0U;
    s_state = PRODUCT_UI_OVERLAY_SNAP_CLOSED;
    product_ui_motion_start(&s_snap_motion, 0, 0U);
    PRODUCT_UI_TRACE("panel_close reason=page_transition\n");
}

/**
 * @brief 向指定对象附着全局下拉打开手势监听。
 */
void product_ui_control_center_bind_pull(lv_obj_t *obj)
{
    if (obj != NULL) {
        product_ui_overlay_bind_drag(obj, &s_pull_drag);
    }
}

/**
 * @brief 为一个模态所有者取得独立下拉占用，并立即关闭控制中心。
 */
void product_ui_control_center_hold_pull(uint8_t *is_held)
{
    if (is_held == NULL || *is_held) return;
    *is_held = 1U;
    s_pull_block_count++;
    product_ui_control_center_close_for_page_transition();
}

/**
 * @brief 仅归还本调用方已经取得的下拉占用，不自动重新打开。
 */
void product_ui_control_center_release_pull(uint8_t *is_held)
{
    if (is_held == NULL || !*is_held) return;
    *is_held = 0U;
    if (s_pull_block_count != 0U) s_pull_block_count--;
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
