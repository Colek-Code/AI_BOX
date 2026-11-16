/**
 * @file lcd_gc9307.h
 * @brief 声明 GC9307 的 TE 边沿等待与带确认的传输能力，帧边界由显示端口管理。
 */

#ifndef __LCD_GC9307_H__
#define __LCD_GC9307_H__

#include <stdint.h>

typedef enum {
    D_LCD_GC9307_TE_WAIT_TIMEOUT = -1,
    D_LCD_GC9307_TE_WAIT_EDGE = 0,
    D_LCD_GC9307_TE_WAIT_BYPASSED = 1,
} d_lcd_gc9307_te_wait_result_e;

typedef struct {
    uint32_t irq_count;
    uint32_t wait_ok_count;
    uint32_t timeout_count;
    uint32_t sem_post_count;
    uint32_t stale_wakeup_count;
    uint32_t request_count;
    uint32_t last_irq_ms;
    uint32_t irq_period_ms;
    uint32_t last_request_ms;
    uint32_t last_request_irq_count;
    uint32_t last_edge_ms;
    uint32_t last_edge_irq_count;
    uint32_t last_request_to_edge_ms;
    uint8_t is_registered;
} d_lcd_gc9307_te_stats_t;

typedef struct {
    uint32_t submit_count;   /**< 接受的像素提交次数。 */
    uint32_t callback_count; /**< 等待中确认计数推进的次数，不是所有原始 IRQ。 */
    uint32_t complete_count; /**< 带回调复核的成功完成次数。 */
    uint32_t failure_count;  /**< 等待或回调确认失败次数。 */
    uint32_t last_submit_ms; /**< 最近异步提交的毫秒时钟。 */
} d_lcd_gc9307_transfer_stats_t;

typedef struct {
    uint16_t xs; /**< 物理起始列。 */
    uint16_t xe; /**< 物理结束列。 */
    uint16_t ys; /**< 物理起始行。 */
    uint16_t ye; /**< 物理结束行。 */
    uint8_t *data; /**< 调用方保持至完成确认或保留策略结束的借用像素。 */
    uint32_t len; /**< 有效字节数；调用方保证与区域及源容量匹配，驱动不交叉核验。 */
} d_lcd_gc9307_flush_t;

/**
 * @brief 在显示所有者请求时，有界等待本次请求之后的一个 TE 边沿。
 * @return EDGE 表示观察到新边沿；TIMEOUT 表示没有确认边沿；能力关闭或未登记时为 BYPASSED。
 * @note 仅任务上下文、单等待者调用；先清旧信号并记录中断计数，信号量唤醒本身不当作新边沿。函数不决定一帧分几次发送，也不改变 TE 参数。
 */
d_lcd_gc9307_te_wait_result_e d_lcd_gc9307_wait_te_frame(void);
/**
 * @brief 复制 TE 中断、请求、等待及最近边沿的诊断计数。
 * @param[out] stats 接收统计；NULL 时不处理，能力关闭时清零。
 * @note 字段逐项读取，不保证跨字段为一个原子时刻；不能把诊断计数当作资源释放票据。
 */
void d_lcd_gc9307_get_te_stats(d_lcd_gc9307_te_stats_t *stats);

/**
 * @brief 组合窗口、写命令及像素提交，不自行等待 TE 或 DMA。
 * @param[in] flush 有效物理区域及借用像素描述，含非零长度。
 * @return 全部准备且提交接受为 0；任一步失败为 -1。
 * @note 成功后由调用方配对 flush_wait，单显示所有者保证串行。
 */
int d_lcd_gc9307_flush_start(const d_lcd_gc9307_flush_t *flush);
/**
 * @brief 在异步 SPI 可用时设置已校验的物理矩形窗口。
 * @param[in] xs 物理起始横坐标。
 * @param[in] xe 物理结束横坐标。
 * @param[in] ys 物理起始纵坐标。
 * @param[in] ye 物理结束纵坐标。
 * @return 0 表示区域合法且已提交窗口命令；区域无效或异步能力不可用为 -1。
 * @note 只准备窗口，不提交像素或等待 TE；命令兼容接口为 void，不额外保证每个命令写入结果。
 */
int d_lcd_gc9307_prepare_area(uint16_t xs, uint16_t xe,
                              uint16_t ys, uint16_t ye);
/**
 * @brief 在异步 SPI 可用时提交面板像素写命令。
 * @return 0 为命令路径已执行；异步能力不可用为 -1。
 * @note 不提交像素数据，不声明 DMA 已完成。
 */
int d_lcd_gc9307_prepare_write(void);
/**
 * @brief 提交一段像素并取得本驱动唯一的在途传输。
 * @param[in] data 借用的像素源，保持到传输确认或所属缓冲保留策略结束。
 * @param[in] len 非零有效字节数。
 * @return 0 为提交已接受；参数无效、已有在途、异步不可用或写入失败为 -1。
 * @note 只由显示所有者串行调用；成功后必须调用 flush_wait 完成确认，不能仅据 submit_count 增长复用源。
 */
int d_lcd_gc9307_flush_pixels_start(uint8_t *data, uint32_t len);
/**
 * @brief 等待本驱动在途像素，并复核 SPI 回调计数确实推进。
 * @param[in] timeout_ticks 本次底层等待的 SDK tick 数。
 * @return 0 为等待成功且回调相对提交基准已变化；没有在途、超时、缺少回调或底层失败为 -1。
 * @note 返回前结束本地 active 并抬高片选；失败不证明硬件已停止读取源缓冲，上层必须保留对应缓冲，不能依本地标志清零提前释放。
 */
int d_lcd_gc9307_flush_wait(uint32_t timeout_ticks);
/**
 * @brief 复制本驱动提交、回调、确认、失败及最近提交时钟。
 * @param[out] stats 调用方持有的诊断结构；NULL 时不处理。
 * @note 供单显示所有者诊断使用，字段不是独立全局总线停止合同。
 */
void d_lcd_gc9307_get_transfer_stats(
    d_lcd_gc9307_transfer_stats_t *stats);

#endif /* __LCD_GC9307_H__ */
