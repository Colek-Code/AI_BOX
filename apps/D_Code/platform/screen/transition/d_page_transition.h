/**
 * @file d_page_transition.h
 * @brief 声明平台拥有的页面过渡能力，不依赖具体产品页面。
 */

#ifndef D_PAGE_TRANSITION_H
#define D_PAGE_TRANSITION_H

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include <stdint.h>

typedef enum {
    PAGE_TRANSITION_IMMEDIATE = 0,
    PAGE_TRANSITION_TE_FRAME,
} page_transition_policy_e;

/**
 * @brief 按板型将页面过渡策略转交显示配置。
 * @param[in] policy 过渡策略；GC9307 的 TE_FRAME 策略请求完整帧过渡。
 * @param[in] transition_id 调用方为本次过渡分配的编号。
 * @note 仅供 LVGL 线程调用；只登记请求，不等待 TE 或 DMA，不通过 void 返回证明显示完成。其他板型当前不执行额外过渡。
 */
void d_page_transition_begin(page_transition_policy_e policy,
                             uint32_t transition_id);

/**
 * @brief 请求把移动浮层的更新合并到下一张完整帧。
 * @note 仅供 LVGL 线程调用；GC9307 Product UI 执行，其他组合不操作。不在这里发送像素或等待 DMA。
 */
void d_screen_request_composed_frame(void);

#endif /* D_LVGL_ENABLE */
#endif /* D_PAGE_TRANSITION_H */
