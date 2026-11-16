/** @file product_ui_alarm_view.h @brief 有限闹钟列表、编辑及音量视图，结构替换与不可变输入身份分开。 */
#ifndef PRODUCT_UI_ALARM_VIEW_H
#define PRODUCT_UI_ALARM_VIEW_H
#include "product_ui_alarm_service.h"
#include "product_ui_time_controls.h"
typedef enum {
    PRODUCT_UI_ALARM_LIST, PRODUCT_UI_ALARM_EDITOR, PRODUCT_UI_ALARM_SETTINGS,
    PRODUCT_UI_ALARM_ROUTINE, PRODUCT_UI_ALARM_MODE_COUNT
} product_ui_alarm_mode_e;
typedef enum {
    PRODUCT_UI_ALARM_REPEAT_ONCE, PRODUCT_UI_ALARM_REPEAT_DAILY, PRODUCT_UI_ALARM_REPEAT_DOUBLE,
    PRODUCT_UI_ALARM_REPEAT_SINGLE, PRODUCT_UI_ALARM_REPEAT_FORTNIGHT, PRODUCT_UI_ALARM_REPEAT_CUSTOM,
    PRODUCT_UI_ALARM_REPEAT_COUNT
} product_ui_alarm_repeat_e;
typedef enum {
    PRODUCT_UI_ALARM_UI_NONE, PRODUCT_UI_ALARM_UI_NEW, PRODUCT_UI_ALARM_UI_EDIT,
    PRODUCT_UI_ALARM_UI_TOGGLE, PRODUCT_UI_ALARM_UI_SETTINGS, PRODUCT_UI_ALARM_UI_BACK,
    PRODUCT_UI_ALARM_UI_SAVE, PRODUCT_UI_ALARM_UI_DELETE, PRODUCT_UI_ALARM_UI_HOUR,
    PRODUCT_UI_ALARM_UI_MINUTE, PRODUCT_UI_ALARM_UI_REPEAT, PRODUCT_UI_ALARM_UI_WEEKDAY,
    PRODUCT_UI_ALARM_UI_LABEL, PRODUCT_UI_ALARM_UI_ENABLED, PRODUCT_UI_ALARM_UI_VOLUME,
    PRODUCT_UI_ALARM_UI_VOLUME_DOWN, PRODUCT_UI_ALARM_UI_VOLUME_UP,
    PRODUCT_UI_ALARM_UI_PREVIEW, PRODUCT_UI_ALARM_UI_RETRY_SAVE, PRODUCT_UI_ALARM_UI_TIME,
    PRODUCT_UI_ALARM_UI_ROUTINE, PRODUCT_UI_ALARM_UI_ALTERNATE_DAY,
    PRODUCT_UI_ALARM_UI_PHASE, PRODUCT_UI_ALARM_UI_DEFAULT_DAYS,
} product_ui_alarm_ui_action_e;
typedef struct {
    product_ui_alarm_ui_action_e action;
    uint32_t id;
    uint32_t revision;
    uint32_t view_generation;
    uint16_t value;
} product_ui_alarm_ui_event_t;
typedef void (*product_ui_alarm_ui_action_fn)(const product_ui_alarm_ui_event_t *event);
typedef struct {
    product_ui_alarm_t rule;
    product_ui_alarm_repeat_e kind;
    uint8_t phase_confirmed;
} product_ui_alarm_routine_draft_t;

typedef struct {
    product_ui_alarm_record_t record;
    product_ui_alarm_routine_draft_t routine;
    uint8_t volume;
    uint8_t dirty;
    uint8_t preview_active;
    uint8_t has_conflict;
} product_ui_alarm_draft_t;

/**
 * @brief 必要时先完整构建候选树，再替换当前闹钟子视图。
 * @param[in,out] parent 持续拥有当前视图的父容器。
 * @param[in] mode 列表、编辑、声音设置或作息模式。
 * @param[in] draft 控制器拥有的本次草稿值，不保留其指针。
 * @param[in] action 非空且覆盖视图寿命的动作回调，同模式复用时保持原绑定。
 * @return 成功或同模式可复用返回 0；参数、模式或任一创建失败返回 -1。
 * @note 仅 UI 线程串行使用；同模式及同作息种类不重建。创建失败删除候选树，原可用树及显示缓存保留。
 */
int product_ui_alarm_view_show(lv_obj_t *parent, product_ui_alarm_mode_e mode,
    const product_ui_alarm_draft_t *draft, product_ui_alarm_ui_action_fn action);
/**
 * @brief 按服务值及控制器草稿更新当前视图，保留输入预览及失败重试条件。
 * @param[in] snapshot 本次借用的服务值。
 * @param[in] draft 控制器拥有的草稿，视图只呈现。
 * @note 列表内容或时钟变化撤销旧行输入；列表刷新成功后才提交已见模型代次。
 */
void product_ui_alarm_view_refresh(const product_ui_alarm_snapshot_t *snapshot, const product_ui_alarm_draft_t *draft);
/**
 * @brief 列表模式下推进共用虚拟列表重试，并为新复用槽附着记录身份监听。
 * @param[in] elapsed_ms 本轮 UI 间隔毫秒数。
 */
void product_ui_alarm_view_update(uint32_t elapsed_ms);
/**
 * @brief 撤销全部手势，删除当前视图树并清空控件、行副本及动作回调。
 */
void product_ui_alarm_view_destroy(void);
/**
 * @brief 推进非零视图代次，取消滚轮／行手势并恢复活跃音量滑条原值。
 */
void product_ui_alarm_view_cancel_input(void);
/**
 * @brief 读取当前 UI 视图的非零输入代次。
 * @return 当前输入代次；控制器执行排队动作前须再比较是否相等。
 */
uint32_t product_ui_alarm_view_generation(void);
/**
 * @brief 读取当前列表或编辑子树的独立纵向偏移。
 * @return 对应根对象滚动偏移，根不存在时为零。
 */
int32_t product_ui_alarm_view_scroll(void);
/**
 * @brief 布局完成后立即恢复当前列表或编辑子树视口。
 * @param[in] y 控制器此前保存的纵向偏移，由 LVGL 约束实际可滚动范围。
 */
void product_ui_alarm_view_restore_scroll(int32_t y);
/**
 * @brief 创建有稳定动作身份及按压校验的窄导航按钮。
 * @param[in,out] parent 导航父容器。
 * @param[in] text 静态借用文字，须覆盖按钮标签寿命。
 * @param[in] action 导航操作编号。
 * @return 成功返回父树拥有的按钮，失败返回 NULL。
 */
lv_obj_t *product_ui_alarm_view_nav_button(lv_obj_t *parent, const char *text, product_ui_alarm_ui_action_e action);
#endif
