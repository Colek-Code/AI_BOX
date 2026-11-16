/** @file product_ui_calendar_view.h @brief 日历控件、持久按钮文字、输入身份及日程呈现；控制器拥有查询。 */
#ifndef __PRODUCT_UI_CALENDAR_VIEW_H__
#define __PRODUCT_UI_CALENDAR_VIEW_H__

#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "lvgl.h"
#include "modules/applications/product_ui/features/time/product_ui_calendar_model.h"

typedef enum {
    PRODUCT_UI_CALENDAR_MONTH,
    PRODUCT_UI_CALENDAR_DETAIL,
    PRODUCT_UI_CALENDAR_PICKER,
} product_ui_calendar_mode_e;

typedef enum {
    PRODUCT_UI_CALENDAR_ACTION_NONE,
    PRODUCT_UI_CALENDAR_ACTION_PREVIOUS,
    PRODUCT_UI_CALENDAR_ACTION_NEXT,
    PRODUCT_UI_CALENDAR_ACTION_PICKER,
    PRODUCT_UI_CALENDAR_ACTION_SELECT,
    PRODUCT_UI_CALENDAR_ACTION_YEAR_DOWN,
    PRODUCT_UI_CALENDAR_ACTION_YEAR_UP,
    PRODUCT_UI_CALENDAR_ACTION_MONTH_DOWN,
    PRODUCT_UI_CALENDAR_ACTION_MONTH_UP,
    PRODUCT_UI_CALENDAR_ACTION_APPLY,
    PRODUCT_UI_CALENDAR_ACTION_TODAY,
    PRODUCT_UI_CALENDAR_ACTION_BACK,
    PRODUCT_UI_CALENDAR_ACTION_BROWSE,
    PRODUCT_UI_CALENDAR_ACTION_DECADE_DOWN,
    PRODUCT_UI_CALENDAR_ACTION_DECADE_UP,
} product_ui_calendar_action_e;

typedef struct {
    product_ui_calendar_mode_e mode;
    const product_ui_calendar_month_t *month;
    product_ui_date_t selected;
    product_ui_date_t today;
    uint16_t picker_year;
    uint8_t picker_month;
    uint8_t has_today;
    int32_t month_scroll; /**< -1 表示显露选中日期，其他值恢复精确偏移。 */
    int32_t detail_scroll; /**< 临时访问设置页前独立保存的详情视口。 */
} product_ui_calendar_presentation_t;

typedef void (*product_ui_calendar_action_cb_t)(
    product_ui_calendar_action_e action, const product_ui_date_t *date);

/**
 * @brief 在无现存视图的串行创建阶段建立父容器及动作回调借用。
 * @param[in,out] parent 覆盖视图寿命的父容器。
 * @param[in] callback 页面动作回调，可为空；非空时须覆盖视图寿命。
 * @note 初始化会清空静态引用，不删除此前对象；已有视图须先 reset。
 */
void product_ui_calendar_view_init(lv_obj_t *parent, product_ui_calendar_action_cb_t callback);
/**
 * @brief 模式变化时先构建完整候选子树再替换旧树，同模式只重新绑定。
 * @param[in] state 调用期间借用的呈现值，month 指向的模型须持续有效至下次绑定或 reset。
 * @return 成功返回 0，空状态／父容器／月模型或任一创建失败返回 -1。
 * @note 日期与排队动作由控制器拥有；构建失败删除候选树并保留旧可用子树，不提交新呈现值。仅 UI 线程使用。
 */
int product_ui_calendar_view_show(const product_ui_calendar_presentation_t *state);
/**
 * @brief 撤销当前按压所有者并推进输入代次，使旧点击无效。
 */
void product_ui_calendar_view_cancel_press(void);
/**
 * @brief 读取当前显示视图对应的独立滚动位置。
 * @return 详情列表或月份网格的纵向偏移；对象尚不存在时为零。
 */
int32_t product_ui_calendar_view_scroll(void);
/**
 * @brief 以日程专用代次和选中日期检查详情缓存，再重试有限行绑定。
 * @param[in] elapsed_ms 本轮视图更新间隔毫秒数。
 * @note 缓存命中仍推进列表待重试工作；不完整查询、重入变更或可见行失败不提交新缓存身份。
 */
void product_ui_calendar_view_update(uint32_t elapsed_ms);
/**
 * @brief 撤销按压、删除本视图子树并清空回调、父引用及文本缓存。
 */
void product_ui_calendar_view_reset(void);

#endif
#endif
