/**
 * @file d_lvgl.h
 * @brief 提供有界等待的 LVGL 任务启动及只发布一次的终态快照。
 */

#ifndef D_LVGL_H
#define D_LVGL_H

#include "config/app_config.h"
#include "d_error.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

/* AC791 支持的任务优先级上限；不引入私有 OS 配置头。 */
#define D_LVGL_WORKER_PRIORITY_MAX 30U

typedef enum {
    D_LVGL_INIT_NOT_STARTED = 0, /**< 尚未开始本次启动。 */
    D_LVGL_INIT_STARTING,       /**< 已接受唯一启动，等待任务发布终态。 */
    D_LVGL_INIT_LATE_PENDING,   /**< 同步等待已分离，后台初始化仍可能完成。 */
    D_LVGL_INIT_READY,          /**< 已发布成功终态。 */
    D_LVGL_INIT_FAILED,         /**< 已发布失败终态，不等于运行期 UI 故障。 */
} d_lvgl_init_state_e;

/** @brief UI 任务内的同步装配回调；D_OK 为成功，失败阻止进入周期循环。 */
typedef d_err_t (*d_lvgl_bootstrap_fn_t)(void);
/** @brief UI 任务周期回调；elapsed_ms 为本轮经过的毫秒数。 */
typedef void (*d_lvgl_update_fn_t)(uint32_t elapsed_ms);
/** @brief UI 任务周期诊断回调；分别报告产品更新与 LVGL handler 的墙上耗时，不表示 CPU 独占时间。 */
typedef void (*d_lvgl_cycle_complete_fn_t)(uint32_t update_ms, uint32_t render_ms);
/** @brief 初始化终态通知；result 是最终结果，context 为调用方保持到回调结束的借用上下文。 */
typedef void (*d_lvgl_init_complete_cb_t)(d_err_t result, void *context);

typedef struct {
    d_lvgl_bootstrap_fn_t bootstrap;
    d_lvgl_update_fn_t update;
    d_lvgl_cycle_complete_fn_t cycle_complete_cb;
    /* 借用到固件结束；仅传 NULL 或静态字体描述符。 */
    const lv_font_t *font_fallback_tail;
    d_lvgl_init_complete_cb_t complete_cb;
    /* 上下文必须保持到异步完成回调执行结束；等待超时不解除借用。 */
    void *complete_context;
    /* 0 表示沿用优先级 1；非零值不得超过上方上限。 */
    uint8_t worker_priority;
} d_lvgl_init_spec_t;

typedef struct {
    d_lvgl_init_state_e state; /**< 启动阶段，不代表全部产品服务状态。 */
    d_err_t result;            /**< 仅在终态发布后作为最终结果解释。 */
    uint8_t is_result_published; /**< 与结果码独立的终态发布事实。 */
} d_lvgl_init_snapshot_t;

/**
 * @brief 按给定规格启动唯一 LVGL 任务，并有界等待初始化终态。
 * @param[in] spec 启动规格，结构体被复制；bootstrap 必须非 NULL，优先级不超过上限。
 * @return D_OK 表示终态成功；D_ERR_BUSY 表示已在启动；D_ERR_TIMEOUT 表示同步等待分离但后台仍可能继续；其他错误见终态快照。
 * @note 只允许一次启动；再次调用在终态已发布时返回该结果，不替换配置。仅启动线程调用，不能从 LVGL 任务等待自身。
 * @note 回调与上下文为借用：字体须存活至固件结束，完成上下文须保持到异步回调运行结束；超时不取消任务或提前释放它们。
 * @note 终态只发布一次，完成回调在临界区外调用；正常异步路径由 LVGL 任务调用，创建失败路径可由初始化调用线程发布。
 */
d_err_t d_lvgl_init(const d_lvgl_init_spec_t *spec);
/**
 * @brief 在短临界区内复制 LVGL 初始化事实。
 * @param[out] snapshot 调用方持有的值类型快照。
 * @return D_OK 表示快照已复制；snapshot 为 NULL 时返回 D_ERR_INVALID_ARG。
 * @note 只读查询，不等待初始化完成，不调用 LVGL。result 在 is_result_published 为真后才是最终结果。
 */
d_err_t d_lvgl_get_init_snapshot(d_lvgl_init_snapshot_t *snapshot);

#endif /* D_LVGL_ENABLE */
#endif /* D_LVGL_H */
