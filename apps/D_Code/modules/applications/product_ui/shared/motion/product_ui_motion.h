/** @file product_ui_motion.h @brief UI 执行器中的有主动画及原生动效策略，统一处理取消与终态。 */
#ifndef PRODUCT_UI_MOTION_H
#define PRODUCT_UI_MOTION_H
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "lvgl.h"
#include <stdint.h>
/* 调整渲染或动画定时器节奏时，保持这些墙钟持续时间。 */
#define PRODUCT_UI_MOTION_PRESS_MS   68U
#define PRODUCT_UI_MOTION_RELEASE_MS 102U
#define PRODUCT_UI_MOTION_ENTER_MS   136U
#define PRODUCT_UI_MOTION_SNAP_MS    204U

typedef void (*product_ui_motion_apply_t)(void *context, int32_t value);
typedef void (*product_ui_motion_complete_t)(void *context);
typedef struct {
    lv_obj_t *owner;
    void *context;
    product_ui_motion_apply_t apply;
    product_ui_motion_complete_t complete;
} product_ui_motion_binding_t;
typedef struct product_ui_motion {
    struct product_ui_motion *next;
    product_ui_motion_binding_t binding;
    int32_t current;
    int32_t target;
    uint32_t generation;
    uint8_t is_active;
    uint8_t is_bound;
} product_ui_motion_t;

/**
 * @brief 将动画状态绑定到对象、应用回调及可选完成回调。
 * @param[in,out] motion 调用方持有并初始化为零或已有有效绑定的动画状态。
 * @param[in] binding 本次拷贝的绑定；owner、context 及回调仍由调用方保有生命周期。
 * @note owner 和 apply 必须非空；对象删除会解除绑定，动画状态必须至少存活到解除绑定完成。仅在 UI 执行器使用。
 */
void product_ui_motion_bind(product_ui_motion_t *motion, const product_ui_motion_binding_t *binding);
/**
 * @brief 取消动画并从全局链及对象删除回调中解除绑定。
 * @param[in,out] motion 动画状态，可为 NULL；函数不释放其内存。
 * @note 不调用完成回调；保留最新代次以拒绝迟到的动画回调。
 */
void product_ui_motion_dispose(product_ui_motion_t *motion);
/**
 * @brief 取消在途动画并立即应用一个固定值。
 * @param[in,out] motion 已绑定动画，NULL 或未绑定时忽略。
 * @param[in] value 新的当前值及目标值。
 */
void product_ui_motion_set(product_ui_motion_t *motion, int32_t value);
/**
 * @brief 从当前值向目标启动缓出动画，或直接进入同一终态路径。
 * @param[in,out] motion 已绑定且由调用方保有的动画状态。
 * @param[in] target 目标值。
 * @param[in] duration_ms 持续毫秒数；零表示立即完成。
 * @note 减少动态效果、当前值已等于目标或 LVGL 启动失败时也执行应用及完成回调；替代旧动画时先取消旧代次。
 */
void product_ui_motion_start(product_ui_motion_t *motion, int32_t target, uint32_t duration_ms);
/**
 * @brief 使旧代次失效并删除 LVGL 动画，可选择收敛到终态。
 * @param[in,out] motion 动画状态，可为 NULL。
 * @param[in] finish 非零且此前活跃并已绑定时，应用目标值并调用完成回调。
 * @note finish 为零时保留当前值且不通知完成。
 */
void product_ui_motion_cancel(product_ui_motion_t *motion, uint8_t finish);
/**
 * @brief 读取当前缓存的减少动态效果策略，首次调用时初始化样式。
 * @return 非零表示减少动态效果，零表示普通动效。
 */
uint8_t product_ui_motion_is_reduced(void);
/**
 * @brief 给可见对象树中的可点击及可滚动对象应用当前原生动效策略。
 * @param[in,out] root 借用的根对象；NULL 或隐藏子树不处理。
 */
void product_ui_motion_apply_tree(lv_obj_t *root);
/**
 * @brief 完成对象树内的活跃自研动画并清除按压态，收敛原生动效。
 * @param[in,out] root 待收敛的借用对象树根，NULL 时忽略。
 * @note 完成回调可能改变动画链，因此每完成一个动画后重新查找。
 */
void product_ui_motion_cancel_tree(lv_obj_t *root);
/**
 * @brief 同步外观偏好，按需收敛动画并周期刷新活动屏幕及顶层策略。
 * @param[in] elapsed_ms 本轮经过的毫秒数，每次累计最多 100 毫秒。
 * @note 由 UI 周期串行调用；切换为减少动态效果时先完成活跃动画。
 */
void product_ui_motion_process(uint32_t elapsed_ms);
#endif
#endif
