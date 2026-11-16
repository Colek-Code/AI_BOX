/** @file product_ui_pomodoro_view.h @brief 三个有限番茄钟视图及稳定动作身份，结构替换失败保留旧视图。 */
#ifndef PRODUCT_UI_POMODORO_VIEW_H
#define PRODUCT_UI_POMODORO_VIEW_H
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "product_ui_pomodoro_service.h"
typedef enum { PRODUCT_UI_POMO_TIMER, PRODUCT_UI_POMO_SETTINGS, PRODUCT_UI_POMO_STATS } product_ui_pomo_mode_e;
typedef enum {
    PRODUCT_UI_POMO_NONE, PRODUCT_UI_POMO_PRIMARY, PRODUCT_UI_POMO_SKIP, PRODUCT_UI_POMO_END,
    PRODUCT_UI_POMO_SETTINGS_OPEN, PRODUCT_UI_POMO_STATS_OPEN, PRODUCT_UI_POMO_BACK,
    PRODUCT_UI_POMO_SAVE, PRODUCT_UI_POMO_CLEAR, PRODUCT_UI_POMO_RETRY,
    PRODUCT_UI_POMO_DECREASE, PRODUCT_UI_POMO_INCREASE, PRODUCT_UI_POMO_TOGGLE,
    PRODUCT_UI_POMO_SET_DURATION,
} product_ui_pomo_action_e;
typedef void (*product_ui_pomo_action_fn)(product_ui_pomo_action_e action, uint8_t field,
    uint8_t value, uint32_t generation);
/**
 * @brief 在候选树完整创建后才替换当前子视图，失败保留原视图。
 * @param[in,out] parent 子视图父容器。
 * @param[in] mode 调用方提供的合法 TIMER／SETTINGS／STATS 枚举。
 * @param[in] snapshot 本次借用的服务快照，初次刷新使用其设置。
 * @param[in] action 必须非空且在视图寿命内有效的动作回调。
 * @return 成功返回 0；必要参数为空、模式超过 STATS 或任一步创建失败返回 -1。
 * @note 仅 UI 线程使用；不保存输入快照指针，静态回调及呈现状态只支持当前单个视图。
 */
int product_ui_pomodoro_view_show(lv_obj_t *parent, product_ui_pomo_mode_e mode,
    const product_ui_pomodoro_snapshot_t *snapshot, product_ui_pomo_action_fn action);
/**
 * @brief 按服务快照及控制器草稿刷新已建视图，模型代次变化撤销旧输入。
 * @param[in] snapshot 本次借用的最新服务值。
 * @param[in] draft 本页控制器拥有的设置草稿。
 * @note 活跃时长滑条不被后台刷新覆盖；此接口只呈现，不修改服务、统计或草稿。
 */
void product_ui_pomodoro_view_refresh(const product_ui_pomodoro_snapshot_t *snapshot,
    const product_ui_pomodoro_settings_t *draft);
/**
 * @brief 取消预览手势、删除当前视图树并撤销动作回调及文字缓存。
 */
void product_ui_pomodoro_view_destroy(void);
/**
 * @brief 创建共用按压校验的窄导航按钮。
 * @param[in,out] parent 导航按钮父容器。
 * @param[in] text 本次复制到标签的文字。
 * @param[in] action 导航动作编号。
 * @return 成功返回父对象拥有的按钮，失败返回 NULL。
 */
lv_obj_t *product_ui_pomodoro_view_nav_button(lv_obj_t *parent, const char *text,
    product_ui_pomo_action_e action);
/**
 * @brief 把阶段映射成静态名称。
 * @param[in] phase 阶段枚举。
 * @return FOCUS 为专注，SHORT_BREAK 为短休息，其余回退为长休息。
 */
const char *product_ui_pomodoro_phase_name(product_ui_pomodoro_phase_e phase);
#endif
