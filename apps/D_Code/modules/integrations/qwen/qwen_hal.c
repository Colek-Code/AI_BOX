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

#include <stdarg.h>

#define QWEN_VM_STORAGE_ID   16
#define QWEN_HEX_BUF_BYTES   64
#define QWEN_MS_PER_SECOND   1000U
#define QWEN_RANDOM_LCG_A    1664525U
#define QWEN_RANDOM_LCG_C    1013904223U

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

static int *qwen_hal_current_thread_pid(void)
{
    return get_cur_thread_pid();
}

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

void *util_malloc(int32_t size)
{
    if (size <= 0) {
        return NULL;
    }

    return malloc((size_t)size);
}

void util_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    free(ptr);
}

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

int64_t util_now_ms(void)
{
    return (int64_t)timer_get_ms();
}

void util_msleep(uint32_t ms)
{
    uint32_t ticks;

    if (ms == 0U) {
        return;
    }

    ticks = qwen_hal_ms_to_ticks(ms);
    os_time_dly((int)ticks);
}

int64_t util_get_timestamp(void)
{
    return (int64_t)d_get_timestamp_ms();
}

int64_t util_get_timestamp_ms(void)
{
    return util_get_timestamp();
}

uint8_t util_timestamp_inited(void)
{
    return (uint8_t)d_crypto_timestamp_inited();
}

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

int32_t util_storage_storage(uint8_t *data, uint32_t size)
{
    if ((data == NULL) || (size == 0U) || (size > 0xFFFFU)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    return qwen_hal_storage_write_slot(QWEN_VM_STORAGE_ID, data, size);
}

int32_t util_storage_load(uint8_t *data, uint32_t size)
{
    if ((data == NULL) || (size == 0U) || (size > 0xFFFFU)) {
        return UTIL_ERR_INVALID_PARAM;
    }

    return qwen_hal_storage_read_slot(QWEN_VM_STORAGE_ID, data, size);
}

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

int util_printf(const char *format, ...)
{
    int ret;
    va_list args;

    va_start(args, format);
    ret = vprintf(format, args);
    va_end(args);
    return ret;
}

void util_set_log_level(uint8_t log_lv)
{
    g_util_log_lv = log_lv;
}

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
