/** @file os_api.h @brief 主机 pthread 对应的 SDK 兼容声明，只服务测试编译，不是原厂实现。 */
#ifndef PRODUCT_UI_HOST_OS_API_H
#define PRODUCT_UI_HOST_OS_API_H
#include <pthread.h>
typedef pthread_mutex_t OS_MUTEX;
typedef struct {
    pthread_mutex_t mutex; /**< 保护计数和条件等待的真实 pthread 互斥。 */
    pthread_cond_t condition; /**< 计数从零变为可用时唤醒等待者。 */
    unsigned count; /**< 持 mutex 访问的可消费信号量计数。 */
} OS_SEM;
#define OS_NO_ERR 0
/**
 * @brief 初始化 pthread 互斥对象。
 * @param[out] mutex 由调用方提供、尚未初始化的主机互斥对象。
 * @return pthread_mutex_init 的结果，0 成功，非零为 POSIX 错误。
 */
int os_mutex_create(OS_MUTEX *mutex);
/**
 * @brief 以真实 pthread 等待取得互斥；零 tick 无限等待，其余按绝对截止时间。
 * @param[in,out] mutex 调用方持有的已初始化互斥对象。
 * @param[in] timeout SDK 等待 tick 数，零为无限等待。
 * @return 0 已取得锁，非零为 pthread 返回的错误或 ETIMEDOUT。
 */
int os_mutex_pend(OS_MUTEX *mutex, int timeout);
/**
 * @brief 释放调用线程已持有的 pthread 互斥。
 * @param[in,out] mutex 已由当前线程取得的互斥对象。
 * @return pthread_mutex_unlock 的原结果。
 */
int os_mutex_post(OS_MUTEX *mutex);
/**
 * @brief 立即尝试取得 pthread 互斥，不阻塞等待。
 * @param[in,out] mutex 已初始化的主机互斥对象。
 * @return 0 已取得，忙或错误时返回 pthread_mutex_trylock 的原结果。
 */
int os_mutex_accept(OS_MUTEX *mutex);
/**
 * @brief 创建信号量互斥和条件变量，条件变量失败时回滚已创建互斥。
 * @param[out] sem 尚未初始化的主机信号量。
 * @param[in] count 初始信号量计数。
 * @return 0 成功；注入创建故障或任一 pthread 初始化失败返回 -1。
 */
int os_sem_create(OS_SEM *sem, int count);
/**
 * @brief 在信号量互斥内增加计数并唤醒一个真实等待者。
 * @param[in,out] sem 已初始化的主机信号量。
 * @return 0 已投递，注入投递故障返回 -1。
 */
int os_sem_post(OS_SEM *sem);
/**
 * @brief 循环等待计数并原子消费；零 tick 无限等待，虚假唤醒仍复核计数。
 * @param[in,out] sem 已初始化的主机信号量。
 * @param[in] ticks SDK tick 数，一个 tick 按 10 毫秒处理；等待接口中零表示无限等待。
 * @return 0 已消费；无计数且等待失败时返回 pthread 条件等待的结果。
 */
int os_sem_pend(OS_SEM *sem, int ticks);
/**
 * @brief 在信号量互斥内立即尝试消费一个计数。
 * @param[in,out] sem 已初始化的主机信号量。
 * @return 0 已消费，计数为零时返回 -1。
 */
int os_sem_accept(OS_SEM *sem);
/**
 * @brief 以每 tick 10 毫秒休眠当前 Windows 主机线程。
 * @param[in] ticks SDK tick 数，一个 tick 按 10 毫秒处理；等待接口中零表示无限等待。
 */
void os_time_dly(int ticks);
/**
 * @brief 从四个固定槽中分配一个真实 pthread，复制名称并保存借用入口和上下文。
 * @param[in] name 线程诊断名称，被复制到固定工作槽。
 * @param[in] priority 目标 SDK 优先级，主机端口当前忽略。
 * @param[in] words 目标 SDK 栈字数，主机线程使用 pthread 默认栈且忽略此值。
 * @param[in] queue 目标消息队列配置，当前主机端口忽略。
 * @param[out] pid 可选线程槽编号输出；成功时为槽下标加一。
 * @param[in] entry 非空线程入口，创建成功后由实际 pthread 调用。
 * @param[in] context 线程入口借用上下文，调用方保持其有效直到线程结束。
 * @return 0 已创建；入口为空、注入失败或无空槽为 -1；pthread 创建错误原样返回。
 * @note 成功槽在 join 后才可复用；priority、words、queue 不影响主机线程配置。
 */
int thread_fork(const char *name, int priority, int words, unsigned queue,
                int *pid, void (*entry)(void *), void *context);
/**
 * @brief 逐个等待已占用主机工作线程结束，并释放固定槽。
 * @return 0 全部已回收；任一 pthread_join 失败时返回 -1。
 */
int product_ui_host_join_workers(void);
/**
 * @brief 原子设置主机同步故障位。
 * @param[in] failures 故障位：1 信号量创建、2 线程创建、4 信号量投递。
 */
void product_ui_host_os_fail(int failures);
/**
 * @brief SDK 消息投递兼容声明；本头不提供目标消息队列实现。
 * @param[in] name SDK 目标任务名，保持原接口字符串。
 * @param[in] count 后续可变消息参数的数量。
 * @return 由对应主机兼容实现提供，不据此推断目标 RTOS 完成状态。
 */
int os_taskq_post(const char *name, int count, ...);
/**
 * @brief SDK 消息等待兼容声明；实际队列及调度由相应主机端口提供。
 * @param[out] messages 调用方提供的消息结果缓冲。
 * @param[in] count 消息缓冲元素容量。
 * @param[in] ticks 传给对应兼容实现的等待 tick 参数。
 * @return 由对应主机兼容实现提供。
 */
int __os_taskq_pend(int *messages, int count, int ticks);
/**
 * @brief 借出当前 pthread 的诊断任务名称。
 * @return 线程局部名称；工作线程借用其固定槽名字，主线程为 host-main。
 */
const char *product_ui_host_task_name(void);
/**
 * @brief 等待固定工作槽中的真实线程结束，清空槽位和调用方编号，不强制终止执行。
 * @param[in,out] pid 工作槽编号地址；为空或非正值时忽略，越界编号触发断言，成功回收后清零。
 * @param[in] mode SDK 模式兼容参数，当前主机实现忽略。
 */
void thread_kill(int *pid, int mode);
#endif
