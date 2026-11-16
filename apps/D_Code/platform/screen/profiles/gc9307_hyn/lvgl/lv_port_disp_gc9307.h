/**
 * @file lv_port_disp_gc9307.h
 * @brief 声明 GC9307 配置专用的 LVGL 显示端口能力，不承载产品策略。
 * D++: 归属 D_Code 的 GC9307/HYN 显示配置。
 */

#ifndef __LV_PORT_DISP_GC9307_H__
#define __LV_PORT_DISP_GC9307_H__

#include "config/app_config.h"
#include "gc9307_rotation.h"

#if defined(D_LCD_ENABLE) && (D_BOARD_IS_GC9307)

/**
 * @brief 登记一次完整页面过渡，收集后按 TE 对齐传送物理完整帧。
 * @param[in] transition_id 调用方提供的非零页面过渡编号；零不登记。
 * @note 由 UI 线程登记；不等待收集或 DMA 完成，不改变业务页面生命周期。
 */
void lv_port_disp_gc9307_begin_full_frame_transition(
    unsigned int transition_id);

/**
 * @brief 请求下一轮刷新采用完整合帧，刷新前以最后的浮层位置为准。
 * @note 仅 UI 线程调用；只在 Product UI 生效，复用既有合帧缓冲，不同步发送整帧。
 */
void lv_port_disp_gc9307_request_composed_frame(void);

/**
 * @brief 在短临界区内复制当前渲染作业使用的方向身份。
 * @param[out] orientation 接收安装角度、方向代次及在途请求编号；NULL 时不处理。
 * @note 供 UI 与触摸手势适配读取，不调用 LVGL；结果是渲染方向，不能等同于最后已确认可见方向。需随作业保持同一方向时保存本次值，不在发送时重新查询。
 */
void lv_port_disp_gc9307_get_orientation(gc9307_frame_orientation_t *orientation);
/**
 * @brief 查询方向事务是否要求阻止新输入。
 * @return 方向阶段不是 IDLE 时为非零。
 * @note 仅供 UI 执行者读取；故障阶段也保留输入门禁，不以超时自动放行。
 */
uint8_t lv_port_disp_gc9307_input_blocked(void);

#if D_DEBUG_UI_PERFORMANCE
/* 仅 UI 执行者维护累计计数；不是 DMA 完成或 CPU 时间合同。
 * total_ms 计既有信号量等待及唤醒调度，无符号差值可跨一次回绕。 */
typedef struct {
    uint32_t total_ms;
    uint32_t count;
    uint32_t submitted;
    uint32_t ready;
} gc9307_ui_wait_stats_t;
/**
 * @brief 在 UI 执行者中复制刷新等待累计计数。
 * @param[out] out 接收累计等待、次数及提交/就绪计数；NULL 时不处理。
 * @note 计时包括信号量唤醒后的调度，不代表 CPU 独占或底层 DMA 完成。无符号差值支持单次计数回绕。
 */
void lv_port_disp_gc9307_get_ui_wait_stats(gc9307_ui_wait_stats_t *out);
#endif


#if D_CAMERA_PREVIEW_ENABLE
#include "lvgl.h"
/**
 * @brief 在 UI 线程为完整图像刷新预先登记区域收集请求。
 * @param[in] area 待完整失效的逻辑图像区域，调用时复制，不保存指针。
 * @return 成功为新分配的非零请求编号；区域不符合能力范围时为 0，并清当前区域预约。
 * @note 先登记再让完整图像失效；后续作业冻结该请求，图像区域只合成一次物理传输。
 */
unsigned int lv_port_disp_gc9307_begin_region_update(const lv_area_t *area);
/**
 * @brief 只取消当前匹配的区域收集预约。
 * @param[in] request_id 要取消的非零区域请求编号。
 * @note UI 线程调用；已在执行的物理传输按原路径完成，不从调用线程释放刷新缓冲。
 */
void lv_port_disp_gc9307_cancel_region_update(unsigned int request_id);
#endif

#endif
#endif /* __LV_PORT_DISP_GC9307_H__ */
