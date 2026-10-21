/**
 * @file product_ui_home.h
 * @brief Product UI page entry contracts.
 */

#ifndef PRODUCT_UI_HOME_H
#define PRODUCT_UI_HOME_H

#include "lvgl.h"
#include <stdint.h>

int product_ui_home_create(lv_obj_t *parent);
void product_ui_home_enter(void);
void product_ui_home_exit(void);
void product_ui_home_update(uint32_t elapsed_ms);

#endif /* PRODUCT_UI_HOME_H */
