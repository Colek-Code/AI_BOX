/**
 * @file lv_port_disp_gc9307_trace.h
 * @brief 汇总有界 GC9307 帧及 TE 耗时诊断，不改变传输或资源策略。
 * D++: 归属 D_Code GC9307/HYN 显示配置。
 */

#ifndef __LV_PORT_DISP_GC9307_TRACE_H__
#define __LV_PORT_DISP_GC9307_TRACE_H__

#include <stdbool.h>
#include <stdint.h>
#include "config/app_config.h"
#include "lvgl.h"

typedef struct {
    const lv_area_t *logical; /**< 仅在本次统计调用期间借用的逻辑矩形。 */
    uint32_t logical_bytes;
    uint32_t physical_bytes; /**< 本片物理字节计量；是否确认发送看独立完成标志。 */
    uint32_t rotate_ms;
    uint32_t wait_ms;
    uint32_t te_to_dma_ms;
    uint32_t tx_ms;
    int wait_result;
    int transfer_result;
    bool physical_dma_completed; /**< 本片实际观察到的 DMA 完成事实，不由字节数推断。 */
    bool is_first;
    bool waited_for_te;
    bool is_last;
    unsigned int transition_id;
} gc9307_port_trace_part_t;

#ifdef D_GC9307_FRAME_TRACE_ENABLE
/**
 * @brief 在刷新所有者中开始一帧诊断，并按需建立报告窗口。
 * @param[in] now_ms 本帧开始的毫秒计数。
 * @param[in] transition_id 本帧页面过渡编号，普通刷新可为零。
 * @note 诊断关闭时为无操作宏；不登记新的业务或 DMA 事务。
 */
void gc9307_port_trace_frame_begin(uint32_t now_ms,
                                   unsigned int transition_id);
/**
 * @brief 累计一个逻辑刷新片段的字节、耗时和完成计数。
 * @param[in] part 借用的片段探针；NULL 或无逻辑区域时忽略。
 * @note 只读消费调用方已经观察的事实；重叠墙上耗时不能当作可相加的 CPU 时间。
 */
void gc9307_port_trace_part(const gc9307_port_trace_part_t *part);
/**
 * @brief 在最后逻辑片段完成时结算帧诊断并按窗口输出。
 * @param[in] now_ms 本帧结束时的毫秒计数。
 * @note 逻辑帧结算不代表每片都成功传屏；物理完成来自独立标志，日志保留原格式。
 */
void gc9307_port_trace_frame_finish(uint32_t now_ms);
#else
#define gc9307_port_trace_frame_begin(now_ms, transition_id) ((void)0)
#define gc9307_port_trace_part(part) ((void)0)
#define gc9307_port_trace_frame_finish(now_ms) ((void)0)
#endif

#endif /* __LV_PORT_DISP_GC9307_TRACE_H__ */
