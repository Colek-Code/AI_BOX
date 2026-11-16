/**
 * @file d_ui_font.h
 * @brief 统一管理默认 LVGL 字体回退链，向页面借用静态字体。
 */

#ifndef D_UI_FONT_H
#define D_UI_FONT_H

#include "config/app_config.h"
#include "d_error.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

/**
 * @brief 在静态副本中装配 Montserrat、中文、emoji 及可选尾节点的字体回退链。
 * @param[in] fallback_tail 可为 NULL；非空时须为持续有效的静态字体描述符。
 * @return 当前实现始终返回 D_OK。
 * @note 由 UI 线程启动阶段调用；不分配字体数据，不接管尾节点所有权。
 */
d_err_t d_ui_font_init(const lv_font_t *fallback_tail);

/**
 * @brief 借用项目默认字体。
 * @return 初始化前返回 SDK Montserrat 字体；初始化后返回静态回退链首节点。
 * @note 调用方不得释放或修改该字体；不以返回非 NULL 判断整个 UI 已就绪。
 */
const lv_font_t *d_ui_font_get_default(void);

#endif /* D_LVGL_ENABLE */
#endif /* D_UI_FONT_H */
