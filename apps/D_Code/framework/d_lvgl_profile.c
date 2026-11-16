/**
 * @file d_lvgl_profile.c
 * @brief 接收调试配置下的 LVGL 公共探针并汇总样本，不改变渲染调度。
 */
#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && D_DEBUG_UI_PERFORMANCE
#include "system/includes.h"
#include "system/spinlock.h"
#include "framework/d_lvgl_profile.h"
#include "system/sys_time.h"
#include "lvgl.h"
#include <string.h>

#define D_LVGL_PROFILE_UI_COUNT D_LVGL_PROFILE_SW_RASTER
#if LV_USE_OS == LV_OS_CUSTOM && LV_USE_DRAW_SW && \
    LV_DRAW_SW_DRAW_UNIT_CNT == 1 && !LV_USE_PARALLEL_DRAW_DEBUG
#define D_LVGL_PROFILE_WORKER_SUPPORTED 1U
#else
#define D_LVGL_PROFILE_WORKER_SUPPORTED 0U
#endif

typedef struct {
    uint32_t depth;
    uint32_t started;
    uint32_t sample_id;
    const char *owner;
    uint8_t eligible;
} d_lvgl_profile_worker_span_t;

static d_lvgl_profile_t s_profile_totals;
static d_lvgl_profile_t s_profile_start;
static d_lvgl_profile_t s_profile_cycle;
static uint32_t s_profile_depth[D_LVGL_PROFILE_UI_COUNT];
static uint32_t s_profile_started[D_LVGL_PROFILE_UI_COUNT];
static uint8_t s_profile_active;
static uint8_t s_profile_scope_valid;
static spinlock_t s_profile_lock;
static d_lvgl_profile_worker_span_t s_profile_worker[2];
static uint32_t s_profile_sample_id;
static uint32_t s_profile_worker_first;
static uint32_t s_profile_worker_last;
static uint8_t s_profile_worker_has_raster;
static uint8_t s_profile_worker_valid;

/**
 * @brief 保存入口中断状态并取得探针自旋锁，配合编译器内存屏障发布状态。
 * @return 入口中断状态，须原样交给配对解锁。
 */
static uint32_t d_lvgl_profile_lock(void)
{
    uint32_t flags;
    __asm__ volatile ("" ::: "memory");
    local_irq_save(flags);
    __asm__ volatile ("" ::: "memory");
    spin_acquire(&s_profile_lock);
    __asm__ volatile ("" ::: "memory");
    return flags;
}

/**
 * @brief 释放探针锁并恢复配对记录的入口中断状态。
 * @param[in] flags d_lvgl_profile_lock 返回的中断状态。
 */
static void d_lvgl_profile_unlock(uint32_t flags)
{
    __asm__ volatile ("" ::: "memory");
    spin_release(&s_profile_lock);
    __asm__ volatile ("" ::: "memory");
    local_irq_restore(flags);
    __asm__ volatile ("" ::: "memory");
}

/**
 * @brief 按任务名判断调用者是否为唯一 UI 任务。
 * @param[in] name OS 提供的借用任务名。
 * @return 名称非 NULL 且等于 d_lvgl 时为 1，否则为 0。
 */
static uint8_t d_lvgl_profile_is_ui(const char *name)
{
    return name != NULL && strcmp(name, "d_lvgl") == 0;
}

/**
 * @brief 读取只读时钟并计算短跨度，倒退时记录诊断故障。
 * @param[in] started 当前范围开始的毫秒计数。
 * @return 时钟未倒退时为差值；倒退或回绕时为 0 并增加 clock_faults。
 * @note 在探针锁内调用；不调整时钟或实际执行流程。
 */
static uint32_t d_lvgl_profile_elapsed(uint32_t started)
{
    uint32_t ended = timer_get_ms();
    /* 保留原只读 tick 来源；短样本内时钟倒退或回绕时仅使诊断无效，不改变执行流程。 */
    if (ended < started) {
        s_profile_totals.clock_faults++;
        return 0U;
    }
    return ended - started;
}

/**
 * @brief 登记工作任务范围故障并使本轮工作样本无效。
 * @note 调用方持有探针锁；不改变原 UI 范围的有效性。
 */
static void d_lvgl_profile_worker_fault(void)
{
    s_profile_totals.worker_faults++;
    s_profile_worker_valid = 0U;
}

/**
 * @brief 登记迟到的工作任务事件，同时使工作样本无效。
 * @note 调用方持有探针锁；迟到事件不归入新的样本。
 */
static void d_lvgl_profile_worker_late(void)
{
    s_profile_totals.worker_late_events++;
    d_lvgl_profile_worker_fault();
}

/**
 * @brief 判断工作范围是否失去当前样本归属。
 * @param[in] span 探针锁保护的工作范围。
 * @return 周期未激活、范围无资格或样本代次不匹配时为 1。
 */
static uint8_t d_lvgl_profile_worker_is_late(const d_lvgl_profile_worker_span_t *span)
{
    return !s_profile_active || !span->eligible ||
        span->sample_id != s_profile_sample_id;
}

/**
 * @brief 核对标签范围是否仍属于相同任务和样本的在途栅格范围。
 * @param[in] owner OS 任务名指针，仅在短范围内用作身份。
 * @param[in] sample_id 预期的样本编号。
 * @return 栅格已进入且所有者指针和样本编号均匹配时为 1。
 */
static uint8_t d_lvgl_profile_worker_has_parent(const char *owner, uint32_t sample_id)
{
    const d_lvgl_profile_worker_span_t *raster = &s_profile_worker[0];
    return raster->depth != 0U && raster->owner == owner &&
        raster->sample_id == sample_id;
}

/**
 * @brief 登记单工作任务绘制范围的嵌套进入，并冻结样本归属。
 * @param[in] scope 已校验的栅格或标签范围编号。
 * @param[in] owner 本次短范围中借用的 OS 任务身份，不对外发布。
 * @note 调用方持有探针锁；所有者冲突、嵌套溢出或缺少父栅格时只标记诊断失败。
 */
static void d_lvgl_profile_worker_enter(uint8_t scope, const char *owner)
{
    d_lvgl_profile_worker_span_t *span = &s_profile_worker[scope - D_LVGL_PROFILE_SW_RASTER];
    const d_lvgl_profile_worker_span_t *raster = &s_profile_worker[0];
    if ((span->depth != 0U && span->owner != owner) || span->depth == UINT32_MAX ||
        (scope == D_LVGL_PROFILE_SW_LABEL &&
         !d_lvgl_profile_worker_has_parent(owner, raster->sample_id))) {
        d_lvgl_profile_worker_fault();
        return;
    }
    if (span->depth++ != 0U) {
        if (d_lvgl_profile_worker_is_late(span)) d_lvgl_profile_worker_late();
        return;
    }
    /* 只在这个短范围尚未闭合时借用任务身份，绝不对外发布指针。 */
    span->owner = owner;
    span->sample_id = scope == D_LVGL_PROFILE_SW_LABEL ? raster->sample_id : s_profile_sample_id;
    span->eligible = s_profile_active && span->sample_id != 0U &&
        (scope != D_LVGL_PROFILE_SW_LABEL || raster->eligible);
    if (d_lvgl_profile_worker_is_late(span)) {
        d_lvgl_profile_worker_late();
        return;
    }
    span->started = timer_get_ms();
    if (scope == D_LVGL_PROFILE_SW_RASTER && !s_profile_worker_has_raster) {
        s_profile_worker_first = span->started;
        s_profile_worker_has_raster = 1U;
    }
}

/**
 * @brief 核对工作范围所有者及代次，最外层退出时累计耗时。
 * @param[in] scope 已校验的栅格或标签范围编号。
 * @param[in] owner 与进入时相同的借用任务身份。
 * @note 调用方持有探针锁；迟到完成只清理原范围，不为新样本累计耗时。
 */
static void d_lvgl_profile_worker_leave(uint8_t scope, const char *owner)
{
    d_lvgl_profile_worker_span_t *span = &s_profile_worker[scope - D_LVGL_PROFILE_SW_RASTER];
    uint32_t ended;
    uint8_t late;
    if (span->depth == 0U || span->owner != owner) {
        d_lvgl_profile_worker_fault();
        return;
    }
    late = d_lvgl_profile_worker_is_late(span);
    if (late) d_lvgl_profile_worker_late();
    span->depth--;
    if (span->depth != 0U) return;
    span->owner = NULL;
    span->eligible = 0U;
    if (late) return;
    if ((scope == D_LVGL_PROFILE_SW_LABEL &&
         !d_lvgl_profile_worker_has_parent(owner, span->sample_id)) ||
        (scope == D_LVGL_PROFILE_SW_RASTER && s_profile_worker[1].depth != 0U)) {
        d_lvgl_profile_worker_fault();
    }
    ended = timer_get_ms();
    if (ended < span->started) {
        d_lvgl_profile_worker_fault();
    } else {
        s_profile_totals.elapsed_ms[scope] += ended - span->started;
    }
    s_profile_totals.calls[scope]++;
    if (scope == D_LVGL_PROFILE_SW_RASTER) s_profile_worker_last = ended;
}

/**
 * @brief 登记 UI 范围的嵌套层数，并在最外层闭合时累计时间。
 * @param[in] scope 原四个 UI 探针范围之一。
 * @param[in] entering 1 为进入，0 为退出；无效输入增加范围错误计数。
 * @note 调用方持有探针锁；未配对退出只记诊断故障。
 */
static void d_lvgl_profile_ui_mark(uint8_t scope, uint8_t entering)
{
    if (scope >= D_LVGL_PROFILE_UI_COUNT || entering > 1U) {
        s_profile_totals.scope_faults++;
        return;
    }
    if (entering) {
        if (s_profile_depth[scope] == 0U) s_profile_started[scope] = timer_get_ms();
        s_profile_depth[scope]++;
        return;
    }
    if (s_profile_depth[scope] == 0U) {
        s_profile_totals.scope_faults++;
        return;
    }
    s_profile_depth[scope]--;
    if (s_profile_depth[scope] == 0U) {
        s_profile_totals.elapsed_ms[scope] += d_lvgl_profile_elapsed(s_profile_started[scope]);
        s_profile_totals.calls[scope]++;
    }
}

/**
 * @brief 按执行者分发探针标记；完整合同见 d_lvgl_profile.h。
 */
void d_lvgl_profile_mark(uint8_t scope, uint8_t entering)
{
    const char *owner = os_current_task();
    uint8_t is_ui = d_lvgl_profile_is_ui(owner);
    uint32_t flags;
    if (scope >= D_LVGL_PROFILE_SW_RASTER && scope < D_LVGL_PROFILE_COUNT) {
        if (!D_LVGL_PROFILE_WORKER_SUPPORTED) return;
        flags = d_lvgl_profile_lock();
        if (owner == NULL || is_ui || entering > 1U) {
            d_lvgl_profile_worker_fault();
        } else if (entering) {
            d_lvgl_profile_worker_enter(scope, owner);
        } else {
            d_lvgl_profile_worker_leave(scope, owner);
        }
        d_lvgl_profile_unlock(flags);
        return;
    }
    if (!is_ui) return;
    flags = d_lvgl_profile_lock();
    d_lvgl_profile_ui_mark(scope, entering);
    d_lvgl_profile_unlock(flags);
}

/**
 * @brief 建立诊断周期基线；完整合同见 d_lvgl_profile.h。
 */
void d_lvgl_profile_begin(void)
{
    uint8_t scope;
    uint8_t worker_debt = 0U;
    uint32_t flags;
    if (!d_lvgl_profile_is_ui(os_current_task())) return;
    flags = d_lvgl_profile_lock();
    if (s_profile_active) {
        s_profile_scope_valid = 0U;
        if (D_LVGL_PROFILE_WORKER_SUPPORTED) d_lvgl_profile_worker_fault();
        d_lvgl_profile_unlock(flags);
        return;
    }
    if (++s_profile_sample_id == 0U) s_profile_sample_id = 1U;
    s_profile_start = s_profile_totals;
    s_profile_cycle.valid = 0U;
    s_profile_cycle.worker_valid = 0U;
    s_profile_scope_valid = 1U;
    s_profile_worker_valid = D_LVGL_PROFILE_WORKER_SUPPORTED;
    s_profile_worker_has_raster = 0U;
    s_profile_worker_first = s_profile_worker_last = 0U;
    for (scope = 0U; scope < D_LVGL_PROFILE_UI_COUNT; scope++) {
        if (s_profile_depth[scope] != 0U) s_profile_scope_valid = 0U;
    }
    for (scope = 0U; scope < 2U; scope++) {
        if (s_profile_worker[scope].depth != 0U) {
            worker_debt = 1U;
            s_profile_worker[scope].eligible = 0U;
        }
    }
    if (worker_debt && D_LVGL_PROFILE_WORKER_SUPPORTED) d_lvgl_profile_worker_fault();
    s_profile_active = 1U;
    d_lvgl_profile_unlock(flags);
}

/**
 * @brief 冻结周期样本及有效性；完整合同见 d_lvgl_profile.h。
 */
void d_lvgl_profile_end(void)
{
    uint8_t scope;
    uint8_t worker_debt = 0U;
    uint32_t flags;
    if (!d_lvgl_profile_is_ui(os_current_task())) return;
    flags = d_lvgl_profile_lock();
    if (!s_profile_active) {
        memset(&s_profile_cycle, 0, sizeof(s_profile_cycle));
        d_lvgl_profile_unlock(flags);
        return;
    }
    for (scope = 0U; scope < D_LVGL_PROFILE_COUNT; scope++) {
        s_profile_cycle.elapsed_ms[scope] = s_profile_totals.elapsed_ms[scope] - s_profile_start.elapsed_ms[scope];
        s_profile_cycle.calls[scope] = s_profile_totals.calls[scope] - s_profile_start.calls[scope];
    }
    for (scope = 0U; scope < D_LVGL_PROFILE_UI_COUNT; scope++) {
        if (s_profile_depth[scope] != 0U) s_profile_scope_valid = 0U;
    }
    /* SDK 在 handler 返回前等待绘制作业；诊断不能再加等待。
     * 即使跨越代次回绕，未闭合的工作范围仍保留为欠账。 */
    for (scope = 0U; scope < 2U; scope++) {
        if (s_profile_worker[scope].depth != 0U) {
            worker_debt = 1U;
            s_profile_worker[scope].eligible = 0U;
        }
    }
    if (worker_debt && D_LVGL_PROFILE_WORKER_SUPPORTED) d_lvgl_profile_worker_fault();
    s_profile_cycle.sample_id = s_profile_sample_id;
    s_profile_cycle.worker_span_ms = 0U;
    if (s_profile_worker_has_raster) {
        if (s_profile_worker[0].depth != 0U || s_profile_worker_last < s_profile_worker_first) {
            if (s_profile_worker_valid) d_lvgl_profile_worker_fault();
        } else {
            s_profile_cycle.worker_span_ms = s_profile_worker_last - s_profile_worker_first;
        }
    }
    s_profile_cycle.worker_late_events = s_profile_totals.worker_late_events - s_profile_start.worker_late_events;
    s_profile_cycle.worker_faults = s_profile_totals.worker_faults - s_profile_start.worker_faults;
    s_profile_cycle.worker_valid = s_profile_worker_valid && s_profile_cycle.worker_faults == 0U;
    s_profile_cycle.clock_faults = s_profile_totals.clock_faults - s_profile_start.clock_faults;
    s_profile_cycle.scope_faults = s_profile_totals.scope_faults - s_profile_start.scope_faults;
    s_profile_cycle.valid = s_profile_scope_valid && s_profile_cycle.clock_faults == 0U &&
        s_profile_cycle.scope_faults == 0U;
    s_profile_active = 0U;
    d_lvgl_profile_unlock(flags);
}

/**
 * @brief 复制冻结的诊断样本；完整合同见 d_lvgl_profile.h。
 */
void d_lvgl_profile_get_cycle(d_lvgl_profile_t *snapshot)
{
    uint32_t flags;
    if (snapshot == NULL) return;
    if (!d_lvgl_profile_is_ui(os_current_task())) {
        memset(snapshot, 0, sizeof(*snapshot));
        return;
    }
    flags = d_lvgl_profile_lock();
    *snapshot = s_profile_cycle;
    d_lvgl_profile_unlock(flags);
}
#endif
