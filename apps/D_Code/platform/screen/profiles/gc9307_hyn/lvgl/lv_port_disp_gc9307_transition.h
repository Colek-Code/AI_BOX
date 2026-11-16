/**
 * @file lv_port_disp_gc9307_transition.h
 * @brief 定义 GC9307 完整合帧的收集及借用接口，不拥有产品页面生命周期。
 * D++: 归属 D_Code GC9307/HYN 显示配置。
 */

#ifndef __LV_PORT_DISP_GC9307_TRANSITION_H__
#define __LV_PORT_DISP_GC9307_TRANSITION_H__

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "gc9307_rotation.h"

typedef enum {
    GC9307_TRANSITION_STAGE_IGNORED = 0,
    GC9307_TRANSITION_STAGE_STAGED,
    GC9307_TRANSITION_STAGE_READY,
    GC9307_TRANSITION_STAGE_ERROR,
} gc9307_transition_stage_result_e;

typedef struct {
    uint8_t *frame_data;
    uint32_t frame_bytes;
    uint32_t logical_parts;
    uint32_t logical_bytes;
    uint32_t collect_ms;
    unsigned int transition_id;
    gc9307_frame_orientation_t orientation;
} gc9307_transition_frame_t;

typedef void (*gc9307_transition_frame_observer_t)(
    const gc9307_transition_frame_t *frame, int transfer_result,
    void *user_data);

/**
 * @brief 按非零过渡编号登记下一轮完整画面收集。
 * @param[in] transition_id 页面过渡编号，零时不处理。
 * @note 由显示端口控制调用，不提供任意线程并发保护；复用缓冲前仍须遵守刷新所有者的完成合同。非 Product UI 配置为空操作。
 */
void gc9307_transition_arm(unsigned int transition_id);
/**
 * @brief 由刷新所有者登记编号为零的普通动画合帧。
 * @note 与页面过渡共用唯一 staging 缓冲，不新增所有者；非 Product UI 配置为空操作。
 */
void gc9307_transition_arm_composed_frame(void);
/**
 * @brief 完成无法确认时永久禁止复用完整帧缓冲。
 * @note 只清元数据、超时或迟到 IRQ 都不能解除；本次运行中保持到重启。非 Product UI 配置无该缓冲。
 */
void gc9307_transition_block_reuse(void);
/**
 * @brief 读取当前页面过渡编号。
 * @return 未登记过渡或非 Product UI 时为 0。
 * @note 零不等于缓冲空闲：普通动画合帧本来使用零编号；不作为 DMA 完成或可复用证明。
 */
unsigned int gc9307_transition_get_active_id(void);
/**
 * @brief 按方向身份、条带顺序和末片事实收集一张完整物理帧。
 * @param[in] orientation 本作业冻结的安装角、方向代次及请求编号。
 * @param[in] transition_id 与登记事务相匹配的过渡编号，动画合帧可为零。
 * @param[in] area 覆盖完整逻辑宽度且按行连续的条带。
 * @param[in] px_map 本条带 RGB565 像素，调用中只读复制。
 * @param[in] is_last 是否为本轮 LVGL 刷新的最后条带。
 * @param[out] frame 仅 READY 时写入完整视图；其他结果不保证初始化此输出。
 * @return IGNORED 为无对应事务，STAGED 为未齐帧，READY 为借出完整缓冲，ERROR 为收集或资源失败；均不表示 DMA 已完成。
 * @note 只由唯一刷新所有者操作 staging；READY 后其缓冲继续借给传输，只有确认完成才能开始覆盖。非 Product UI 配置始终 IGNORED。
 * @note 角度、代次、请求编号在首片冻结；后续不符会保留重绘意图并重置收集元数据，不把旧方向混入新完整帧。
 */
gc9307_transition_stage_result_e gc9307_transition_stage(
    const gc9307_frame_orientation_t *orientation,
    unsigned int transition_id, const lv_area_t *area,
    const uint8_t *px_map, bool is_last,
    gc9307_transition_frame_t *frame);
/**
 * @brief 取走并清除完整合帧失败后的重绘意图。
 * @return 有待重绘意图为 true，否则为 false。
 * @note 由显示端口在刷新确认边界消费；不是资源释放确认。
 */
bool gc9307_transition_take_redraw_request(void);
/**
 * @brief 登记完整页面过渡帧的借用观察者及上下文。
 * @param[in] observer 可为 NULL；由刷新所有者在发布时调用。
 * @param[in] user_data 借用上下文，须保持到所有在途观察调用结束。
 * @note 本接口不等待旧回调退出；由显示装配保证登记与发布的串行边界，不能据替换或清空槽立即释放上下文。非 Product UI 配置忽略。
 */
void gc9307_transition_set_frame_observer(
    gc9307_transition_frame_observer_t observer, void *user_data);
/**
 * @brief 把本次完整页面过渡帧及传输结果通知已绑定观察者。
 * @param[in] frame 本次帧描述符及借用像素；NULL 时不通知。
 * @param[in] transfer_result 底层传输返回值，0 才是当前路径确认的 DMA 成功。
 * @note 刷新所有者调用；只在回调期间借用描述符和像素，不负责替观察者保存副本，也不隐式释放 staging。
 */
void gc9307_transition_publish_frame(
    const gc9307_transition_frame_t *frame, int transfer_result);

#endif /* __LV_PORT_DISP_GC9307_TRANSITION_H__ */
