/** @file product_ui_app_registry.h @brief Product UI 页面模块与首页可见目录共用的静态清单。 */

#ifndef PRODUCT_UI_APP_REGISTRY_H
#define PRODUCT_UI_APP_REGISTRY_H

#include "modules/applications/product_ui/product_ui.h"
#include "framework/d_ui_module.h"
#include <stdint.h>

typedef const d_ui_module_t *(*product_ui_module_descriptor_fn_t)(void);

typedef struct {
    product_ui_module_descriptor_fn_t descriptor;
    uint8_t home_visible;
    uint8_t home_order;
    product_ui_icon_id_t home_icon;
    product_ui_color_e home_accent;
} product_ui_app_entry_t;

/**
 * @brief 读取当前编译配置中的完整应用目录数。
 * @return 静态目录条目数，包含首页及隐藏详情页。
 */
uint8_t product_ui_app_registry_count(void);
/**
 * @brief 按目录下标借用静态应用条目。
 * @param[in] index 零起始完整目录下标。
 * @return 有效下标返回静态只读条目，越界为 NULL。
 */
const product_ui_app_entry_t *product_ui_app_registry_get(uint8_t index);
/**
 * @brief 按目录下标调用描述符访问器借用模块描述。
 * @param[in] index 零起始完整目录下标。
 * @return 条目、访问器或其结果不可用时为 NULL，否则为模块借用指针。
 */
const d_ui_module_t *product_ui_app_registry_module(uint8_t index);
/**
 * @brief 统计被标记为首页可见的目录项数。
 * @return 当前编译配置下首页可见条目数。
 */
uint8_t product_ui_app_registry_home_count(void);
/**
 * @brief 按显式首页顺序键查找可见条目。
 * @param[in] order 首页顺序键，不是完整目录下标。
 * @return 存在对应顺序返回静态条目，否则为 NULL。
 */
const product_ui_app_entry_t *product_ui_app_registry_home_get(uint8_t order);
/**
 * @brief 通过条目中的静态访问器借用模块描述。
 * @param[in] entry 借用的静态应用条目，可为 NULL。
 * @return 条目或访问器为空返回 NULL，其余原样返回访问器结果。
 */
const d_ui_module_t *product_ui_app_entry_module(
    const product_ui_app_entry_t *entry);

#endif /* PRODUCT_UI_APP_REGISTRY_H */
