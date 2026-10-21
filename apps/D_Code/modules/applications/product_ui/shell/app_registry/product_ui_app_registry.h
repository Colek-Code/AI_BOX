/**
 * @file product_ui_app_registry.h
 * @brief Single Product UI page-registration and Home-catalog manifest.
 */

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

uint8_t product_ui_app_registry_count(void);
const product_ui_app_entry_t *product_ui_app_registry_get(uint8_t index);
const d_ui_module_t *product_ui_app_registry_module(uint8_t index);
uint8_t product_ui_app_registry_home_count(void);
const product_ui_app_entry_t *product_ui_app_registry_home_get(uint8_t order);
const d_ui_module_t *product_ui_app_entry_module(
    const product_ui_app_entry_t *entry);

#endif /* PRODUCT_UI_APP_REGISTRY_H */
