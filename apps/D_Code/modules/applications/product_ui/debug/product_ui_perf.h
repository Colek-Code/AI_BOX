/** @file product_ui_perf.h @brief 按宏启用的 UI 聚合性能观察，区分墙钟跨度、等待与实际 worker 统计。 */

#ifndef __PRODUCT_UI_PERF_H__
#define __PRODUCT_UI_PERF_H__

#include <stdint.h>

/**
 * @brief 在 UI 周期按需安装显示观察器，并每两秒输出聚合性能及内存统计。
 * @param[in] elapsed_ms 本轮 UI 间隔毫秒数。
 * @note D_DEBUG_UI_PERFORMANCE 关闭时为空操作；DRAW 跨度包含等待和任务抢占，不能作为纯光栅 CPU 时间。
 */
void product_ui_perf_tick(uint32_t elapsed_ms);
/**
 * @brief 在周期结束消费累计等待样本并记录本报告窗口的峰值周期及帧。
 * @param[in] update_ms 调用方测得的本轮更新跨度。
 * @param[in] render_ms 调用方测得的本轮处理／渲染跨度。
 * @note 首次等待样本仅建基线，不把启动历史归入本轮；宏关闭时为空操作。
 */
void product_ui_perf_cycle(uint32_t update_ms, uint32_t render_ms);
/**
 * @brief 按启用宏输出页面创建经过时间。
 * @param[in] name 调用期间借用的页面名，NULL 时不输出。
 * @param[in] started_at_ms 页面创建开始时的 LVGL 毫秒计数。
 */
void product_ui_perf_page_created(const char *name, uint32_t started_at_ms);

#endif /* __PRODUCT_UI_PERF_H__ */
