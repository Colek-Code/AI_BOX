/** @file product_ui.h @brief GC9307 Product UI 的注册、导航及状态发布入口。 */

#ifndef __PRODUCT_UI_H__
#define __PRODUCT_UI_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/assets/product_ui_icons.h"
#include "modules/applications/product_ui/runtime/status/product_ui_status_model.h"
#include "lvgl.h"
#include <stdint.h>

typedef enum {
    PRODUCT_UI_PAGE_ROLE_AUTO,
    PRODUCT_UI_PAGE_ROLE_HOME,
    PRODUCT_UI_PAGE_ROLE_CONTENT,
    PRODUCT_UI_PAGE_ROLE_SETTINGS,
} product_ui_page_role_e;

typedef enum {
    PRODUCT_UI_PAGE_THEME_AUTO,
    PRODUCT_UI_PAGE_THEME_LIGHT,
    PRODUCT_UI_PAGE_THEME_DARK,
    PRODUCT_UI_PAGE_THEME_TRANSPARENT,
} product_ui_page_theme_e;

typedef struct {
    const char *title;
    const char *back_title;
    lv_event_cb_t nav_back_cb;
    void *nav_back_user_data;
    const char *nav_action_symbol;
    const lv_image_dsc_t *nav_action_image;
    const char *nav_action_text;
    uint8_t nav_action_read_only;
    uint8_t nav_action_disabled;
    lv_event_cb_t nav_action_cb;
    void *nav_action_user_data;
    uint8_t show_back;
    product_ui_page_role_e role;
    product_ui_page_theme_e theme;
} product_ui_page_spec_t;

typedef struct {
    const char *item_id;
    const char *title;
    product_ui_icon_id_t icon_id;
    product_ui_color_e accent;
} product_ui_list_item_t;

/**
 * @brief 串行装配主题、服务投影、业务域及平台适配并注册首页。
 * @return 成功返回页面管理器注册结果；关键初始化失败返回 -1。
 * @note 外观保存及连接域失败只降低相应能力；本接口不是运行中重复初始化或并发拆卸入口。
 */
int product_ui_register(void);
/**
 * @brief 核对设置模块已注册可用页面。
 * @return 设置模块存在可用页面返回 0，否则返回 -1。
 * @note 当前实现只核对注册关系，不创建页面对象。
 */
int product_ui_prepare_pages(void);
/**
 * @brief 在通知与维护允许时切到具名模块，成功后保存来源页面。
 * @param[in] module_id 本次借用的非空模块标识，须匹配已注册且拥有页面的模块。
 * @return 成功或已在目标页返回 0；参数、模块、门禁或切页失败返回 -1；导航栈满返回 -2。
 * @note 仅由 UI 执行器调用；切换失败不压入导航栈，但切页前会关闭控制中心并收敛反馈及动画。
 */
int product_ui_navigate_to(const char *module_id);
/**
 * @brief 返回导航栈顶页面，空栈时回首页。
 * @return 成功或已在首页返回 0；通知／维护阻断或切页失败返回 -1。
 * @note 成功切页后才弹出来源；回首页时清空导航栈。
 */
int product_ui_navigation_back(void);
/**
 * @brief 清空有限导航栈，在首页进入或 UI 注册时建立导航起点。
 */
void product_ui_navigation_reset(void);
/**
 * @brief 通过统一导航入口打开具名模块。
 * @param[in] module_id 本次调用借用的模块标识。
 * @return 与 product_ui_navigate_to 完全相同的返回约定。
 */
int product_ui_open_module(const char *module_id);
/**
 * @brief 规范化状态输入，内容变化时保存值副本并置脏。
 * @param[in] status 调用期间借用的状态，不保留输入指针。
 * @return 规范化有效返回 OK，剔除无效字段返回 PARTIAL，空参数返回 INVALID_ARGUMENT。
 * @note 互斥量建立后受其保护；初始化之前的调用需由启动方串行组织。
 */
product_ui_status_result_e product_ui_status_update(
    const product_ui_status_t *status);

#endif
#endif /* __PRODUCT_UI_H__ */
