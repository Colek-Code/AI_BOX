/** @file qwen_subtitle_text.h @brief 两种根 UI 共用的有限 Qwen 文本缓存及句子呈现合同。 */
#ifndef __QWEN_SUBTITLE_TEXT_H__
#define __QWEN_SUBTITLE_TEXT_H__

#include <stdint.h>
#include "lvgl.h"
#include "framework/d_error.h"

#define QWEN_SUBTITLE_TEXT_MAX           2048U
#define QWEN_SUBTITLE_SENTENCE_MAX       32U
#define QWEN_SUBTITLE_SENTENCE_TEXT_MAX  192U

typedef enum {
    QWEN_SUBTITLE_STATE_IDLE = 0,
    QWEN_SUBTITLE_STATE_STREAMING,
    QWEN_SUBTITLE_STATE_FINAL,
} qwen_subtitle_state_t;

typedef struct {
    const char (*sentences)[QWEN_SUBTITLE_SENTENCE_TEXT_MAX];
    uint8_t sentence_count;
    uint8_t is_clipped;
    qwen_subtitle_state_t state;
    d_err_t init_result;
    uint32_t revision;
} qwen_subtitle_render_t;

/**
 * @brief 为 partial 与 final 事件建立一次持久订阅，部分失败时撤销前一项。
 * @return 已订阅或本次成功返回 D_OK，订阅失败返回 D_ERR_IO；Qwen 功能关闭时保持 NOT_READY。
 * @note 由 UI／启动串行调用；缓存与回调为静态生命周期，不随页面销毁取消。失败可在后续调用重试。
 */
d_err_t qwen_subtitle_init(void);
/**
 * @brief 取走原文脏状态，在 UI 线程清洗文本并分句，返回可借用的呈现快照。
 * @param[in] force 非零强制重新准备。
 * @param[in] font 借用的显示字体，用于缺字检测；NULL 使用框架默认字体。
 * @param[out] render 本次重新准备时写入代次、句子数组指针、状态及裁剪信息。
 * @return 本次重新准备返回 1；无强制或脏变化、或 render 为空返回 0，输出保持原值。
 * @note 只在 LVGL 线程调用；句子数组为内部静态存储，至下一次 prepare 前有效，调用方不释放。事件锁只保护原文快照，字形及分句工作在锁外执行。
 */
uint8_t qwen_subtitle_prepare(uint8_t force, const lv_font_t *font,
                              qwen_subtitle_render_t *render);
/**
 * @brief 选择字幕状态的静态短标签。
 * @param[in] state 字幕状态。
 * @param[in] is_clipped 非零表示显示内容达到上限。
 * @return 裁剪优先返回 CLIP；流式为 LIVE，完整为 FINAL，其余为 WAIT。
 */
const char *qwen_subtitle_status_text(qwen_subtitle_state_t state,
                                            uint8_t is_clipped);

#endif /* __QWEN_SUBTITLE_TEXT_H__ */
