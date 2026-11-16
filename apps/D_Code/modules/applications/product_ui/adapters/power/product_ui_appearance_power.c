/** @file product_ui_appearance_power.c @brief 把外观保存的静默和排空接到电源服务。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && D_FEATURE_POWER_CONTROL
#include "modules/services/power/power_ctrl_appearance.h"
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"

/**
 * @brief 禁止受理新的外观保存，保留已经受理的事务。
 */
void power_ctrl_appearance_begin(void)
{
    product_ui_appearance_quiesce_begin();
}

/**
 * @brief 等待外观服务完成当前事务，原样返回其排空结果。
 */
d_err_t power_ctrl_appearance_wait_idle(uint32_t timeout_ms)
{
    return product_ui_appearance_wait_idle(timeout_ms);
}

/**
 * @brief 取消电源静默，允许尚未进入停止的外观服务恢复受理。
 */
void power_ctrl_appearance_cancel(void)
{
    product_ui_appearance_quiesce_cancel();
}
#endif
