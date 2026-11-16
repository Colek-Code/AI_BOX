/** @file qwen_hal.c @brief 固定 Qwen SDK 的内存、互斥、时间、随机及 VM16 平台适配。 */
#define BOOL_DEFINE_CONFLICT

#include "config/app_config.h"

#if D_FEATURE_QWEN

#ifndef CONFIG_PLATFORM_AC7911
#define CONFIG_PLATFORM_AC7911 1
#endif

#include "sdk/include/c_utils/c_utils.h"
#include "framework/d_crypto.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "system/timer.h"
#include "syscfg_id.h"
#include "system/spinlock.h"
#include "qwen_hal_storage.h"
#include "sdk/include/lib_c_license.h"

#include <stdarg.h>

#define QWEN_VM_STORAGE_ID   16
#define QWEN_HEX_BUF_BYTES   64
#define QWEN_MS_PER_SECOND   1000U
#define QWEN_RANDOM_LCG_A    1664525U
#define QWEN_RANDOM_LCG_C    1013904223U
/* 当前固定 License 序列化结果为 176 字节，保留有界余量；
 * 将来格式无法容纳时，公开序列化器返回零。 */
#define QWEN_STORAGE_VERIFY_CAPACITY 512U

static DEFINE_SPINLOCK(s_qwen_storage_lock);
static uint8_t s_qwen_storage_busy;
static uint8_t s_qwen_storage_expected[QWEN_STORAGE_VERIFY_CAPACITY];
static uint8_t s_qwen_storage_observed[QWEN_STORAGE_VERIFY_CAPACITY];

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
#define QWEN_HAL_TRACE(...)  printf("[QWEN_HAL] " __VA_ARGS__)
#else
#define QWEN_HAL_TRACE(...)
#endif

typedef struct {
    OS_MUTEX os_mutex;
    int *owner_pid;
    uint8_t is_locked;
} qwen_os_mutex_t;

static uint32_t s_qwen_random_state;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
static uint32_t s_qwen_random_init_count;
static uint32_t s_qwen_random_call_count;
static uint32_t s_qwen_mutex_create_count;
static uint32_t s_qwen_mutex_lock_count;
static uint32_t s_qwen_mutex_unlock_count;
#endif

/**
 * @brief 把毫秒向上换算为系统节拍，正值至少一拍。
 * @param[in] ms 等待毫秒数。
 * @return 0 输入返回 0，其余返回换算节拍。
 */
static uint32_t qwen_hal_ms_to_ticks(uint32_t ms)
{
    uint64_t scaled_ticks;
    uint32_t ticks;

    if (ms == 0U) {
        return 0U;
    }

    scaled_ticks = ((uint64_t)ms * (uint64_t)OS_TICKS_PER_SEC) + (QWEN_MS_PER_SECOND - 1U);
    ticks = (uint32_t)(scaled_ticks / (uint64_t)QWEN_MS_PER_SECOND);

    if (ticks == 0U) {
        ticks = 1U;
    }

    return ticks;
}

/**
 * @brief 借用 SDK 当前线程 PID 指针作为互斥所有者身份。
 * @return 当前线程 PID 指针。
 */
static int *qwen_hal_current_thread_pid(void)
{
    return get_cur_thread_pid();
}

/**
 * @brief 在短临界区核对互斥锁是否由指定线程持有。
 * @param[in] os_mutex 平台互斥封装。
 * @param[in] current_pid 待检查线程 PID 指针。
 * @return 相同所有者且已锁定返回 1，否则为 0。
 */
static uint8_t qwen_hal_mutex_is_owner(qwen_os_mutex_t *os_mutex, int *current_pid)
{
    uint8_t is_owner;

    if ((os_mutex == NULL) || (current_pid == NULL)) {
        return 0U;
    }

    OS_ENTER_CRITICAL();
    is_owner = (uint8_t)(((os_mutex->is_locked != 0U) && (os_mutex->owner_pid == current_pid)) ? 1U : 0U);
    OS_EXIT_CRITICAL();

    return is_owner;
}

/**
 * @brief 在短临界区登记互斥所有者与锁定标记。
 * @param[in,out] os_mutex 平台互斥封装。
 * @param[in] owner_pid 新所有者 PID 指针。
 */
static void qwen_hal_mutex_set_owner(qwen_os_mutex_t *os_mutex, int *owner_pid)
{
    if (os_mutex == NULL) {
        return;
    }

    OS_ENTER_CRITICAL();
    os_mutex->owner_pid = owner_pid;
    os_mutex->is_locked = 1U;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 在短临界区清除互斥所有者与锁定标记。
 * @param[in,out] os_mutex 平台互斥封装；为空时忽略。
 */
static void qwen_hal_mutex_clear_owner(qwen_os_mutex_t *os_mutex)
{
    if (os_mutex == NULL) {
        return;
    }

    OS_ENTER_CRITICAL();
    os_mutex->owner_pid = NULL;
    os_mutex->is_locked = 0U;
    OS_EXIT_CRITICAL();
}

/**
 * @brief 按 SDK 精确字节数合同写入指定 VM 槽。
 * @param[in] storage_id VM 槽编号。
 * @param[in] storage_data 输入缓冲。
 * @param[in] size 1～65535 的字节数。
 * @return 完整写入返回 UTIL_SUCCESS；参数或短写返回对应 UTIL 错误。
 */
static int32_t qwen_hal_storage_write_slot(uint16_t storage_id, const uint8_t *storage_data, uint32_t size)
{
    int32_t write_len;

    if ((storage_data == NULL) || (size == 0U) || (size > 0xFFFFU)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    write_len = syscfg_write(storage_id, (void *)storage_data, (u16)size);
    if (write_len != (int32_t)size) {
        printf("[QWEN_HAL] storage write fail id=%u size=%u ret=%d\n",
               (unsigned)storage_id,
               (unsigned)size,
               (int)write_len);
        return UTIL_ERR_IO;
    }

    return UTIL_SUCCESS;
}

/**
 * @brief 从指定 VM 槽读取至 SDK 缓冲。
 * @param[in] storage_id VM 槽编号。
 * @param[out] storage_data 读取缓冲。
 * @param[in] size 1～65535 的容量。
 * @return 读取返回正数时为 UTIL_SUCCESS；无记录或读取失败映射 UTIL_ERR_NOT_FOUND；参数无效返回 UTIL_ERR_INVALID_PARAM。
 * @note 该兼容入口不要求返回长度恰好为 size；受检保存入口另行核对精确长度。
 */
static int32_t qwen_hal_storage_read_slot(uint16_t storage_id, uint8_t *storage_data, uint32_t size)
{
    int32_t read_len;

    if ((storage_data == NULL) || (size == 0U) || (size > 0xFFFFU)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    read_len = syscfg_read(storage_id, storage_data, (u16)size);
    if (read_len <= 0) {
        printf("[QWEN_HAL] storage read miss id=%u size=%u ret=%d\n",
               (unsigned)storage_id,
               (unsigned)size,
               (int)read_len);
        return UTIL_ERR_NOT_FOUND;
    }

    return UTIL_SUCCESS;
}

/**
 * @brief 导出、写入并回读验证 SDK 凭据；完整合同见 qwen_hal_storage.h。
 */
d_err_t qwen_hal_storage_save_checked(void)
{
    uint32_t size;
    int read_size;
    d_err_t result = D_ERR_IO;

    spin_lock(&s_qwen_storage_lock);
    if (s_qwen_storage_busy) {
        spin_unlock(&s_qwen_storage_lock);
        return D_ERR_BUSY;
    }
    s_qwen_storage_busy = 1U;
    spin_unlock(&s_qwen_storage_lock);

    if (!c_license_is_enable()) {
        result = D_ERR_NOT_READY;
        goto done;
    }
    /* 当前 SDK 的 c_mmi_storage_save 丢弃 util_storage_storage 错误；
     * 使用相同公开序列化器与 HAL 槽，独立核对真实结果。 */
    size = c_license_get_storage_data(s_qwen_storage_expected, sizeof(s_qwen_storage_expected));
    if (size == 0U || size > sizeof(s_qwen_storage_expected)) goto done;
    if (qwen_hal_storage_write_slot(QWEN_VM_STORAGE_ID, s_qwen_storage_expected, size) != UTIL_SUCCESS)
        goto done;
    read_size = syscfg_read(QWEN_VM_STORAGE_ID, s_qwen_storage_observed, (u16)size);
    if (read_size == (int)size && memcmp(s_qwen_storage_expected, s_qwen_storage_observed, size) == 0)
        result = D_OK;
done:
    memset(s_qwen_storage_expected, 0, sizeof(s_qwen_storage_expected));
    memset(s_qwen_storage_observed, 0, sizeof(s_qwen_storage_observed));
    spin_lock(&s_qwen_storage_lock);
    s_qwen_storage_busy = 0U;
    spin_unlock(&s_qwen_storage_lock);
    return result;
}

/**
 * @brief 把 RTOS 成功、超时与其他失败映射到 SDK util 错误。
 * @param[in] ret RTOS 原始结果。
 * @return UTIL_SUCCESS、UTIL_ERR_TIMEOUT 或 UTIL_ERR_FAIL。
 */
static int32_t qwen_hal_os_ret_to_util(int32_t ret)
{
    if (ret == OS_NO_ERR) {
        return UTIL_SUCCESS;
    }

    if (ret == OS_TIMEOUT) {
        return UTIL_ERR_TIMEOUT;
    }

    return UTIL_ERR_FAIL;
}

uint8_t g_util_log_lv = UTIL_LOG_LV_INFO;

/**
 * @brief 实现 SDK 动态分配回调。
 * @param[in] size 请求字节数。
 * @return 分配的指针；size 非正或分配失败返回 NULL。
 * @note SDK c_utils 声明的兼容实现，调用者最终通过 util_free 归还。
 */
void *util_malloc(int32_t size)
{
    if (size <= 0) {
        return NULL;
    }

    return malloc((size_t)size);
}

/**
 * @brief 实现 SDK 内存释放回调，空指针不处理。
 * @param[in] ptr 由兼容分配器得到的内存。
 */
void util_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    free(ptr);
}

/**
 * @brief 实现 SDK 扩缩容回调，非正大小时释放旧内存。
 * @param[in,out] ptr 原分配指针或 NULL。
 * @param[in] size 新字节数，非正表示释放。
 * @return 新指针或 NULL；正大小扩容失败时旧内存仍由调用者持有。
 */
void *util_realloc(void *ptr, int32_t size)
{
    if (size <= 0) {
        if (ptr != NULL) {
            free(ptr);
        }
        return NULL;
    }

    return realloc(ptr, (size_t)size);
}

/**
 * @brief 分配 SDK 与平台互斥封装，并初始化 RTOS 互斥量。
 * @return 新互斥描述，任一步失败清理已分配对象并返回 NULL。
 */
util_mutex_t *util_mutex_create(void)
{
    util_mutex_t *mutex;
    qwen_os_mutex_t *os_mutex;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    uint32_t create_index;
#endif

    mutex = (util_mutex_t *)malloc(sizeof(util_mutex_t));
    if (mutex == NULL) {
        return NULL;
    }

    os_mutex = (qwen_os_mutex_t *)malloc(sizeof(qwen_os_mutex_t));
    if (os_mutex == NULL) {
        free(mutex);
        return NULL;
    }

    if (os_mutex_create(&os_mutex->os_mutex) != OS_NO_ERR) {
        free(os_mutex);
        free(mutex);
        return NULL;
    }

    os_mutex->owner_pid = NULL;
    os_mutex->is_locked = 0U;
    mutex->mutex_handle = os_mutex;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    create_index = ++s_qwen_mutex_create_count;
    QWEN_HAL_TRACE("mutex_create[%u]: mutex=%p handle=%p\n", create_index, mutex, os_mutex);
#endif
    return mutex;
}

/**
 * @brief 销毁并释放 SDK 互斥封装。
 * @param[in,out] mutex 目标互斥描述，允许为空；调用后不可再使用。
 * @note 调用者必须先保证没有使用者或等待者；本接口不等待它们退出。
 */
void util_mutex_delete(util_mutex_t *mutex)
{
    qwen_os_mutex_t *os_mutex;

    if (mutex == NULL) {
        return;
    }

    os_mutex = (qwen_os_mutex_t *)mutex->mutex_handle;
    if (os_mutex != NULL) {
        qwen_hal_mutex_clear_owner(os_mutex);
        os_mutex_del(&os_mutex->os_mutex, OS_DEL_ALWAYS);
        free(os_mutex);
    }

    free(mutex);
}

/**
 * @brief 按毫秒期限取得非递归互斥量并登记当前所有者。
 * @param[in,out] mutex 有效 SDK 互斥对象。
 * @param[in] timeout 0 只尝试，MUTEX_WAIT_FOREVER 无限等待，其他正数为毫秒。
 * @return 成功返回 UTIL_SUCCESS；参数、重复持有、超时或 RTOS 失败返回相应 UTIL 错误。
 * @note 同线程重入不增加计数：有限等待先延时后报超时，零或无限等待报 BUSY。
 */
int32_t util_mutex_lock(util_mutex_t *mutex, int32_t timeout)
{
    qwen_os_mutex_t *os_mutex;
    int *current_pid;
    int32_t ret;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    uint32_t begin_ms;
    uint32_t end_ms;
    uint32_t elapsed_ms;
    uint32_t lock_index;
    uint8_t was_owner;
    int *owner_pid_snapshot;
    uint8_t is_locked_snapshot;
#endif

    if ((mutex == NULL) || (mutex->mutex_handle == NULL)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    if ((timeout < 0) && (timeout != MUTEX_WAIT_FOREVER)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    os_mutex = (qwen_os_mutex_t *)mutex->mutex_handle;
    current_pid = qwen_hal_current_thread_pid();
    if (current_pid == NULL) {
        return UTIL_ERR_FAIL;
    }

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    OS_ENTER_CRITICAL();
    owner_pid_snapshot = os_mutex->owner_pid;
    is_locked_snapshot = os_mutex->is_locked;
    OS_EXIT_CRITICAL();
    was_owner = (uint8_t)(((is_locked_snapshot != 0U) && (owner_pid_snapshot == current_pid)) ? 1U : 0U);
    lock_index = ++s_qwen_mutex_lock_count;
    begin_ms = (uint32_t)timer_get_ms();
    QWEN_HAL_TRACE("mutex_lock[%u]-enter: mutex=%p handle=%p timeout=%d pid=%p owner=%p locked=%u self_owner=%u\n",
                   lock_index,
                   mutex,
                   os_mutex,
                   timeout,
                   current_pid,
                   owner_pid_snapshot,
                   is_locked_snapshot,
                   was_owner);
#endif

    if (qwen_hal_mutex_is_owner(os_mutex, current_pid) != 0U) {
        if ((timeout > 0) && (timeout != MUTEX_WAIT_FOREVER)) {
            util_msleep((uint32_t)timeout);
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
            end_ms = (uint32_t)timer_get_ms();
            elapsed_ms = end_ms - begin_ms;
            QWEN_HAL_TRACE("mutex_lock[%u]-owner-timeout: ret=%d elapsed=%u\n",
                           lock_index,
                           UTIL_ERR_TIMEOUT,
                           elapsed_ms);
#endif
            return UTIL_ERR_TIMEOUT;
        }

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
        end_ms = (uint32_t)timer_get_ms();
        elapsed_ms = end_ms - begin_ms;
        QWEN_HAL_TRACE("mutex_lock[%u]-owner-busy: ret=%d elapsed=%u\n",
                       lock_index,
                       UTIL_ERR_BUSY,
                       elapsed_ms);
#endif
        return UTIL_ERR_BUSY;
    }

    if (timeout == MUTEX_WAIT_FOREVER) {
        ret = os_mutex_pend(&os_mutex->os_mutex, 0);
    } else if (timeout == 0) {
        ret = os_mutex_accept(&os_mutex->os_mutex);
    } else {
        ret = os_mutex_pend(&os_mutex->os_mutex, (int)qwen_hal_ms_to_ticks((uint32_t)timeout));
    }

    if (ret == OS_NO_ERR) {
        qwen_hal_mutex_set_owner(os_mutex, current_pid);
    }

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    end_ms = (uint32_t)timer_get_ms();
    elapsed_ms = end_ms - begin_ms;
    OS_ENTER_CRITICAL();
    owner_pid_snapshot = os_mutex->owner_pid;
    is_locked_snapshot = os_mutex->is_locked;
    OS_EXIT_CRITICAL();
    QWEN_HAL_TRACE("mutex_lock[%u]-exit: os_ret=%d util_ret=%d elapsed=%u owner=%p locked=%u\n",
                   lock_index,
                   ret,
                   qwen_hal_os_ret_to_util(ret),
                   elapsed_ms,
                   owner_pid_snapshot,
                   is_locked_snapshot);
#endif

    return qwen_hal_os_ret_to_util(ret);
}

/**
 * @brief 仅允许当前登记的线程释放互斥量。
 * @param[in,out] mutex 当前线程持有的 SDK 互斥对象。
 * @return 成功返回 UTIL_SUCCESS；对象无效或非所有者返回对应错误；RTOS post 失败恢复所有者标记。
 */
int32_t util_mutex_unlock(util_mutex_t *mutex)
{
    qwen_os_mutex_t *os_mutex;
    int *current_pid;
    int32_t ret;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    uint32_t unlock_index;
    int *owner_pid_snapshot;
    uint8_t is_locked_snapshot;
#endif

    if ((mutex == NULL) || (mutex->mutex_handle == NULL)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    os_mutex = (qwen_os_mutex_t *)mutex->mutex_handle;
    current_pid = qwen_hal_current_thread_pid();
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    OS_ENTER_CRITICAL();
    owner_pid_snapshot = os_mutex->owner_pid;
    is_locked_snapshot = os_mutex->is_locked;
    OS_EXIT_CRITICAL();
    unlock_index = ++s_qwen_mutex_unlock_count;
    QWEN_HAL_TRACE("mutex_unlock[%u]-enter: mutex=%p handle=%p pid=%p owner=%p locked=%u\n",
                   unlock_index,
                   mutex,
                   os_mutex,
                   current_pid,
                   owner_pid_snapshot,
                   is_locked_snapshot);
#endif
    if ((current_pid == NULL) || (qwen_hal_mutex_is_owner(os_mutex, current_pid) == 0U)) {
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
        QWEN_HAL_TRACE("mutex_unlock[%u]-reject: ret=%d\n", unlock_index, UTIL_ERR_FAIL);
#endif
        return UTIL_ERR_FAIL;
    }

    qwen_hal_mutex_clear_owner(os_mutex);
    ret = os_mutex_post(&os_mutex->os_mutex);
    if (ret != OS_NO_ERR) {
        qwen_hal_mutex_set_owner(os_mutex, current_pid);
    }

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    OS_ENTER_CRITICAL();
    owner_pid_snapshot = os_mutex->owner_pid;
    is_locked_snapshot = os_mutex->is_locked;
    OS_EXIT_CRITICAL();
    QWEN_HAL_TRACE("mutex_unlock[%u]-exit: os_ret=%d util_ret=%d owner=%p locked=%u\n",
                   unlock_index,
                   ret,
                   qwen_hal_os_ret_to_util(ret),
                   owner_pid_snapshot,
                   is_locked_snapshot);
#endif

    return qwen_hal_os_ret_to_util(ret);
}

/**
 * @brief 提供 SDK 单调计时回调。
 * @return timer_get_ms 转换的 int64_t 值，底层计数仍按其原宽度回绕。
 */
int64_t util_now_ms(void)
{
    return (int64_t)timer_get_ms();
}

/**
 * @brief 将正毫秒延时向上转换到 RTOS 节拍。
 * @param[in] ms 等待毫秒数，0 不延时。
 */
void util_msleep(uint32_t ms)
{
    uint32_t ticks;

    if (ms == 0U) {
        return;
    }

    ticks = qwen_hal_ms_to_ticks(ms);
    os_time_dly((int)ticks);
}

/**
 * @brief 提供加密模块维护的 UTC 毫秒时间戳。
 * @return d_get_timestamp_ms 的有符号返回视图。
 */
int64_t util_get_timestamp(void)
{
    return (int64_t)d_get_timestamp_ms();
}

/**
 * @brief 提供与 util_get_timestamp 相同的 UTC 毫秒兼容入口。
 * @return 当前 UTC 毫秒值。
 */
int64_t util_get_timestamp_ms(void)
{
    return util_get_timestamp();
}

/**
 * @brief 读取加密时间源的初始化标记。
 * @return 加密模块的时间已初始化标记。
 */
uint8_t util_timestamp_inited(void)
{
    return (uint8_t)d_crypto_timestamp_inited();
}

/**
 * @brief 向 SDK VM16 写一个零字节以失效原记录。
 * @return syscfg 返回正数时 UTIL_SUCCESS，否则 UTIL_ERR_IO。
 * @note 不是整个 VM 区擦除；不提供物理擦除完成证明。
 */
int32_t util_storage_erase(void)
{
    uint8_t empty = 0U;
    int32_t write_len;

    write_len = syscfg_write(QWEN_VM_STORAGE_ID, &empty, sizeof(empty));
    if (write_len <= 0) {
        return UTIL_ERR_IO;
    }

    return UTIL_SUCCESS;
}

/**
 * @brief 实现 SDK VM16 凭据写入回调。
 * @param[in] data SDK 原格式记录输入。
 * @param[in] size 非零且不超过 65535 的长度。
 * @return 完整写入返回 UTIL_SUCCESS；参数或短写返回对应错误。
 */
int32_t util_storage_storage(uint8_t *data, uint32_t size)
{
    if ((data == NULL) || (size == 0U) || (size > 0xFFFFU)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    return qwen_hal_storage_write_slot(QWEN_VM_STORAGE_ID, data, size);
}

/**
 * @brief 实现 SDK VM16 凭据读取回调。
 * @param[out] data SDK 记录输出缓冲。
 * @param[in] size 非零且不超过 65535 的容量。
 * @return 正数读取返回 UTIL_SUCCESS；缺失、失败或参数错误返回映射结果。
 */
int32_t util_storage_load(uint8_t *data, uint32_t size)
{
    if ((data == NULL) || (size == 0U) || (size > 0xFFFFU)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    return qwen_hal_storage_read_slot(QWEN_VM_STORAGE_ID, data, size);
}

/**
 * @brief 在短临界区保存 SDK 线性同余随机种子。
 * @param[in] seed 初始 32 位状态。
 * @return 固定返回 UTIL_SUCCESS。
 */
int32_t util_random_init(uint32_t seed)
{
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    uint32_t init_index;
#endif

    OS_ENTER_CRITICAL();
    s_qwen_random_state = seed;
    OS_EXIT_CRITICAL();
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    init_index = ++s_qwen_random_init_count;
    QWEN_HAL_TRACE("random_init[%u]: seed=%u(0x%x)\n", init_index, seed, seed);
#endif
    return UTIL_SUCCESS;
}

/**
 * @brief 在短临界区推进 SDK 线性同余状态。
 * @return 本次新 32 位状态值。
 * @note 此算法提供兼容伪随机序列，不宣称密码学随机性。
 */
uint32_t util_random(void)
{
    uint32_t random_value;
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    uint32_t random_index;
    uint32_t prev_state;
#endif

    OS_ENTER_CRITICAL();
#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    prev_state = s_qwen_random_state;
#endif
    s_qwen_random_state = (s_qwen_random_state * QWEN_RANDOM_LCG_A) + QWEN_RANDOM_LCG_C;
    random_value = s_qwen_random_state;
    OS_EXIT_CRITICAL();

#if D_QWEN_HAL_ACCEPT_TEST_ENABLE
    random_index = ++s_qwen_random_call_count;
    QWEN_HAL_TRACE("random[%u]: prev=%u(0x%x) next=%u(0x%x)\n",
                   random_index,
                   prev_state,
                   prev_state,
                   random_value,
                   random_value);
#endif

    return random_value;
}

/**
 * @brief 把 SDK 可变参数日志转交平台 vprintf。
 * @param[in] format 非空格式字符串；后续可变参数与之匹配。
 * @return vprintf 原始结果。
 */
int util_printf(const char *format, ...)
{
    int ret;
    va_list args;

    va_start(args, format);
    ret = vprintf(format, args);
    va_end(args);
    return ret;
}

/**
 * @brief 设置 SDK util 日志级别全局值。
 * @param[in] log_lv SDK 定义的日志级别。
 */
void util_set_log_level(uint8_t log_lv)
{
    g_util_log_lv = log_lv;
}

/**
 * @brief 把最多 64 字节转换为小写十六进制诊断文本。
 * @param[in] buf 可空原始字节。
 * @param[in] len 输入长度，超过 64 截断。
 * @return 内部静态零结尾缓冲；下次调用会覆盖。
 * @note 没有独立存储所有权，也不保证并发调用安全。
 */
const char *hex_str(const void *buf, size_t len)
{
    static char str[(QWEN_HEX_BUF_BYTES * 2) + 1];
    const uint8_t *data;
    size_t index;
    size_t out_len;
    static const char s_hex[] = "0123456789abcdef";

    if ((buf == NULL) || (len == 0U)) {
        str[0] = '\0';
        return str;
    }

    data = (const uint8_t *)buf;
    out_len = (len > QWEN_HEX_BUF_BYTES) ? QWEN_HEX_BUF_BYTES : len;
    for (index = 0; index < out_len; index++) {
        str[index * 2] = s_hex[(data[index] >> 4) & 0x0F];
        str[index * 2 + 1] = s_hex[data[index] & 0x0F];
    }

    str[out_len * 2] = '\0';
    return str;
}

#endif
