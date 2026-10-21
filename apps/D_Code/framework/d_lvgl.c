/**
 * @file d_lvgl.c
 * @brief LVGL worker lifecycle and exact-once terminal publication.
 */

#include "config/app_config.h"
#include "system/includes.h"
#include "os/os_api.h"

#ifdef D_LVGL_ENABLE

#include "d_lvgl.h"
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
/*
 * The initializer runs at most once, so retain the static semaphore for the
 * firmware lifetime.  This also prevents a timeout-boundary publisher from
 * posting to an OS object deleted after it left the critical section.
 */
static uint8_t s_is_sem_created;
static uint8_t s_is_callback_claimed;

static d_lvgl_init_state_e d_lvgl_terminal_state(d_err_t result)
{
    return (result == D_OK) ? D_LVGL_INIT_READY : D_LVGL_INIT_FAILED;
}

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
        time_till_next = lv_task_handler();
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

d_err_t d_lvgl_init(const d_lvgl_init_spec_t *spec)
{
    int task_result;
    int wait_result;
    d_err_t terminal_result;

    if ((spec == NULL) || (spec->bootstrap == NULL)) {
        return D_ERR_INVALID_ARG;
    }

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

    printf(LOG_TAG "Creating LVGL task...\n");
    task_result = thread_fork("d_lvgl", 1, 16 * 1024, 0, 0,
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
