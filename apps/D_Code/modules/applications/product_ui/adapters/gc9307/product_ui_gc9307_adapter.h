/** @file product_ui_gc9307_adapter.h @brief GC9307 产品配置下的 UI 状态、设置及连接适配入口。 */

#ifndef PRODUCT_UI_GC9307_ADAPTER_H
#define PRODUCT_UI_GC9307_ADAPTER_H

#include "config/app_config.h"
#include <stdint.h>

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT) && \
    (D_BOARD_IS_GC9307)

/**
 * @brief 串行初始化状态汇聚，启动阶段允许 ADC 尚未提供有效值。
 * @return 首次初始化返回 0；重复调用返回本次完整刷新结果，失败为 -1。
 */
int product_ui_status_gc9307_init(void);
/**
 * @brief 周期刷新可信时间、电量和 WiFi 缓存投影。
 * @param[in] elapsed_ms 本轮 UI 周期的毫秒数。
 * @note 未初始化时不操作；启动期短周期重试，WiFi 版本变化触发额外刷新。
 */
void product_ui_status_gc9307_process(uint32_t elapsed_ms);
/**
 * @brief 初始化设备设置投影并注册单字节命令处理器。
 * @return 成功返回 0；绑定或发布失败返回 -1，并撤销命令绑定。
 * @note 显示初始化应先完成；本函数依赖显示软件状态，不证明原生硬件验收。
 */
int product_ui_settings_gc9307_init(void);
/**
 * @brief 按设备音量代次刷新设置投影，成功发布后才记住已见代次。
 */
void product_ui_settings_gc9307_process(void);

#if D_FEATURE_WIFI
/**
 * @brief 串行初始化连接快照、同步回调及请求路由。
 * @return 装配成功返回 0；初始化、命令绑定或服务发布失败返回 -1。
 * @note 后台无线未开启仍可初始化成功并保持 UNAVAILABLE；不得并发重复初始化以代替拆卸。
 */
int product_ui_connection_gc9307_init(void);
#endif

#endif

#endif /* PRODUCT_UI_GC9307_ADAPTER_H */
