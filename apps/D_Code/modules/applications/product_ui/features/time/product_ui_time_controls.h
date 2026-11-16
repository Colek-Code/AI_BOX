/** @file product_ui_time_controls.h @brief 共用时钟／闹钟控件，滚轮只在同代次完整松手时提交。 */
#ifndef PRODUCT_UI_TIME_CONTROLS_H
#define PRODUCT_UI_TIME_CONTROLS_H
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"

typedef struct product_ui_time_roller product_ui_time_roller_t;
struct product_ui_time_roller {
    lv_obj_t *object;
    void (*changed)(product_ui_time_roller_t *binding, uint16_t selected);
    uint32_t generation;
    uint32_t pressed_generation;
    uint16_t original;
    uint8_t field;
    uint8_t armed;
};

extern const char product_ui_time_hours[];
extern const char product_ui_time_minutes[];
extern const char product_ui_time_months[];
extern const char product_ui_time_years[];
/**
 * @brief 借用对应当月天数的一至末日静态选项。
 * @param[in] count 期望的当月天数。
 * @return 28 至 31 返回对应表，其他输入回退为 31 日表。
 * @note 只选择静态选项，不校验所属年月。
 */
const char *product_ui_time_day_options(uint8_t count);
/**
 * @brief 清除默认样式、滚动及事件传播标志，为明确控件策略做准备。
 * @param[in,out] object 调用方保证非空的 LVGL 对象。
 */
void product_ui_time_control_clean(lv_obj_t *object);
/**
 * @brief 创建借用不可变文字的主题标签。
 * @param[in,out] parent 标签父对象。
 * @param[in] text 必须覆盖标签寿命的不可变文字，通常为字面量。
 * @param[in] style 主题样式标记。
 * @return 成功返回父对象拥有的标签，创建失败返回 NULL。
 * @note 后续临时或格式化文字应改用 lv_label_set_text 复制，不能把栈缓冲交给静态文本接口。
 */
lv_obj_t *product_ui_time_label(lv_obj_t *parent, const char *text, product_ui_style_e style);
/**
 * @brief 创建次要角色按钮，沿用同一按压校验及不可变文字合同。
 * @param[in,out] parent 按钮父对象。
 * @param[in] text 覆盖标签生命周期的不可变文字。
 * @param[in] callback 可为空；非空时监听全部事件，须自行选择业务事件。
 * @param[in] context 回调借用的上下文，须覆盖按钮生命周期。
 * @return 成功返回父对象拥有的按钮，参数或创建失败为 NULL。
 */
lv_obj_t *product_ui_time_button(lv_obj_t *parent, const char *text, product_ui_action_cb_t callback, void *context);
typedef struct {
    const char *text;
    product_ui_button_role_e role;
    product_ui_action_cb_t callback;
    void *context;
} product_ui_time_button_spec_t;
/**
 * @brief 先安装按钮角色，再应用默认几何并绑定手势校验和可选业务回调。
 * @param[in,out] parent 按钮父对象。
 * @param[in] spec 本次借用的规格；文字及回调上下文保持借用。
 * @return 成功返回按钮；父对象、规格、文字为空或创建失败返回 NULL，局部按钮失败会删除。
 * @note 移动超过门限或失去输入资格的 CLICKED 被拦截；业务回调仍收到其他事件。
 */
lv_obj_t *product_ui_time_button_create(lv_obj_t *parent, const product_ui_time_button_spec_t *spec);
/**
 * @brief 创建持有完整拖动的三行滚轮并绑定页面持有的输入状态。
 * @param[in,out] parent 滚轮父对象。
 * @param[in] options 本次交给 LVGL 设置的换行分隔选项。
 * @param[in,out] binding 页面持有且须覆盖对象寿命的绑定；创建后由 bind 明确对象与代次。
 * @return 成功返回父对象拥有的滚轮，参数或创建失败返回 NULL。
 */
lv_obj_t *product_ui_time_roller_create(lv_obj_t *parent, const char *options, product_ui_time_roller_t *binding);
/**
 * @brief 绑定滚轮及当前代次，代次或对象变化时先撤销旧手势。
 * @param[in,out] binding 持久绑定状态，NULL 时忽略。
 * @param[in,out] object 有效滚轮对象，NULL 时忽略。
 * @param[in] selected 无活跃手势时同步的选项下标。
 * @param[in] generation 页面当前动作代次。
 * @note 按下期间不覆盖用户预览；只有相同代次的 RELEASED 调用 changed。
 */
void product_ui_time_roller_bind(product_ui_time_roller_t *binding, lv_obj_t *object, uint16_t selected, uint32_t generation);
/**
 * @brief 撤销已装备滚轮手势并恢复按下前的原选项。
 * @param[in,out] binding 持久绑定，NULL 或无活跃手势时忽略。
 * @note 不提交 changed，失去按压与输入重置都使用此路径。
 */
void product_ui_time_roller_cancel(product_ui_time_roller_t *binding);
#endif
