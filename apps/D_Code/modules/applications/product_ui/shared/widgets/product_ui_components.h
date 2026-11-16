/** @file product_ui_components.h @brief 共用 UI 组件及持久引用合同，所有入口由 UI 线程串行使用。 */

#ifndef __PRODUCT_UI_COMPONENTS_H__
#define __PRODUCT_UI_COMPONENTS_H__

#include "modules/applications/product_ui/product_ui.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_row_model.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_app_slot.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

/**
 * @brief UI 组件的同步事件动作，事件及其参数只在分发期间借用。
 * @param[in] event LVGL 借用事件；具体过滤阶段遵循对应构造器合同。
 */
typedef void (*product_ui_action_cb_t)(lv_event_t *event);

#define PRODUCT_UI_LABEL_CACHE_CAPACITY 128U
typedef struct {
    lv_obj_t *label;
    char requested[PRODUCT_UI_LABEL_CACHE_CAPACITY];
    uint8_t valid;
} product_ui_label_cache_t;

/** 每个物理复用行可选的 UI 自有文字缓存；数组不得放入 UI 栈上的
 * 临时候选视图，须覆盖行对象寿命并在重建／删除时清零。 */
typedef struct {
    product_ui_label_cache_t title;
    product_ui_label_cache_t subtitle;
    product_ui_label_cache_t trailing;
} product_ui_row_text_cache_t;

/**
 * @brief 以请求全文比较的缓存更新标签，并为文字或 DOT 格式化失败保留重试。
 * @param[in,out] label 有效标签对象。
 * @param[in] text 本次借用的 NUL 结尾全文，LVGL 复制，不保留此指针。
 * @param[in,out] cache 可为空；非空时须由页面持有并在创建、重建及销毁时清零。
 * @return 0 未变化，1 已写入，-1 参数无效或尚未成功格式化。
 * @note 仅 UI 线程；比较 requested 以兼容 DOT 缩略。长于缓存容量的全文仍显示但不缓存，长正文应由内容代次控制而非高频轮询。NULL 缓存不会保存原请求，缩略文本可能需要重复写入。
 */
int product_ui_label_sync(lv_obj_t *label, const char *text,
                          product_ui_label_cache_t *cache);

typedef enum {
    PRODUCT_UI_FEEDBACK_EMPTY,
    PRODUCT_UI_FEEDBACK_LOADING,
    PRODUCT_UI_FEEDBACK_ERROR,
} product_ui_feedback_state_e;

typedef enum {
    PRODUCT_UI_ROW_APPEARANCE_DEFAULT,
    PRODUCT_UI_ROW_APPEARANCE_INSET,
} product_ui_row_appearance_e;

typedef struct {
    const product_ui_list_item_t *item;
    const char *subtitle;
    const char *status_text;
    product_ui_action_cb_t action_cb;
    void *user_data;
    product_ui_row_interaction_e interaction;
    product_ui_row_trailing_e trailing;
    uint8_t is_enabled;
    uint8_t is_checked;
    uint8_t is_single_line;
    uint32_t item_key;
    product_ui_row_appearance_e appearance; /**< 零使用原有外观，INSET 使用内嵌列表样式，不改变动作身份。 */
    product_ui_row_text_cache_t *text_cache;
} product_ui_row_spec_t;

/**
 * @brief 复用当前行对象，更新图标、文字、尾部及可操作性。
 * @param[in,out] row 本模块固定布局创建的行。
 * @param[in] spec 本次借用的显示规格；可选文本缓存须覆盖行寿命。
 * @return 成功返回 0；规格、图标、结构或文字／尾部更新失败返回 -1。
 * @note 原动作回调和上下文保持附着，不被新规格替换；失败可能已更新部分外观及文字，调用方保留对象并重试，不能当作原子整行提交。
 */
int product_ui_list_row_rebind(lv_obj_t *row,
                               const product_ui_row_spec_t *spec);
/**
 * @brief 只把现有行尾更新为状态文字，不重建整行及动作绑定。
 * @param[in,out] row 本模块创建的列表行。
 * @param[in] text 本次复制的状态文字。
 * @return 成功返回 0，空参数、行尾缺失或更新失败返回 -1。
 */
int product_ui_list_row_status_update(lv_obj_t *row, const char *text);

typedef struct {
    product_ui_feedback_state_e state;
    const char *title;
    const char *message;
    const char *action_label;
    product_ui_action_cb_t action_cb;
    void *user_data;
} product_ui_feedback_spec_t;

#define PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT    6U

typedef struct product_ui_virtual_list product_ui_virtual_list_t;
/**
 * @brief 为逻辑条目及指定物理槽产生候选行规格，不提交业务动作。
 * @param[in] item_index 当前逻辑条目下标。
 * @param[in] slot_index 六槽之一；返回文字须在槽重绑前保持有效。
 * @param[out] spec 接收完整规格及稳定 item_key，列表随后注入其动作门禁。
 * @param[in,out] user_data 调用方持有的持续状态，可更新该槽显示副本。
 * @return 0 表示绑定成功，非零使列表保留待重试状态。
 */
typedef int (*product_ui_virtual_list_bind_cb_t)(
    uint16_t item_index, uint8_t slot_index,
    product_ui_row_spec_t *spec, void *user_data);
/**
 * @brief 列表完整点击及绑定身份校验通过后同步报告逻辑动作。
 * @param[in] item_index 当前有效逻辑条目下标，不是稳定记录编号。
 * @param[in,out] user_data 列表借用的业务上下文，具体动作由调用方受理。
 */
typedef void (*product_ui_virtual_list_action_cb_t)(
    uint16_t item_index, void *user_data);
/** 可选自定义行呈现所有者，创建及重绑回调必须成对。创建器收到非空
 * spec->action_cb 时将其绑定 LV_EVENT_ALL；列表仍拥有控件生命周期、
 * 绑定身份、视口及点击取消，呈现所有者不能绕开这些门禁。 */
typedef lv_obj_t *(*product_ui_virtual_row_create_cb_t)(lv_obj_t *parent, const product_ui_row_spec_t *spec);
typedef int (*product_ui_virtual_row_rebind_cb_t)(lv_obj_t *row, const product_ui_row_spec_t *spec);

typedef struct {
    uint16_t item_count;
    uint16_t row_height;
    product_ui_virtual_list_bind_cb_t bind_cb;
    product_ui_virtual_list_action_cb_t action_cb;
    void *user_data;
    product_ui_virtual_row_create_cb_t row_create_cb;
    product_ui_virtual_row_rebind_cb_t row_rebind_cb;
    product_ui_row_text_cache_t *text_caches; /**< 可选的 SLOT_COUNT 项持久缓存数组，不随逻辑下标移动。 */
} product_ui_virtual_list_spec_t;

typedef struct {
    lv_obj_t *row;
    product_ui_virtual_list_t *owner;
    uint16_t item_index;
    uint32_t binding_version;
    uint32_t item_key;
    uint32_t pressed_version;
    uint8_t is_armed;
    product_ui_row_interaction_e interaction;
} product_ui_virtual_list_slot_t;

struct product_ui_virtual_list {
    lv_obj_t *root;
    lv_obj_t *extent;
    lv_obj_t *header;
    /**< 物理控件／缓存槽，槽顺序不等于可见逻辑条目顺序。 */
    product_ui_virtual_list_slot_t slots[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    product_ui_virtual_list_bind_cb_t bind_cb;
    product_ui_virtual_list_action_cb_t action_cb;
    void *user_data;
    product_ui_virtual_row_create_cb_t row_create_cb;
    product_ui_virtual_row_rebind_cb_t row_rebind_cb;
    product_ui_row_text_cache_t *text_caches;
    uint16_t item_count;
    uint16_t first_bound;
    uint16_t row_height;
    uint16_t row_pitch;
    int32_t header_height;
    uint8_t is_refreshing;
    uint8_t render_pending;
    uint16_t retry_elapsed_ms;
};

/**
 * @brief 模态删除并归还下拉占用后同步报告用户选择。
 * @param[in] is_confirmed 普通／危险确认：0 取消、1 确认；保存三选还允许 2 不保存。
 * @param[in,out] user_data 调用方持续持有的借用上下文。
 * @note clear 或外部删除不调用此回调；持久化与导航由调用方继续负责。
 */
typedef void (*product_ui_confirm_cb_t)(uint8_t is_confirmed, void *user_data);

#define PRODUCT_UI_WIFI_BAR_COUNT 4

/** 只记录已呈现值，不构成第二套服务状态。每组可见表示变化时才写入，
 * 不可用图标保留其布局槽。 */
typedef struct {
    char time[8];
    lv_color_t foreground;
    lv_color_t battery_color;
    lv_opa_t time_opa;
    lv_opa_t wifi_opa;
    lv_opa_t wifi_bars[PRODUCT_UI_WIFI_BAR_COUNT];
    lv_opa_t bluetooth_opa;
    lv_opa_t battery_opa;
    uint8_t battery_width;
    uint8_t charging;
} product_ui_status_projection_t;

typedef struct product_ui_status_view {
    lv_obj_t *root;
    lv_obj_t *time;
    lv_obj_t *wifi; /**< 四格信号容器，显示强度来自有效 wifi_level。 */
    lv_obj_t *wifi_bar[PRODUCT_UI_WIFI_BAR_COUNT];
    lv_obj_t *bluetooth;
    /* host 是完整 flex 单元，外框／端点／填充位于内部，端点不与相邻图标重叠。 */
    lv_obj_t *battery_host;
    lv_obj_t *battery;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_tip;
    lv_obj_t *battery_charge;
    lv_obj_t *battery_text;
    struct product_ui_status_view *next;
    uint8_t is_light;
    uint8_t hide_connectivity;
    uint8_t is_registered;
    uint8_t has_projection;
    product_ui_status_projection_t projection;
} product_ui_status_view_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *status;
    lv_obj_t *nav;
    lv_obj_t *title;
    product_ui_motion_t title_motion;
    lv_obj_t *nav_action;
    lv_obj_t *nav_action_label;
    lv_obj_t *content;
    uint8_t is_nav_action_armed;
    uint8_t is_nav_action_read_only;
    product_ui_status_view_t status_view;
} product_ui_page_view_t;

/**
 * @brief 验证规格后创建图标、文字及尾部，绑定共用动作门禁。
 * @param[in,out] parent 新行父容器。
 * @param[in] spec 本次借用的规格，动作及上下文须覆盖行寿命。
 * @return 成功返回父树拥有的完整行，参数、图标或子对象失败返回 NULL，失败删除半成品行。
 * @note 文字交 LVGL 复制，图像借用不可变生成资源；隐藏、禁用或无点击能力时拦截业务点击。
 */
lv_obj_t *product_ui_list_row_create(lv_obj_t *parent,
                                     const product_ui_row_spec_t *spec);
/**
 * @brief 创建指定宽度的主题分组标题。
 * @param[in,out] parent 标签父容器。
 * @param[in] title 本次复制的分组标题。
 * @return 成功返回父树拥有的标签，空参数或创建失败返回 NULL。
 */
lv_obj_t *product_ui_section_create(lv_obj_t *parent, const char *title);
/**
 * @brief 创建居中的主题空状态说明标签。
 * @param[in,out] parent 标签父容器。
 * @param[in] message 本次复制的说明文字。
 * @return 成功返回父树拥有的标签，空参数或创建失败返回 NULL。
 */
lv_obj_t *product_ui_empty_state_create(lv_obj_t *parent, const char *message);
/**
 * @brief 创建六槽复用列表的根、页头及滚动占位，首轮内容留待 refresh。
 * @param[in,out] parent 有效父容器，内容宽高须大于零。
 * @param[out] list 调用方持有的列表状态，须保持有效至根对象删除回调完成。
 * @param[in] spec 本次复制的规格；回调、上下文及可选缓存仍借用。
 * @return 结构创建成功返回 0；参数、回调配对、行高、父尺寸或对象创建失败返回 -1。
 * @note 仅 UI 线程使用；自定义创建和重绑回调必须成对提供。已有列表须先删除旧根，不能用此 memset 重建代替拆卸。结构成功不代表首轮数据完整呈现。
 */
int product_ui_virtual_list_create(lv_obj_t *parent,
                                      product_ui_virtual_list_t *list,
                                      const product_ui_virtual_list_spec_t *spec);
/**
 * @brief 强制重绑当前有限窗口，并尽量按稳定条目键恢复视口锚点。
 * @param[in,out] list 已经创建且由页面持有的列表。
 * @return 完整刷新或重入保护返回 0；参数、锚点读取或行创建／绑定失败返回 -1。
 * @note 失败保留已建控件及 render_pending，下次 update 再试；重入返回零不表示已完成，调用方还需检查 render_pending／is_refreshing。
 */
int product_ui_virtual_list_refresh(product_ui_virtual_list_t *list);
/**
 * @brief 立即更新逻辑数量并强制刷新当前窗口。
 * @param[in,out] list 已经创建的列表。
 * @param[in] count 新的逻辑条目总数。
 * @return 刷新结果，成功为 0，参数或绑定失败为 -1。
 * @note 失败不回滚数量，保留 pending 及已有控件；上层不能据此提交完整呈现缓存键。
 */
int product_ui_virtual_list_set_count(product_ui_virtual_list_t *list, uint16_t count);
/**
 * @brief 仅在 render_pending 且累计间隔达到一百毫秒时重试强制刷新。
 * @param[in,out] list 页面持有的列表，空或未创建时忽略。
 * @param[in] elapsed_ms 本轮更新毫秒数。
 */
void product_ui_virtual_list_update(product_ui_virtual_list_t *list, uint32_t elapsed_ms);
/**
 * @brief 按滚动像素、页头及行间距计算首个可见逻辑下标。
 * @param[in] list 借用的已创建列表。
 * @return 计算下标；列表无效、间距为零或仍在页头内返回零。
 * @note 结果不是稳定条目身份，容器内容变化时应结合 item_key 恢复；本函数不额外按 item_count 钳制。
 */
uint16_t product_ui_virtual_list_first_visible(
    const product_ui_virtual_list_t *list);
/**
 * @brief 把指定逻辑行滚入视图，首项回到零偏移并刷新复用窗口。
 * @param[in,out] list 页面持有的列表。
 * @param[in] item_index 目标逻辑下标，非空列表越界时钳制到最后一项。
 * @note 刷新失败直接保留当前位置；最终像素偏移由 LVGL 实际范围限制。
 */
void product_ui_virtual_list_scroll_to(product_ui_virtual_list_t *list,
                                       uint16_t item_index);
/**
 * @brief 按合法反馈状态创建无动作面板，复制标题及说明。
 * @param[in,out] parent 面板父容器。
 * @param[in] state 合法 EMPTY／LOADING／ERROR 枚举。
 * @param[in] title 本次复制的标题。
 * @param[in] message 本次复制的说明。
 * @return 成功返回父树拥有的面板，空参数或创建失败返回 NULL。
 */
lv_obj_t *product_ui_feedback_create(lv_obj_t *parent,
                                     product_ui_feedback_state_e state,
                                     const char *title,
                                     const char *message);
/**
 * @brief 创建可选动作反馈面板，动作文字和回调必须同时有或同时无。
 * @param[in,out] parent 面板父容器。
 * @param[in] spec 借用的合法反馈规格；文字复制，回调及上下文须覆盖面板寿命。
 * @return 完整创建返回父树拥有的根，必要参数、角色或创建不符返回 NULL，失败删除局部树。
 */
lv_obj_t *product_ui_feedback_create_ex(
    lv_obj_t *parent, const product_ui_feedback_spec_t *spec);
/**
 * @brief 替换单个非模态提示条，按期限自动删除并应用当前动效策略。
 * @param[in] message 本次复制的非空提示，空指针／空串忽略。
 * @param[in] duration_ms 正显示时长毫秒，零忽略。
 * @note 仅 UI 线程；不取得输入所有权。对象或定时器创建失败不保留半成品提示。
 */
void product_ui_toast_show(const char *message, uint16_t duration_ms);
/**
 * @brief 清除当前提示条及确认模态，归还本模态的下拉占用。
 * @note 清除只撤销 UI 与回调引用，不向业务回调伪造一次取消选择，也不撤销服务已受理动作。
 */
void product_ui_feedback_clear(void);
/**
 * @brief 查询当前单个确认模态根是否存在。
 * @return 存在返回非零，否则为零。
 */
uint8_t product_ui_feedback_is_modal(void);
typedef enum {
    PRODUCT_UI_BUTTON_SECONDARY,
    PRODUCT_UI_BUTTON_PRIMARY,
    PRODUCT_UI_BUTTON_DANGER,
    PRODUCT_UI_BUTTON_ICON_LIGHT,
    PRODUCT_UI_BUTTON_ICON_DARK,
} product_ui_button_role_e;

/* 模态调用方保持原源码／ABI 及精确三角色策略，不接纳图标按钮角色。 */
typedef product_ui_button_role_e product_ui_dialog_button_role_e;
#define PRODUCT_UI_DIALOG_SECONDARY PRODUCT_UI_BUTTON_SECONDARY
#define PRODUCT_UI_DIALOG_PRIMARY   PRODUCT_UI_BUTTON_PRIMARY
#define PRODUCT_UI_DIALOG_DANGER    PRODUCT_UI_BUTTON_DANGER

/**
 * @brief 替换按钮的视觉／本地样式并安装主题角色，不改变事件回调与点击资格。
 * @param[in,out] button 借用按钮，NULL 时忽略。
 * @param[in] role 主、次、危险、浅色图标或深色图标角色，非法值回退次要。
 * @note 调用后再设置尺寸和布局；已有直接标签继承按钮状态文字色，标签字体及布局保留。新标签应避免覆盖其状态颜色。
 */
void product_ui_button_style(lv_obj_t *button, product_ui_button_role_e role);
/**
 * @brief 以旧对话框三角色合同调用共用按钮样式，其他角色回退次要。
 * @param[in,out] button 借用按钮，NULL 时忽略。
 * @param[in] role SECONDARY／PRIMARY／DANGER 之一。
 * @note 仅 UI 线程，在直接标签创建后且设置最终几何前使用。
 */
void product_ui_dialog_button_style(lv_obj_t *button, product_ui_dialog_button_role_e role);
/**
 * @brief 创建全局单个普通确认模态。
 * @param[in] title 本次复制的标题。
 * @param[in] message 本次复制的说明。
 * @param[in] callback 可为空的静态或持续有效选择回调，0 取消、1 确认。
 * @param[in] user_data 回调借用上下文，须有效至选择或显式 clear。
 * @return 成功返回外层遮罩句柄，已存在模态、空标题／说明或创建失败返回 NULL。
 * @note 先删除模态及释放占用再调用业务回调；外部删除／clear 不回调。仅报告选择意图，不自动执行持久化或导航。
 */
lv_obj_t *product_ui_confirm_dialog_show(const char *title,
                                         const char *message,
                                         product_ui_confirm_cb_t callback,
                                         void *user_data);
/**
 * @brief 创建危险操作确认模态，沿用普通确认的零／一回调与生命周期。
 * @param[in] title 本次复制的标题。
 * @param[in] message 本次复制的说明。
 * @param[in] callback 可为空的选择回调，0 取消、1 确认。
 * @param[in] user_data 覆盖模态寿命的借用上下文。
 * @return 成功返回遮罩句柄，参数、已有模态或创建失败为 NULL。
 * @note 危险动作仍由调用方在回调后受理，创建模态不执行业务变更。
 */
lv_obj_t *product_ui_danger_dialog_show(const char *title, const char *message,
    product_ui_confirm_cb_t callback, void *user_data);

/**
 * @brief 创建取消／不保存／保存的三选模态，只报告用户意图。
 * @param[in] title 本次复制的标题。
 * @param[in] message 本次复制的说明。
 * @param[in] callback 可为空，0 取消、1 保存、2 不保存离开。
 * @param[in] user_data 覆盖模态寿命的借用上下文。
 * @return 成功返回遮罩句柄，参数、已有模态或创建失败为 NULL。
 * @note 异步持久化完成及导航由调用方继续拥有，保存选择不等于已写入。
 */
lv_obj_t *product_ui_save_dialog_show(const char *title, const char *message,
    product_ui_confirm_cb_t callback, void *user_data);
/**
 * @brief 对刚创建的当前模态选择紧凑布局，短正文缩至内容，长正文保留滚动。
 * @param[in,out] modal show 返回的外层遮罩句柄，须在首次渲染前传入。
 * @return 应用成功返回 0，句柄／结构不属于当前模态或可用空间不足返回 -1。
 * @note 只改变布局，不改变确认回调、角色或业务意图；拒绝后原模态继续可用。
 */
int product_ui_dialog_compact(lv_obj_t *modal);
/**
 * @brief 创建带标题、副标题及本地开关的设置行。
 * @param[in,out] parent 新行的父容器。
 * @param[in] title 本次标签复制的标题。
 * @param[in] subtitle 本次标签复制的副标题。
 * @param[in] is_checked 非零初始选中。
 * @return 成功返回父容器拥有的行，参数或子对象创建失败返回 NULL；部分行可仍留在父树待页面清理。
 * @note 行点击翻转开关并发送值变化；此辅助控件不自行提交服务命令。
 */
lv_obj_t *product_ui_toggle_row_create(lv_obj_t *parent, const char *title,
                                       const char *subtitle, uint8_t is_checked);
/**
 * @brief 从本模块固定布局的设置行借用开关或滑条对象。
 * @param[in] row 设置行，允许为空。
 * @return 至少有两个子对象时返回第二个，否则为 NULL。
 * @note 返回对象由行及页面树持有，不由调用方单独释放。
 */
lv_obj_t *product_ui_setting_row_get_control(lv_obj_t *row);
/**
 * @brief 创建带百分比副标题的本地设置滑条行。
 * @param[in,out] parent 新行的父容器。
 * @param[in] title 本次标签复制的标题。
 * @param[in] value 初值，钳制为 0 至 100。
 * @return 成功返回父容器拥有的行，参数或子对象创建失败返回 NULL。
 * @note 只绑定本地文字更新；消费者另行绑定业务受理。创建失败的部分对象交父页面清理。
 */
lv_obj_t *product_ui_slider_row_create(lv_obj_t *parent, const char *title,
                                       int32_t value);
/**
 * @brief 在真实 RESET 回调中，仅对仍按下的指针输入设置等待物理松手。
 * @param[in] event 借用事件，NULL、非 RESET、已松开或非指针输入均不操作。
 * @note 输入设备从 RESET 的 param 取得；不阻塞等待，不保留设备指针，不改变已经释放的输入。
 */
void product_ui_control_wait_release(lv_event_t *event);

/**
 * @brief 在 UI 启动阶段创建全局控制中心、状态栏及内容并绑定吸附动画。
 * @return 已创建或完整创建返回 0，任一步失败删除局部全局树并返回 -1。
 * @note 静态单实例超出普通页面寿命；状态与回调只由 UI 线程串行访问。
 */
int product_ui_control_center_init(void);
/**
 * @brief 预约打开或关闭吸附，存在下拉占用时拒绝打开。
 * @param[in] visible 非零请求完全打开，零请求关闭。
 * @note 须先成功 init；接口只启动 UI 收敛，不表示立刻到达 OPEN。
 */
void product_ui_control_center_show(uint8_t visible);
/**
 * @brief 仅在全局状态明确 OPEN 时读取并刷新服务内容。
 * @note 拖动及吸附中不取服务快照；首个完整打开由完成回调立即刷新。
 */
void product_ui_control_center_refresh(void);
/**
 * @brief 清除打开／关闭拖动与旧遮罩点击资格，并立即收敛为关闭终态。
 * @note 用于页面切换及模态取得下拉占用，不改变任何业务服务事务。
 */
void product_ui_control_center_close_for_page_transition(void);
/**
 * @brief 向指定对象附着全局下拉打开手势监听。
 * @param[in,out] obj 借用对象，NULL 忽略。
 * @note 监听引用静态拖动状态；本函数追加回调，调用方应避免重复绑定同一对象。
 */
void product_ui_control_center_bind_pull(lv_obj_t *obj);
/**
 * @brief 为一个模态所有者取得独立下拉占用，并立即关闭控制中心。
 * @param[in,out] is_held 调用方持久持有、初始为零的所有者位；NULL 或已经持有时忽略。
 * @note 同一所有者重复取得不重复计数；不同模态使用独立字节，必须在其清理时归还。仅 UI 线程。
 */
void product_ui_control_center_hold_pull(uint8_t *is_held);
/**
 * @brief 仅归还本调用方已经取得的下拉占用，不自动重新打开。
 * @param[in,out] is_held 与 hold 使用相同的所有者字节，未持有或 NULL 时忽略。
 */
void product_ui_control_center_release_pull(uint8_t *is_held);
/**
 * @brief 查询控制中心非关闭、已有下拉占用或打开手势正在跟踪的综合 UI 忙状态。
 * @return 任一条件存在为非零，否则为零；不等于某业务服务正在处理。
 */
uint8_t product_ui_control_center_is_busy(void);
/**
 * @brief 查询控制中心是否到达完全打开终态。
 * @return 状态为 OPEN 时非零，拖动／吸附／关闭阶段均为零。
 */
uint8_t product_ui_control_center_is_open(void);
/**
 * @brief 创建控制中心专用浅色前景状态栏，注册到共用值刷新链。
 * @param[in,out] parent 状态栏父容器。
 * @param[out] view 持续有效的状态栏引用，内存须覆盖根删除回调。
 * @return 成功返回父树拥有的根，参数或子对象失败返回 NULL，部分子树交父容器清理。
 * @note 不绑定下拉手势；连接图标只在真实有效能力发布后呈现，未提供值时保留布局槽。
 */
lv_obj_t *product_ui_control_status_create(lv_obj_t *parent,
                                           product_ui_status_view_t *view);

/**
 * @brief 在页面管理器提供的根对象中创建状态栏、可选导航和内容区。
 * @param[in,out] parent 页面管理器拥有的根容器，创建失败的部分子树也由它删除。
 * @param[in] spec 本次借用的页面规格；文字复制，图像／回调及上下文须持续有效。
 * @param[in,out] view 首次为全零或仍有效的页面状态，须保持内存有效至页面树删除回调结束。
 * @return 完整创建返回 0；参数、主题／控制中心或任一子对象失败返回 -1。
 * @note 仅 UI 线程；创建先 reset 引用而不删除旧树，调用方须按页面生命周期重建。AUTO 角色按是否返回导航推导；失败不声称已清除所有子对象。
 */
int product_ui_page_frame_create_ex(lv_obj_t *parent,
                                    const product_ui_page_spec_t *spec,
                                    product_ui_page_view_t *view);
/**
 * @brief 解除标题动效、页面监听和状态栏登记，再清空页面状态，不删除父对象树。
 * @param[in,out] view 持续有效的页面状态，NULL 时忽略。
 * @note 页面管理器仍拥有实际对象删除；状态内存须至少保留至其子树删除完成，不能把 reset 解释为所有借用回调都已排空。
 */
void product_ui_page_view_reset(product_ui_page_view_t *view);
/**
 * @brief 向已有页面同步发送共用进入事件，触发标题轻移进入效果。
 * @param[in,out] root 目标页面根，NULL 或事件尚未注册时忽略。
 * @param[in] is_back 非零为返回导航，决定标题起始方向。
 * @note 事件参数只在同步分发期间借用。
 */
void product_ui_page_enter(lv_obj_t *root, uint8_t is_back);
/**
 * @brief 更新导航文字动作的显示、使能及只读状态；操作改变时撤销按压资格。
 * @param[in,out] view 持有已创建导航动作及标签的页面状态。
 * @param[in] text 本次复制的文字；NULL 或空串隐藏动作。
 * @param[in] is_enabled 非零且非只读时允许点击。
 * @param[in] is_read_only 非零仅作为状态显示，不接收点击。
 * @note 文字失败仍可由后续周期重试；不替换业务回调绑定，不把旧操作按压转移给新显示操作。
 */
void product_ui_page_action_update(product_ui_page_view_t *view,
                                    const char *text, uint8_t is_enabled,
                                    uint8_t is_read_only);

/**
 * @brief 把已规范化状态值投影到所有完整且仍登记的状态栏。
 * @param[in] status 本次借用的规范化状态，NULL 时忽略。
 * @note 仅 UI 线程；每组仅在可见表现变化时写样式，文字失败不提交完整投影缓存。
 */
void product_ui_status_bar_refresh(const product_ui_status_t *status);

#endif
#endif /* __PRODUCT_UI_COMPONENTS_H__ */
