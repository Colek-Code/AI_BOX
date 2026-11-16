/**
 * @file lv_port_disp_gc9307_region.h
 * @brief 在一次物理传输前收集完整图像区域；资源只归刷新所有者管理。
 * @author D_Code
 * @date 2026-09-12
 */
#ifndef __LV_PORT_DISP_GC9307_REGION_H__
#define __LV_PORT_DISP_GC9307_REGION_H__
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
#include "gc9307_rotation.h"

#define GC9307_REGION_CAPACITY_BYTES (64U * 1024U)

typedef struct {
    unsigned int id;
    lv_area_t logical;
} gc9307_region_request_t;

typedef struct {
    const lv_area_t *logical;
    const uint8_t *pixels;
    uint32_t stride;
    uint32_t now_ms;
    bool is_last_refresh;
    gc9307_frame_orientation_t orientation;
} gc9307_region_part_t;

typedef struct {
    uint8_t *pixels;
    lv_area_t physical;
    uint32_t bytes;
    uint32_t parts;
    uint32_t collect_ms;
    unsigned int request_id;
    gc9307_frame_orientation_t orientation;
} gc9307_region_frame_t;

typedef enum {
    GC9307_REGION_BYPASS = 0,
    GC9307_REGION_STAGED,
    GC9307_REGION_READY,
    GC9307_REGION_ERROR,
} gc9307_region_result_e;

/**
 * @brief 校验区域处于逻辑画面内且像素总量不超过静态容量。
 * @param[in] area 待校验的逻辑区域。
 * @return 有效为 true；NULL、负坐标、倒置、越界或容量超限为 false。
 */
bool gc9307_region_area_valid(const lv_area_t *area);
/**
 * @brief 按冻结区域请求及方向收集本次像素条带。
 * @param[in] request 本作业按值冻结的区域请求。
 * @param[in] part 本条带区域、像素、跨度、时钟及方向。
 * @param[out] frame 有效输入时先清零；READY 时包含静态区域缓冲的借用视图。
 * @return BYPASS 为沿普通刷新处理，STAGED 为尚未收齐，READY 为借出完整物理区域，ERROR 为失败；均不是 DMA 完成。
 * @note 只由唯一刷新所有者调用；READY 缓冲保持到其 DMA 确认完成。方向角/代次不符或条带顺序失败会请求整帧重绘，不从其他线程释放缓冲。
 * @note 缓冲已因完成不确定而冻结时返回 BYPASS；元数据丢弃不解除该永久门禁。
 */
gc9307_region_result_e gc9307_region_stage(const gc9307_region_request_t *request,
                                          const gc9307_region_part_t *part,
                                          gc9307_region_frame_t *frame);
/**
 * @brief 清除区域收集元数据，不清像素或解除资源冻结。
 * @note 只供刷新所有者在安全边界调用；不能以清元数据代替确认 DMA 停止。
 */
void gc9307_region_discard(void);
/**
 * @brief 将区域像素缓冲保留到本次固件运行结束。
 * @note 物理传输完成不确定时由刷新所有者调用；迟到回调或普通元数据重置不能解除。普通局部刷新仍可沿独立缓冲继续。
 */
void gc9307_region_block_reuse(void);
/**
 * @brief 取走并清除一次失败区域的重绘请求编号。
 * @return 待重绘的区域编号；无请求为 0。
 * @note UI 只在刷新所有者确认本轮作业结束后读取，不与区域收集并发修改。
 */
unsigned int gc9307_region_take_redraw_request(void);
#endif
