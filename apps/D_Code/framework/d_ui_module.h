/**
 * @file d_ui_module.h
 * @brief 声明静态 UI 模块描述符和注册接口，属于公共页面框架。
 * @note 可见的注册顺序由 app/d_ui_startup.c 装配，不由各页面临时替换。
 */

#ifndef D_UI_MODULE_H
#define D_UI_MODULE_H

#include "config/app_config.h"
#include "d_error.h"

#ifdef D_LVGL_ENABLE

#define D_MODULE_MAX_COUNT 24

typedef struct d_ui_module {
    const char *id;           /**< 稳定且非空的模块标识；按字符串内容判重。 */
    const char *display_name; /**< 借用的静态显示名。 */
    const char *icon;         /**< 借用的图标字符串。 */
    int (*register_page)(int page_type); /**< 同步注册分配的页面编号，0 为成功。 */
    d_err_t (*init)(void);    /**< UI 线程中的可选初始化回调，D_OK 为成功。 */
} d_ui_module_t;

#define D_UI_MODULE_DEFINE_ID(symbol, id_text, disp_name, icon_sym, reg_func, init_func) \
    static const d_ui_module_t s_d_ui_module_##symbol = { \
        .id = id_text, \
        .display_name = disp_name, \
        .icon = icon_sym, \
        .register_page = reg_func, \
        .init = init_func, \
    }; \
    /** @brief 借用该模块的静态描述符，调用方不得释放。 */ \
    const d_ui_module_t *d_ui_module_descriptor_##symbol(void) \
    { \
        return &s_d_ui_module_##symbol; \
    }

#define D_UI_MODULE_DEFINE(name, disp_name, icon_sym, reg_func, init_func) \
    D_UI_MODULE_DEFINE_ID(name, #name, disp_name, icon_sym, reg_func, init_func)

#define D_UI_MODULE_DECLARE(name) \
    const d_ui_module_t *d_ui_module_descriptor_##name(void)

/**
 * @brief 清空静态模块目录及自动分配的页面编号。
 * @note 仅在 UI 线程启动装配时调用，不负责销毁此前创建的页面。
 */
void d_ui_module_init(void);
/**
 * @brief 借用模块描述符并为其注册页面，成功后登记到模块目录。
 * @param[in] module 持续有效的模块描述符；id 和 register_page 必须非 NULL。
 * @return 成功时为分配的正页面编号；无效、重复、满或页面注册失败时为 -1。
 * @note 仅供 UI 线程启动阶段调用。模块结构及其引用对象不复制；失败不递增目录编号。
 */
int d_ui_module_register(const d_ui_module_t *module);
/**
 * @brief 读取已成功登记的模块数量。
 * @return 当前目录项数，范围为 0 至 D_MODULE_MAX_COUNT。
 */
int d_ui_module_get_count(void);
/**
 * @brief 按目录索引借用模块描述符。
 * @param[in] index 从 0 开始的目录索引。
 * @return 已登记的描述符指针；索引越界时为 NULL。
 */
const d_ui_module_t *d_ui_module_get(int index);
/**
 * @brief 按目录索引查询自动分配的页面编号。
 * @param[in] index 从 0 开始的目录索引。
 * @return 有效页面编号；索引越界时为 -1。
 */
int d_ui_module_get_page_type(int index);
/**
 * @brief 判断模块是否提供页面注册回调。
 * @param[in] module 待查询描述符，可为 NULL。
 * @return module 与 register_page 都非 NULL 时为 1，否则为 0。
 */
int d_ui_module_has_page(const d_ui_module_t *module);
/**
 * @brief 按注册顺序调用各模块的可选初始化回调。
 * @return 全部成功为 D_OK；首个失败时立即返回该错误。
 * @note 仅供 UI 线程启动阶段调用；失败不会回滚先前模块，也不会继续初始化后续模块。
 */
d_err_t d_ui_module_init_all(void);

#endif /* D_LVGL_ENABLE */
#endif /* D_UI_MODULE_H */
