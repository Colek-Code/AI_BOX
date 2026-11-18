/** @file product_ui_host_os.c @brief 以 pthread 实现主机工作线程同步和有界等待；LVGL 仍限 UI 线程，不模拟目标 RTOS 调度。 */
#include "lvgl.h"
#include <assert.h>
#include "os/os_api.h"
#include <errno.h>
#include <stdatomic.h>
#include <time.h>
#include <windows.h>
#include <string.h>

static pthread_mutex_t s_critical = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_workers_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int s_failures;
typedef struct {
    pthread_t thread; /**< 真实 pthread 句柄，回收前保持槽占用。 */
    void (*entry)(void *); /**< 借用工作入口，线程运行期间有效。 */
    void *context; /**< 借用入口上下文，由调用方保持生存期。 */
    int is_used; /**< 槽是否已分配，受工作槽互斥保护。 */
    char name[32]; /**< 复制后的线程局部诊断名称来源。 */
} product_ui_host_worker_t;
static product_ui_host_worker_t s_workers[4];
static _Thread_local const char *s_task_name = "host-main";
/**
 * @brief 借出当前 pthread 的诊断任务名称。
 */
const char *product_ui_host_task_name(void) { return s_task_name; }

/**
 * @brief 原子设置主机同步故障位。
 */
void product_ui_host_os_fail(int failures) { atomic_store(&s_failures, failures); }
/**
 * @brief 锁住主机全局临界区互斥，允许真实工作线程之间同步。
 */
void product_ui_host_enter_critical(void) { pthread_mutex_lock(&s_critical); }
/**
 * @brief 释放调用线程持有的主机全局临界区互斥。
 */
void product_ui_host_exit_critical(void) { pthread_mutex_unlock(&s_critical); }
/**
 * @brief 以每 tick 10 毫秒休眠当前 Windows 主机线程。
 */
void os_time_dly(int ticks) { Sleep((DWORD)ticks * 10U); }

/**
 * @brief 把 SDK tick 超时加到 CLOCK_REALTIME，先分离整秒以避免 32 位 long 溢出。
 * @param[in] ticks SDK tick 数，一个 tick 按 10 毫秒处理；等待接口中零表示无限等待。
 * @return 纳秒规范化到 0 至 999999999 的绝对截止时间。
 */
static struct timespec product_ui_host_deadline(int ticks)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    /* Windows long 为 32 位；先加整秒，再处理有界纳秒，避免中间乘法溢出。 */
    deadline.tv_sec += ticks / 100;
    deadline.tv_nsec += (long)(ticks % 100) * 10000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    return deadline;
}

/**
 * @brief 初始化 pthread 互斥对象。
 */
int os_mutex_create(OS_MUTEX *mutex) { return pthread_mutex_init(mutex, NULL); }
/**
 * @brief 以真实 pthread 等待取得互斥；零 tick 无限等待，其余按绝对截止时间。
 */
int os_mutex_pend(OS_MUTEX *mutex, int ticks)
{
    struct timespec deadline;
    if (ticks == 0) return pthread_mutex_lock(mutex);
    deadline = product_ui_host_deadline(ticks);
    return pthread_mutex_timedlock(mutex, &deadline);
}
/**
 * @brief 释放调用线程已持有的 pthread 互斥。
 */
int os_mutex_post(OS_MUTEX *mutex) { return pthread_mutex_unlock(mutex); }
/**
 * @brief 立即尝试取得 pthread 互斥，不阻塞等待。
 */
int os_mutex_accept(OS_MUTEX *mutex) { return pthread_mutex_trylock(mutex); }

/**
 * @brief 创建信号量互斥和条件变量，条件变量失败时回滚已创建互斥。
 */
int os_sem_create(OS_SEM *sem, int count)
{
    if (atomic_load(&s_failures) & 1) return -1;
    if (pthread_mutex_init(&sem->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&sem->condition, NULL) != 0) {
        pthread_mutex_destroy(&sem->mutex);
        return -1;
    }
    sem->count = (unsigned)count;
    return 0;
}

/**
 * @brief 在信号量互斥内增加计数并唤醒一个真实等待者。
 */
int os_sem_post(OS_SEM *sem)
{
    if (atomic_load(&s_failures) & 4) return -1;
    pthread_mutex_lock(&sem->mutex);
    sem->count++;
    pthread_cond_signal(&sem->condition);
    pthread_mutex_unlock(&sem->mutex);
    return 0;
}

/**
 * @brief 在信号量互斥内立即尝试消费一个计数。
 */
int os_sem_accept(OS_SEM *sem)
{
    int result = -1;
    pthread_mutex_lock(&sem->mutex);
    if (sem->count != 0U) {
        sem->count--;
        result = 0;
    }
    pthread_mutex_unlock(&sem->mutex);
    return result;
}

/**
 * @brief 循环等待计数并原子消费；零 tick 无限等待，虚假唤醒仍复核计数。
 */
int os_sem_pend(OS_SEM *sem, int ticks)
{
    struct timespec deadline = product_ui_host_deadline(ticks);
    int result = 0;
    pthread_mutex_lock(&sem->mutex);
    while (sem->count == 0U && result == 0) {
        result = ticks == 0 ? pthread_cond_wait(&sem->condition, &sem->mutex) :
            pthread_cond_timedwait(&sem->condition, &sem->mutex, &deadline);
    }
    if (sem->count != 0U) {
        sem->count--;
        result = 0;
    }
    pthread_mutex_unlock(&sem->mutex);
    return result;
}

/**
 * @brief 设置线程局部任务名后调用固定工作槽的入口。
 * @param[in] context 线程入口借用上下文，调用方保持其有效直到线程结束。
 * @return 入口执行结束后返回 NULL。
 */
static void *product_ui_host_worker_entry(void *context)
{
    product_ui_host_worker_t *worker = context;
    s_task_name = worker->name;
    worker->entry(worker->context);
    return NULL;
}

/**
 * @brief 从四个固定槽中分配一个真实 pthread，复制名称并保存借用入口和上下文。
 * @note 成功槽在 join 后才可复用；priority、words、queue 不影响主机线程配置。
 */
int thread_fork(const char *name, int priority, int words, unsigned queue,
                int *pid, void (*entry)(void *), void *context)
{
    unsigned index;
    int result = -1;
    (void)priority; (void)words; (void)queue;
    if (entry == NULL || (atomic_load(&s_failures) & 2)) return -1;
    pthread_mutex_lock(&s_workers_mutex);
    for (index = 0U; index < sizeof(s_workers) / sizeof(s_workers[0]); index++) {
        product_ui_host_worker_t *worker = &s_workers[index];
        if (worker->is_used) continue;
        worker->entry = entry;
        worker->context = context;
        strncpy(worker->name, name ? name : "host-worker", sizeof(worker->name) - 1U);
        worker->name[sizeof(worker->name) - 1U] = '\0';
        worker->is_used = 1;
        result = pthread_create(&worker->thread, NULL, product_ui_host_worker_entry, worker);
        if (result != 0) worker->is_used = 0;
        else if (pid != NULL) *pid = (int)index + 1;
        break;
    }
    pthread_mutex_unlock(&s_workers_mutex);
    return result;
}

/**
 * @brief 等待固定工作槽中的真实线程结束，清空槽位和调用方编号，不强制终止执行。
 */
void thread_kill(int *pid, int mode)
{
    int index;
    pthread_t thread;
    (void)mode;
    if (pid == NULL || *pid <= 0) return;
    index = *pid - 1;
    assert(index >= 0 && index < (int)(sizeof(s_workers) / sizeof(s_workers[0])));
    pthread_mutex_lock(&s_workers_mutex);
    thread = s_workers[index].thread;
    pthread_mutex_unlock(&s_workers_mutex);
    assert(pthread_join(thread, NULL) == 0);
    pthread_mutex_lock(&s_workers_mutex);
    s_workers[index].is_used = 0;
    pthread_mutex_unlock(&s_workers_mutex);
    *pid = 0;
}

/**
 * @brief 逐个等待已占用主机工作线程结束，并释放固定槽。
 */
int product_ui_host_join_workers(void)
{
    unsigned index;
    for (index = 0U; index < sizeof(s_workers) / sizeof(s_workers[0]); index++) {
        product_ui_host_worker_t *worker = &s_workers[index];
        int is_used;
        pthread_t thread;
        pthread_mutex_lock(&s_workers_mutex);
        is_used = worker->is_used;
        thread = worker->thread;
        pthread_mutex_unlock(&s_workers_mutex);
        if (!is_used) continue;
        if (pthread_join(thread, NULL) != 0) return -1;
        pthread_mutex_lock(&s_workers_mutex);
        worker->is_used = 0;
        pthread_mutex_unlock(&s_workers_mutex);
    }
    return 0;
}

/**
 * @brief 初始化 UI 线程专用的 LVGL 锁标记。
 * @param[in,out] mutex 非空 UI 线程锁标记；跨线程工作使用 OS_MUTEX。
 * @return LV_RESULT_OK；合同违背时触发断言。
 */
lv_result_t lv_mutex_init(lv_mutex_t *mutex)
{
    assert(mutex != NULL);
    *mutex = 0U;
    return LV_RESULT_OK;
}

/**
 * @brief 断言标记未占用后置为占用；不用于跨线程同步。
 * @param[in,out] mutex 非空 UI 线程锁标记；跨线程工作使用 OS_MUTEX。
 * @return LV_RESULT_OK；合同违背时触发断言。
 */
lv_result_t lv_mutex_lock(lv_mutex_t *mutex)
{
    assert(mutex != NULL && *mutex == 0U);
    *mutex = 1U;
    return LV_RESULT_OK;
}

/**
 * @brief 断言标记已占用后清零。
 * @param[in,out] mutex 非空 UI 线程锁标记；跨线程工作使用 OS_MUTEX。
 * @return LV_RESULT_OK；合同违背时触发断言。
 */
lv_result_t lv_mutex_unlock(lv_mutex_t *mutex)
{
    assert(mutex != NULL && *mutex == 1U);
    *mutex = 0U;
    return LV_RESULT_OK;
}

/**
 * @brief 断言 UI 锁标记已释放，不分配或释放堆对象。
 * @param[in,out] mutex 非空 UI 线程锁标记；跨线程工作使用 OS_MUTEX。
 * @return LV_RESULT_OK；合同违背时触发断言。
 */
lv_result_t lv_mutex_delete(lv_mutex_t *mutex)
{
    assert(mutex != NULL && *mutex == 0U);
    return LV_RESULT_OK;
}
