/** @file product_ui_motion.c @brief UI 执行器中的有主动画及原生动效策略，统一处理取消与终态。 */
#include "product_ui_motion.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"
#include <string.h>

/* 这些对象标志由 Product UI 原生动效策略专用。 */
#define MOTION_POLICY_BOUND     LV_OBJ_FLAG_USER_1
#define MOTION_HAD_MOMENTUM     LV_OBJ_FLAG_USER_2
#define MOTION_HAD_ELASTIC      LV_OBJ_FLAG_USER_3
#define MOTION_POLICY_REDUCED   LV_OBJ_FLAG_USER_4

static product_ui_motion_t *s_motions;
static uint8_t s_initialized;
static uint8_t s_reduced;
static uint32_t s_policy_elapsed;
static uint32_t s_generation;
static lv_style_t s_release_style, s_press_style, s_reduced_style;
static lv_style_transition_dsc_t s_release_transition, s_press_transition, s_no_transition;

/**
 * @brief 一次性初始化动效样式和外观策略缓存。
 */
static void product_ui_motion_init(void)
{
    static const lv_style_prop_t properties[] = {LV_STYLE_BG_COLOR, LV_STYLE_TEXT_OPA, 0};
    product_ui_appearance_snapshot_t appearance;
    if (s_initialized) return;
    product_ui_appearance_get_snapshot(&appearance);
    s_reduced = appearance.reduce_motion;
    lv_style_transition_dsc_init(&s_release_transition, properties, lv_anim_path_ease_out, PRODUCT_UI_MOTION_RELEASE_MS, 0, NULL);
    lv_style_transition_dsc_init(&s_press_transition, properties, lv_anim_path_ease_out, PRODUCT_UI_MOTION_PRESS_MS, 0, NULL);
    lv_style_transition_dsc_init(&s_no_transition, properties, lv_anim_path_linear, 0, 0, NULL);
    lv_style_init(&s_release_style);
    lv_style_init(&s_press_style);
    lv_style_init(&s_reduced_style);
    lv_style_set_transition(&s_release_style, &s_release_transition);
    lv_style_set_transition(&s_press_style, &s_press_transition);
    lv_style_set_transition(&s_reduced_style, &s_no_transition);
    s_initialized = 1U;
}

/**
 * @brief 读取当前缓存的减少动态效果策略，首次调用时初始化样式。
 */
uint8_t product_ui_motion_is_reduced(void)
{
    product_ui_motion_init();
    return s_reduced;
}

/**
 * @brief 应用目标终值，并在绑定和代次仍有效时通知完成。
 * @param[in,out] motion 调用方保证非空的动画状态。
 * @note 应用回调可重入并改变绑定或代次，完成通知前重新检查。
 */
static void product_ui_motion_finish(product_ui_motion_t *motion)
{
    uint32_t generation = motion->generation;
    motion->is_active = 0U;
    motion->current = motion->target;
    if (motion->binding.apply != NULL) motion->binding.apply(motion->binding.context, motion->current);
    if (motion->is_bound && motion->generation == generation && motion->binding.complete != NULL) {
        motion->binding.complete(motion->binding.context);
    }
}

/**
 * @brief 仅为当前有效代次更新插值并调用应用回调。
 * @param[in] animation LVGL 借用的动画描述，var 指向绑定状态。
 */
static void product_ui_motion_exec(lv_anim_t *animation, int32_t value)
{
    product_ui_motion_t *motion = animation->var;
    if (!motion->is_bound || !motion->is_active ||
        motion->generation != (uint32_t)(uintptr_t)lv_anim_get_user_data(animation)) return;
    motion->current = value;
    motion->binding.apply(motion->binding.context, value);
}

/**
 * @brief 过滤迟到的完成事件后进入统一终态路径。
 * @param[in] animation LVGL 借用的动画描述，其用户数据携带代次。
 */
static void product_ui_motion_completed(lv_anim_t *animation)
{
    product_ui_motion_t *motion = animation->var;
    if (motion->is_bound && motion->is_active &&
        motion->generation == (uint32_t)(uintptr_t)lv_anim_get_user_data(animation)) {
        product_ui_motion_finish(motion);
    }
}

/**
 * @brief 使旧代次失效并删除 LVGL 动画，可选择收敛到终态。
 */
void product_ui_motion_cancel(product_ui_motion_t *motion, uint8_t finish)
{
    uint8_t was_active;
    if (motion == NULL) return;
    was_active = motion->is_active;
    s_generation++;
    if (s_generation == 0U) s_generation = 1U;
    motion->generation = s_generation;
    motion->is_active = 0U;
    lv_anim_delete(motion, NULL);
    if (finish && was_active && motion->is_bound) product_ui_motion_finish(motion);
}

/**
 * @brief 所有者对象删除时解除对应动画绑定。
 * @param[in] event LVGL 删除事件，用户数据指向动画状态。
 */
static void product_ui_motion_deleted(lv_event_t *event)
{
    product_ui_motion_t *motion = lv_event_get_user_data(event);
    if (lv_event_get_target(event) != motion->binding.owner) return;
    motion->binding.owner = NULL;
    product_ui_motion_dispose(motion);
}

/**
 * @brief 取消动画并从全局链及对象删除回调中解除绑定。
 */
void product_ui_motion_dispose(product_ui_motion_t *motion)
{
    product_ui_motion_t **entry;
    uint32_t generation;
    if (motion == NULL) return;
    product_ui_motion_cancel(motion, 0U);
    generation = motion->generation;
    for (entry = &s_motions; *entry != NULL; entry = &(*entry)->next) {
        if (*entry == motion) {
            *entry = motion->next;
            break;
        }
    }
    if (motion->is_bound && motion->binding.owner != NULL) {
        lv_obj_remove_event_cb(motion->binding.owner, product_ui_motion_deleted);
    }
    memset(motion, 0, sizeof(*motion));
    motion->generation = generation;
}

/**
 * @brief 将动画状态绑定到对象、应用回调及可选完成回调。
 */
void product_ui_motion_bind(product_ui_motion_t *motion, const product_ui_motion_binding_t *binding)
{
    if (motion == NULL || binding == NULL || binding->owner == NULL || binding->apply == NULL) return;
    product_ui_motion_init();
    product_ui_motion_dispose(motion);
    motion->binding = *binding;
    motion->next = s_motions;
    motion->is_bound = 1U;
    s_motions = motion;
    lv_obj_add_event_cb(binding->owner, product_ui_motion_deleted, LV_EVENT_DELETE, motion);
}

/**
 * @brief 取消在途动画并立即应用一个固定值。
 */
void product_ui_motion_set(product_ui_motion_t *motion, int32_t value)
{
    if (motion == NULL || !motion->is_bound) return;
    product_ui_motion_cancel(motion, 0U);
    motion->current = motion->target = value;
    motion->binding.apply(motion->binding.context, value);
}

/**
 * @brief 从当前值向目标启动缓出动画，或直接进入同一终态路径。
 */
void product_ui_motion_start(product_ui_motion_t *motion, int32_t target, uint32_t duration_ms)
{
    lv_anim_t animation;
    if (motion == NULL || !motion->is_bound) return;
    product_ui_motion_cancel(motion, 0U);
    motion->target = target;
    motion->is_active = 1U;
    if (s_reduced || duration_ms == 0U || motion->current == target) {
        product_ui_motion_finish(motion);
        return;
    }
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, motion);
    lv_anim_set_user_data(&animation, (void *)(uintptr_t)motion->generation);
    lv_anim_set_values(&animation, motion->current, target);
    lv_anim_set_duration(&animation, duration_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_custom_exec_cb(&animation, product_ui_motion_exec);
    lv_anim_set_completed_cb(&animation, product_ui_motion_completed);
    if (lv_anim_start(&animation) == NULL) product_ui_motion_finish(motion);
}

/**
 * @brief 删除开关原生动画并显式应用终值和完成通知。
 * @param[in,out] object 借用的 LVGL 开关对象。
 * @note 仅删除动画会使 LVGL 9 的开关滑块停在中间位置，因此先复制描述再执行终态回调。
 */
static void product_ui_motion_finish_switch(lv_obj_t *object)
{
    lv_anim_t *running = lv_anim_get(object, NULL);
    if (running != NULL) {
        lv_anim_t terminal = *running;
        /* 仅删除动画会把 LVGL 9 开关滑块留在中间坐标，必须显式应用终值。 */
        lv_anim_delete(object, NULL);
        if (terminal.exec_cb != NULL) terminal.exec_cb(terminal.var, terminal.end_value);
        if (terminal.completed_cb != NULL) terminal.completed_cb(&terminal);
    }
}

/**
 * @brief 保存原始滚动标志并按策略切换对象过渡样式及开关动画。
 * @param[in,out] object 待配置的借用对象。
 * @param[in] reduced 非零关闭惯性、弹性及过渡；零恢复原先存在的滚动能力。
 */
static void product_ui_motion_native_policy(lv_obj_t *object, uint8_t reduced)
{
    uint8_t was_reduced = lv_obj_has_flag(object, MOTION_POLICY_REDUCED);
    uint8_t is_bound = lv_obj_has_flag(object, MOTION_POLICY_BOUND);
    const lv_style_t *release = reduced ? &s_reduced_style : &s_release_style;
    const lv_style_t *press = reduced ? &s_reduced_style : &s_press_style;
    if (is_bound && reduced == was_reduced) return;
    if (!is_bound) {
        if (lv_obj_has_flag(object, LV_OBJ_FLAG_SCROLL_MOMENTUM)) lv_obj_add_flag(object, MOTION_HAD_MOMENTUM);
        if (lv_obj_has_flag(object, LV_OBJ_FLAG_SCROLL_ELASTIC)) lv_obj_add_flag(object, MOTION_HAD_ELASTIC);
        lv_obj_add_flag(object, MOTION_POLICY_BOUND);
        lv_obj_add_style(object, release, 0);
        lv_obj_add_style(object, press, LV_STATE_PRESSED);
    } else {
        /* 替换过渡样式同时清除正在执行的原生样式过渡。 */
        lv_obj_replace_style(object, was_reduced ? &s_reduced_style : &s_release_style, release, 0);
        lv_obj_replace_style(object, was_reduced ? &s_reduced_style : &s_press_style, press, LV_STATE_PRESSED);
    }
    if (reduced) {
        lv_obj_add_flag(object, MOTION_POLICY_REDUCED);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
    } else {
        lv_obj_remove_flag(object, MOTION_POLICY_REDUCED);
        if (lv_obj_has_flag(object, MOTION_HAD_MOMENTUM)) lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        if (lv_obj_has_flag(object, MOTION_HAD_ELASTIC)) lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLL_ELASTIC);
    }
    if (lv_obj_check_type(object, &lv_switch_class)) {
        if (reduced) product_ui_motion_finish_switch(object);
        lv_obj_set_style_anim_duration(object, reduced ? 0U : PRODUCT_UI_MOTION_ENTER_MS, 0);
    }
}

/**
 * @brief 递归处理可见对象树的动效策略及可选按压态清除。
 * @param[in,out] root 根对象；NULL 或隐藏时跳过该子树。
 * @param[in] reduced 本次应用的减少动态效果开关。
 * @param[in] cancel_press 非零时清除可交互对象的按压态。
 */
static void product_ui_motion_policy_tree(lv_obj_t *root, uint8_t reduced, uint8_t cancel_press)
{
    uint32_t index;
    if (root == NULL || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_CLICKABLE) || lv_obj_has_flag(root, LV_OBJ_FLAG_SCROLLABLE)) {
        product_ui_motion_native_policy(root, reduced);
        if (cancel_press) lv_obj_remove_state(root, LV_STATE_PRESSED);
    }
    for (index = 0U; index < lv_obj_get_child_count(root); index++) {
        product_ui_motion_policy_tree(lv_obj_get_child(root, (int32_t)index), reduced, cancel_press);
    }
}

/**
 * @brief 给可见对象树中的可点击及可滚动对象应用当前原生动效策略。
 */
void product_ui_motion_apply_tree(lv_obj_t *root)
{
    product_ui_motion_init();
    product_ui_motion_policy_tree(root, s_reduced, 0U);
}

/**
 * @brief 沿所有者的父链判断动画是否属于给定对象树。
 * @param[in] motion 已绑定的动画状态。
 * @param[in] root 待匹配的借用对象树根。
 * @return 所有者或其祖先等于 root 返回非零，否则返回零。
 */
static uint8_t product_ui_motion_in_tree(const product_ui_motion_t *motion, lv_obj_t *root)
{
    lv_obj_t *owner = motion->binding.owner;
    while (owner != NULL) {
        if (owner == root) return 1U;
        owner = lv_obj_get_parent(owner);
    }
    return 0U;
}

/**
 * @brief 完成对象树内的活跃自研动画并清除按压态，收敛原生动效。
 */
void product_ui_motion_cancel_tree(lv_obj_t *root)
{
    product_ui_motion_t *motion;
    if (root == NULL) return;
    for (;;) {
        for (motion = s_motions; motion != NULL; motion = motion->next) {
            if (motion->is_active && product_ui_motion_in_tree(motion, root)) break;
        }
        if (motion == NULL) break;
        product_ui_motion_cancel(motion, 1U);
    }
    product_ui_motion_policy_tree(root, 1U, 1U);
}

/**
 * @brief 同步外观偏好，按需收敛动画并周期刷新活动屏幕及顶层策略。
 */
void product_ui_motion_process(uint32_t elapsed_ms)
{
    product_ui_appearance_snapshot_t appearance;
    product_ui_motion_t *motion;
    uint8_t changed;
    product_ui_motion_init();
    product_ui_appearance_get_snapshot(&appearance);
    changed = s_reduced != appearance.reduce_motion;
    s_reduced = appearance.reduce_motion;
    if (changed) {
        if (s_reduced) {
            for (;;) {
                for (motion = s_motions; motion != NULL && !motion->is_active; motion = motion->next) {}
                if (motion == NULL) break;
                product_ui_motion_cancel(motion, 1U);
            }
        }
        for (motion = s_motions; motion != NULL; motion = motion->next) {
            if (!motion->is_active) motion->binding.apply(motion->binding.context, motion->current);
        }
    }
    s_policy_elapsed += LV_MIN(elapsed_ms, 100U);
    if (changed || s_policy_elapsed >= 100U) {
        s_policy_elapsed = 0U;
        product_ui_motion_apply_tree(lv_screen_active());
        product_ui_motion_apply_tree(lv_layer_top());
    }
}
#endif
