/** @file product_ui_appearance_power.c @brief Connect appearance saves to the power service. */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && D_FEATURE_POWER_CONTROL
#include "modules/services/power/power_ctrl_appearance.h"
#include "modules/applications/product_ui/runtime/preferences/product_ui_appearance.h"

void power_ctrl_appearance_begin(void)
{
    product_ui_appearance_quiesce_begin();
}

d_err_t power_ctrl_appearance_wait_idle(uint32_t timeout_ms)
{
    return product_ui_appearance_wait_idle(timeout_ms);
}

void power_ctrl_appearance_cancel(void)
{
    product_ui_appearance_quiesce_cancel();
}
#endif
