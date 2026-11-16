/**
 * @file d_lvgl.c
 * @brief 实现 LVGL 任务生命周期及终态发布，不把等待超时当成初始化取消。
 */

#include "config/app_config.h"
#include "system/includes.h"
#include "os/os_api.h"

#ifdef D_LVGL_ENABLE

#include "d_lvgl.h"
#include "d_lvgl_profile.h"
#include "d_ui_font.h"
#include "framework/compat/d_stdio_compat.h"
#include "d_lvgl_port.h"

#define LOG_TAG "[LVGL] "
#define D_LVGL_INIT_TIMEOUT_TICKS (5 * OS_TICKS_PER_SEC)

static OS_SEM s_lvgl_init_sem;
static d_lvgl_init_spec_t s_lvgl_init_spec;
static volatile d_lvgl_init_state_e s_lvgl_init_state =
    D_LVGL_INIT_NOT_STARTED;
static volatile d_err_t s_lvgl_init_result = D_ERR_NOT_READY;
static volatile uint8_t s_is_result_published;
static uint8_t s_is_init_started;
static uint8_t s_is_wait_detached;
/* 初始化最多执行一次，静态信号量保留到固件结束。这样可避免超时边界上的发布者
 * 退出临界区后，向已被等待线程删除的 OS 对象发通知。 */
static uint8_t s_is_sem_created;
static uint8_t s_is_callback_claimed;

/**
 * @brief 把初始化结果映射到成功或失败终态。
 * @param[in] result 本次初始化结果。
 * @return D_OK 对应 READY，其他结果对应 FAILED。
 */
static d_lvgl_init_state_e d_lvgl_terminal_state(d_err_t result)
{
    return (result == D_OK) ? D_LVGL_INIT_READY : D_LVGL_INIT_FAILED;
}

/**
 * @brief 只接纳首个初始化终态，锁外通知等待者和完成回调。
 * @param[in] result 待发布的初始化结果。
 * @return 已经发布或本次接纳的最终结果。
 * @note 临界区只登记结果及回调归属；信号量与回调在退出临界区后执行。静态信号量不因等待超时而销毁。
 */
static d_err_t d_lvgl_publish_terminal(d_err_t result)
{
    d_lvgl_init_complete_cb_t callback = NULL;
    void *callback_context = NULL;
    uint8_t should_post = 0U;
    uint8_t is_late = 0U;

    OS_ENTER_CRITICAL();
    if (s_is_result_published != 0U) {
        result = s_lvgl_init_result;
    } else {
        s_lvgl_init_result = result;
        s_lvgl_init_state = d_lvgl_terminal_state(result);
        s_is_result_published = 1U;
        should_post = s_is_sem_created;
        is_late = s_is_wait_detached;
        if ((s_lvgl_init_spec.complete_cb != NULL) &&
            (s_is_callback_claimed == 0U)) {
            s_is_callback_claimed = 1U;
            callback = s_lvgl_init_spec.complete_cb;
            callback_context = s_lvgl_init_spec.complete_context;
        }
    }
    OS_EXIT_CRITICAL();

    if (is_late != 0U) {
        printf(LOG_TAG "Late UI initialization finished: %d\n", result);
    }
    if (should_post != 0U) {
        (void)os_sem_post(&s_lvgl_init_sem);
    }
    if (callback != NULL) {
        callback(result, callback_context);
    }
    return result;
}

/**
 * @brief 等待失败时复核终态，或把同步等待标为已分离。
 * @param[in] wait_error 映射后的等待错误码。
 * @param[in] wait_ret SDK 等待结果，用于失败诊断。
 * @return 终态已经发布时返回最终结果；否则返回传入的等待错误。
 * @note 只改变等待者状态，不终止 LVGL 初始化任务。
 */
static d_err_t d_lvgl_detach_startup_wait(d_err_t wait_error, int wait_ret)
{
    d_err_t result;
    uint8_t is_terminal;

    OS_ENTER_CRITICAL();
    is_terminal = s_is_result_published;
    if (is_terminal != 0U) {
        result = s_lvgl_init_result;
    } else {
        s_is_wait_detached = 1U;
        s_lvgl_init_state = D_LVGL_INIT_LATE_PENDING;
        result = wait_error;
    }
    OS_EXIT_CRITICAL();

    if (is_terminal != 0U) {
        return result;
    }
    if (wait_error == D_ERR_TIMEOUT) {
        printf(LOG_TAG "UI startup wait timed out; initialization continues\n");
    } else {
        printf(LOG_TAG "Wait for UI initialization failed: %d\n", wait_ret);
    }
    return result;
}

/**
 * @brief 在唯一 UI 任务中初始化 LVGL、显示、输入、字体及根页面，然后持续更新。
 * @param[in] context SDK 任务参数，当前未使用。
 * @note 任何初始化失败都会发布终态并结束任务；成功后循环调用产品更新及 LVGL handler，不向外转移 UI 执行权。
 */
static void d_lvgl_task(void *context)
{
    d_err_t result;
    u32 last_tick;

    (void)context;
    printf(LOG_TAG "Waiting for LCD initialization...\n");
    os_time_dly(100);

    printf(LOG_TAG "Initializing LVGL...\n");
    lv_init();
    lv_port_disp_init();
    if (lv_display_get_default() == NULL) {
        printf(LOG_TAG "Display port initialization failed\n");
        (void)d_lvgl_publish_terminal(D_ERR_IO);
        return;
    }
    lv_port_indev_init();

    extern u32 timer_get_ms(void);
    lv_tick_set_cb(&timer_get_ms);

    result = d_ui_font_init(s_lvgl_init_spec.font_fallback_tail);
    if (result != D_OK) {
        printf(LOG_TAG "Default font initialization failed: %d\n", result);
        (void)d_lvgl_publish_terminal(result);
        return;
    }
    result = s_lvgl_init_spec.bootstrap();
    if (result != D_OK) {
        printf(LOG_TAG "UI bootstrap failed: %d\n", result);
        (void)d_lvgl_publish_terminal(result);
        return;
    }

    printf(LOG_TAG "LVGL initialized successfully\n");
    (void)d_lvgl_publish_terminal(D_OK);

    last_tick = timer_get_ms();
    while (1) {
        u32 now = timer_get_ms();
        u32 elapsed_ms = now - last_tick;
        u32 time_till_next;
        u32 render_started_ms = 0U;

        last_tick = now;
        if (s_lvgl_init_spec.update != NULL) {
            s_lvgl_init_spec.update(elapsed_ms);
        }
        if (s_lvgl_init_spec.cycle_complete_cb != NULL) {
            render_started_ms = timer_get_ms();
        }
#if D_DEBUG_UI_PERFORMANCE
        d_lvgl_profile_begin();
#endif
        time_till_next = lv_task_handler();
#if D_DEBUG_UI_PERFORMANCE
        d_lvgl_profile_end();
#endif
        if (s_lvgl_init_spec.cycle_complete_cb != NULL) {
            s_lvgl_init_spec.cycle_complete_cb(render_started_ms - now,
                                               timer_get_ms() - render_started_ms);
        }
        if (time_till_next >= 1000 / OS_TICKS_PER_SEC) {
            msleep(time_till_next);
        } else {
            msleep(5);
        }
    }
}

/**
 * @brief 创建一次性 UI 任务并等待其终态；完整合同见 d_lvgl.h。
 */
d_err_t d_lvgl_init(const d_lvgl_init_spec_t *spec)
{
    int task_result;
    int wait_result;
    int worker_priority;
    d_err_t terminal_result;

    if ((spec == NULL) || (spec->bootstrap == NULL) ||
        (spec->worker_priority > D_LVGL_WORKER_PRIORITY_MAX)) {
        return D_ERR_INVALID_ARG;
    }
    worker_priority = spec->worker_priority ? spec->worker_priority : 1;

    OS_ENTER_CRITICAL();
    if (s_is_result_published != 0U) {
        terminal_result = s_lvgl_init_result;
        OS_EXIT_CRITICAL();
        return terminal_result;
    }
    if (s_is_init_started != 0U) {
        OS_EXIT_CRITICAL();
        return D_ERR_BUSY;
    }
    s_lvgl_init_spec = *spec;
    s_is_init_started = 1U;
    s_lvgl_init_state = D_LVGL_INIT_STARTING;
    OS_EXIT_CRITICAL();

    if (os_sem_create(&s_lvgl_init_sem, 0) != OS_NO_ERR) {
        printf(LOG_TAG "Create init semaphore failed\n");
        return d_lvgl_publish_terminal(D_ERR_IO);
    }
    OS_ENTER_CRITICAL();
    s_is_sem_created = 1U;
    OS_EXIT_CRITICAL();

    printf(LOG_TAG "Creating LVGL task priority=%d...\n", worker_priority);
    task_result = thread_fork("d_lvgl", worker_priority, 16 * 1024, 0, 0,
                              d_lvgl_task, NULL);
    if (task_result != OS_NO_ERR) {
        printf(LOG_TAG "Create LVGL task failed: %d\n", task_result);
        return d_lvgl_publish_terminal(D_ERR_IO);
    }

    wait_result = os_sem_pend(&s_lvgl_init_sem,
                              D_LVGL_INIT_TIMEOUT_TICKS);
    if (wait_result == OS_TIMEOUT) {
        return d_lvgl_detach_startup_wait(D_ERR_TIMEOUT, wait_result);
    }
    if (wait_result != OS_NO_ERR) {
        return d_lvgl_detach_startup_wait(D_ERR_IO, wait_result);
    }

    OS_ENTER_CRITICAL();
    terminal_result = s_lvgl_init_result;
    OS_EXIT_CRITICAL();
    return terminal_result;
}

/**
 * @brief 复制初始化状态；完整合同见 d_lvgl.h。
 */
d_err_t d_lvgl_get_init_snapshot(d_lvgl_init_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return D_ERR_INVALID_ARG;
    }

    OS_ENTER_CRITICAL();
    snapshot->state = s_lvgl_init_state;
    snapshot->result = s_lvgl_init_result;
    snapshot->is_result_published = s_is_result_published;
    OS_EXIT_CRITICAL();
    return D_OK;
}

#endif /* D_LVGL_ENABLE */
