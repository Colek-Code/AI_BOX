/**
 * @file d_lvgl_profile.h
 * @brief 提供可选的 UI 执行耗时诊断，不把墙上时间等同于 CPU 执行时间。
 */
#ifndef D_LVGL_PROFILE_H
#define D_LVGL_PROFILE_H

#include <stdint.h>

/* 此头文件同时充当 SDK 探针适配；解析 lv_conf_internal.h 时不得依赖
 * 产品配置或 LVGL 类型，以免形成配置包含环。 */
#if (defined(D_DEBUG_UI_PERFORMANCE) && D_DEBUG_UI_PERFORMANCE) || \
    (defined(LV_USE_PROFILER) && LV_USE_PROFILER)
typedef enum {
    D_LVGL_PROFILE_OBJECT_LAYOUT = 0,
    D_LVGL_PROFILE_DISPATCH,
    D_LVGL_PROFILE_FLUSH_CALLBACK,
    D_LVGL_PROFILE_REFRESH_LAYOUT,
    D_LVGL_PROFILE_SW_RASTER,
    D_LVGL_PROFILE_SW_LABEL,
    D_LVGL_PROFILE_COUNT
} d_lvgl_profile_scope_e;

typedef struct {
    uint32_t elapsed_ms[D_LVGL_PROFILE_COUNT];
    uint32_t calls[D_LVGL_PROFILE_COUNT];
    uint32_t clock_faults;
    uint32_t scope_faults;
    uint8_t valid;
    uint32_t sample_id;
    uint32_t worker_span_ms;
    uint8_t worker_valid;
    uint32_t worker_late_events;
    uint32_t worker_faults;
} d_lvgl_profile_t;

/**
 * @brief 登记已选探针范围的进入或退出时间。
 * @param[in] scope d_lvgl_profile_scope_e 范围编号。
 * @param[in] entering 1 表示进入，0 表示退出；其他值计为诊断错误。
 * @note 原四个 UI 范围只接收 d_lvgl 任务标记；受支持的单软件绘制任务另接收栅格和标签范围。
 * @note 只读时钟量化为 10 ms，重叠墙上时间包含抢占，不是可相加的 CPU 成本。SDK 没有绘制同步信号量等待的专用标记。
 */
void d_lvgl_profile_mark(uint8_t scope, uint8_t entering);
/**
 * @brief 在 UI 线程开始一个诊断周期，登记样本代次和累计值基线。
 * @note 非 UI 线程调用直接返回；嵌套开始会使当前样本无效，不重置在途工作任务范围。
 */
void d_lvgl_profile_begin(void);
/**
 * @brief 在 UI 线程结束诊断周期并冻结本轮快照。
 * @note 不为诊断新增等待。未闭合范围仍作为在途欠账；工作任务与原 UI 范围的有效性分别记录。
 * @note worker_span_ms 包含首个栅格开始至最后结束之间的间隙，不是纯绘制执行时间。
 */
void d_lvgl_profile_end(void);
/**
 * @brief 复制最近一次冻结的诊断周期。
 * @param[out] snapshot 接收值类型快照；NULL 时不处理。
 * @note 仅供 UI 线程查询；其他线程调用时将非空输出清零，不返回跨线程推断的有效样本。
 */
void d_lvgl_profile_get_cycle(d_lvgl_profile_t *snapshot);
#endif
#endif
