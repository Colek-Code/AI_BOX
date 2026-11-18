/** @file product_ui_host_alarm.c @brief 以主机双文件、独立时钟及可故障音频端口支持真实闹钟模型和服务。 */
#include "product_ui_host_alarm.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_port.h"
#include "modules/applications/product_ui/features/time/product_ui_clock_port.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_model.h"
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <windows.h>
#ifdef PRODUCT_UI_ALARM_HOST_SDK_VM
#include "system/includes.h"
#include "syscfg/syscfg_id.h"
#endif

static pthread_mutex_t s_vm_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_vm_condition = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t s_tick_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_network_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_ullong s_monotonic;
static atomic_int s_realtime;
static pthread_t s_realtime_thread;
static char s_vm_prefix[740];
static int s_fault, s_blocked, s_entered;
static int s_read_result[2];
static unsigned s_writes;
static int64_t s_network_utc = -1;
static uint64_t s_network_at;
/**
 * @brief 保存产品时钟主机定时回调。
 * @note 这是主机夹具保存的回调槽位，不是另一个函数实现。
 */
static void (*s_clock_tick)(void *);
/**
 * @brief 保存闹钟服务主机定时回调。
 * @note 这是主机夹具保存的回调槽位，不是另一个函数实现。
 */
static void (*s_alarm_tick)(void *);
static int s_timer_fault;
/**
 * @brief 设置定时器失败：1 闹钟服务定时器，2 产品时钟定时器。
 */
void product_ui_host_alarm_timer_fault(int fault) { s_timer_fault = fault; }

/**
 * @brief 在 VM 互斥内复制双槽文件前缀，写入已进入时拒绝改路径。
 */
int product_ui_host_alarm_path(const char *prefix)
{
    if (prefix == NULL || strlen(prefix) >= sizeof(s_vm_prefix)) return D_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_vm_mutex);
    if (s_entered) { pthread_mutex_unlock(&s_vm_mutex); return D_ERR_BUSY; }
    strcpy(s_vm_prefix, prefix);
    pthread_mutex_unlock(&s_vm_mutex);
    return D_OK;
}

/**
 * @brief 以 UTF-8 前缀及 -a.bin/-b.bin 打开指定主机记录文件。
 * @param[in] slot 双槽记录下标，仅允许 0 或 1。
 * @param[in] write 是否按创建覆盖模式打开文件。
 * @return 成功的 Win32 句柄由调用方 CloseHandle；未配置或转换、打开失败为 INVALID_HANDLE_VALUE。
 */
static HANDLE product_ui_host_alarm_open(uint8_t slot, int write)
{
    char utf8[768];
    wchar_t path[768];
    if (!s_vm_prefix[0]) return INVALID_HANDLE_VALUE;
    strcpy(utf8, s_vm_prefix); strcat(utf8, slot ? "-b.bin" : "-a.bin");
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, path, 768)) return INVALID_HANDLE_VALUE;
    return CreateFileW(path, write ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ, NULL,
        write ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

/**
 * @brief 持 VM 互斥读取指定槽，模拟明确错误、内容损坏与短读，拒绝比容量更长的记录。
 * @param[in] slot 双槽记录下标，仅允许 0 或 1。
 * @param[out] record 至少 size 字节的记录输出；仅正返回值范围有效。
 * @param[in] size 须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 实际正字节数；不存在为 D_ERR_NOT_FOUND；未就绪为 D_ERR_NOT_READY；超长为版本错误；读取失败为 D_ERR_IO。
 */
static int product_ui_host_vm_read(uint8_t slot, uint8_t *record, uint16_t size)
{
    HANDLE file;
    DWORD count = 0U;
    int result;
    if (slot > 1U || record == NULL || size != PRODUCT_UI_ALARM_RECORD_SIZE) return D_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_vm_mutex);
    if (s_read_result[slot]) {
        result = s_read_result[slot]; pthread_mutex_unlock(&s_vm_mutex); return result;
    }
    if (!s_vm_prefix[0] || s_fault == 5) { pthread_mutex_unlock(&s_vm_mutex); return D_ERR_NOT_READY; }
    file = product_ui_host_alarm_open(slot, 0);
    if (file == INVALID_HANDLE_VALUE) result = GetLastError() == ERROR_FILE_NOT_FOUND ? D_ERR_NOT_FOUND : D_ERR_NOT_READY;
    else {
        DWORD length = GetFileSize(file, NULL);
        result = length > size ? PRODUCT_UI_ALARM_ERR_VERSION :
            ReadFile(file, record, size, &count, NULL) ? (int)count : D_ERR_IO;
        CloseHandle(file);
        if (result == 0) result = D_ERR_NOT_FOUND;
        if (result > 0 && s_fault == 3) record[80] ^= 1U;
        if (result > 0 && s_fault == 4) result--;
    }
    pthread_mutex_unlock(&s_vm_mutex);
    return result;
}

/**
 * @brief 记录写入进入状态，等待阻断解除后写入并 FlushFileBuffers，最后关闭句柄。
 * @param[in] slot 双槽记录下标，仅允许 0 或 1。
 * @param[in] record 至少 size 字节的不可变记录。
 * @param[in] size 须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 实际写入字节数；打开、写入或刷盘失败为 D_ERR_IO；短写注入返回 size-1。
 */
static int product_ui_host_vm_write(uint8_t slot, const uint8_t *record, uint16_t size)
{
    HANDLE file;
    DWORD count = 0U;
    int result = D_ERR_IO;
    if (slot > 1U || record == NULL || size != PRODUCT_UI_ALARM_RECORD_SIZE) return D_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_vm_mutex);
    s_entered = 1; s_writes++;
    while (s_blocked) pthread_cond_wait(&s_vm_condition, &s_vm_mutex);
    file = s_fault == 1 ? INVALID_HANDLE_VALUE : product_ui_host_alarm_open(slot, 1);
    if (file != INVALID_HANDLE_VALUE) {
        if (WriteFile(file, record, s_fault == 2 ? size - 1U : size, &count, NULL) && FlushFileBuffers(file)) result = (int)count;
        CloseHandle(file);
    }
    s_entered = 0;
    pthread_mutex_unlock(&s_vm_mutex);
    return result;
}

#ifdef PRODUCT_UI_ALARM_HOST_SDK_VM
/**
 * @brief 把主机双槽结果投影为真实 SDK VM 错误类别，保留 -510 的未写入/读取错误歧义。
 * @param[in] id SDK VM 记录编号，只支持闹钟 A/B 槽。
 * @param[out] record 调用方提供的非空记录缓冲。
 * @param[in] size 须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 正数为长度；越界为 VM_INDEX_ERR，其余按 VM_READ_DATA_ERR、VM_DATA_LEN_ERR 或 VM_NOT_INIT 返回。
 */
int syscfg_read(u16 id, void *record, u16 size)
{
    int result;
    if (id < PRODUCT_UI_ALARM_VM_A || id > PRODUCT_UI_ALARM_VM_B) return VM_INDEX_ERR;
    result = product_ui_host_vm_read((uint8_t)(id - PRODUCT_UI_ALARM_VM_A), record, size);
    if (result == D_ERR_NOT_FOUND || result == D_ERR_IO) return VM_READ_DATA_ERR;
    if (result == PRODUCT_UI_ALARM_ERR_VERSION) return VM_DATA_LEN_ERR;
    return result == D_ERR_NOT_READY ? VM_NOT_INIT : result;
}

/**
 * @brief 仅将闹钟 A/B 索引转换成主机文件槽，不写真实 VM。
 * @param[in] id SDK VM 记录编号，只支持闹钟 A/B 槽。
 * @param[in] record 调用方提供的序列化记录。
 * @param[in] size 须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 实际文件写入结果，索引不属 A/B 时为 VM_INDEX_ERR。
 */
int syscfg_write(u16 id, void *record, u16 size)
{
    if (id < PRODUCT_UI_ALARM_VM_A || id > PRODUCT_UI_ALARM_VM_B) return VM_INDEX_ERR;
    return product_ui_host_vm_write((uint8_t)(id - PRODUCT_UI_ALARM_VM_A), record, size);
}

struct _VM_STRUCT;
/**
 * @brief 生成固定 AC79 VM 布局的主机状态，存在性由实际文件及显式读取分区决定。
 * @param[out] destination 调用方提供、至少 624 字节的兼容结构存储。
 * @return 固定 0；输出仅是主机 ABI 夹具，不证明设备 VM 状态。
 */
int vm_status(struct _VM_STRUCT *destination)
{
    uint8_t *bytes = (uint8_t *)(void *)destination;
    uint8_t slot;
    memset(bytes, 0, 624U);
    pthread_mutex_lock(&s_vm_mutex);
    bytes[623] = s_vm_prefix[0] ? 1U : 0U;
    for (slot = 0U; slot < 2U; slot++) {
        HANDLE file = product_ui_host_alarm_open(slot, 0);
        uint8_t present = 0U;
        if (file != INVALID_HANDLE_VALUE) { present = GetFileSize(file, NULL) != 0U; CloseHandle(file); }
        if (s_read_result[slot]) present = s_read_result[slot] != D_ERR_NOT_FOUND && s_read_result[slot] != D_ERR_NOT_READY;
        bytes[24U + 2U * (PRODUCT_UI_ALARM_VM_A + slot)] = present ? 4U : 0U;
    }
    pthread_mutex_unlock(&s_vm_mutex);
    return 0;
}
#else
/**
 * @brief 把生产闹钟存储读取委派给双槽主机文件端口。
 * @param[in] slot 双槽记录下标，仅允许 0 或 1。
 * @param[out] record 至少 size 字节的结果缓冲。
 * @param[in] size 须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 主机实际读取字节数或其负错误码。
 */
int product_ui_alarm_storage_read(uint8_t slot, uint8_t *record, uint16_t size)
{ return product_ui_host_vm_read(slot, record, size); }
/**
 * @brief 把生产闹钟存储写入委派给可阻断双槽主机文件端口。
 * @param[in] slot 双槽记录下标，仅允许 0 或 1。
 * @param[in] record 至少 size 字节的不可变记录。
 * @param[in] size 须为 PRODUCT_UI_ALARM_RECORD_SIZE。
 * @return 实际写入字节数或 D_ERR_IO 等负错误。
 */
int product_ui_alarm_storage_write(uint8_t slot, const uint8_t *record, uint16_t size)
{ return product_ui_host_vm_write(slot, record, size); }
#endif

/**
 * @brief 在 VM 互斥内设置故障和写阻断，并广播唤醒等待者。
 */
void product_ui_host_alarm_fault(int fault, int blocked)
{
    pthread_mutex_lock(&s_vm_mutex);
    s_fault = fault; s_blocked = blocked;
    pthread_cond_broadcast(&s_vm_condition);
    pthread_mutex_unlock(&s_vm_mutex);
}

/**
 * @brief 为单槽指定读取结果；非法槽忽略，零恢复真实文件路径。
 */
void product_ui_host_alarm_read_fault(uint8_t slot, int result)
{
    if (slot > 1U) return;
    pthread_mutex_lock(&s_vm_mutex); s_read_result[slot] = result; pthread_mutex_unlock(&s_vm_mutex);
}

/**
 * @brief 在 VM 互斥内读取写入是否已经进入。
 */
int product_ui_host_alarm_entered(void)
{
    int value;
    pthread_mutex_lock(&s_vm_mutex); value = s_entered; pthread_mutex_unlock(&s_vm_mutex);
    return value;
}

/**
 * @brief 读取双槽主机写入尝试累计次数。
 */
unsigned product_ui_host_alarm_writes(void)
{
    unsigned value;
    pthread_mutex_lock(&s_vm_mutex); value = s_writes; pthread_mutex_unlock(&s_vm_mutex);
    return value;
}

/**
 * @brief 读取原子主机设备时钟，独立于 LVGL 页面是否刷新。
 * @return 当前累计毫秒的低 32 位。
 */
uint32_t product_ui_clock_port_ms(void) { return (uint32_t)atomic_load(&s_monotonic); }

/**
 * @brief 以网络互斥保存 UTC 样本及对应设备毫秒锚点。
 */
void product_ui_host_alarm_network(int64_t utc_seconds)
{
    pthread_mutex_lock(&s_network_mutex);
    s_network_utc = utc_seconds;
    s_network_at = atomic_load(&s_monotonic);
    pthread_mutex_unlock(&s_network_mutex);
}

/**
 * @brief 在网络互斥内由 UTC 样本和设备时差推算当前网络时间。
 * @param[out] utc_seconds 推算的 UTC 秒；仅返回 NONE 时可作为有效样本。
 * @return 样本有效返回 NONE；负样本为 NOT_READY；输出为空为 SOURCE_INVALID。
 */
product_ui_time_unavailable_reason_e product_ui_clock_port_network(int64_t *utc_seconds)
{
    product_ui_time_unavailable_reason_e result;
    if (utc_seconds == NULL) return PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID;
    pthread_mutex_lock(&s_network_mutex);
    result = s_network_utc < 0 ? PRODUCT_UI_TIME_UNAVAILABLE_NOT_READY : PRODUCT_UI_TIME_UNAVAILABLE_NONE;
    *utc_seconds = s_network_utc + (int64_t)((atomic_load(&s_monotonic) - s_network_at) / 1000U);
    pthread_mutex_unlock(&s_network_mutex);
    return result;
}

/**
 * @brief 在 tick 互斥内保存产品时钟回调。
 * @param[in] callback 保存给主机时钟推进调用的非空定时函数。
 * @return 正常为标识 1；时钟定时器失败分区为 0。
 */
uint16_t product_ui_clock_port_start(void (*callback)(void *))
{ if (s_timer_fault == 2) return 0U; pthread_mutex_lock(&s_tick_mutex); s_clock_tick = callback; pthread_mutex_unlock(&s_tick_mutex); return 1U; }
/**
 * @brief 持 tick 互斥清除产品时钟回调，不等待目标硬件。
 * @param[in] timer 主机兼容定时器标识。
 */
void product_ui_clock_port_stop(uint16_t timer)
{ (void)timer; pthread_mutex_lock(&s_tick_mutex); s_clock_tick = NULL; pthread_mutex_unlock(&s_tick_mutex); }
#ifdef PRODUCT_UI_ALARM_HOST_SDK_VM
/**
 * @brief 只接收 sys_timer 的 100 毫秒闹钟回调注册。
 */
uint16_t sys_timer_add_to_task(const char *name, void *context, void (*callback)(void *), uint32_t milliseconds)
{
    (void)context;
    if (strcmp(name, "sys_timer") || milliseconds != 100U || s_timer_fault == 1) return 0U;
    pthread_mutex_lock(&s_tick_mutex); s_alarm_tick = callback; pthread_mutex_unlock(&s_tick_mutex); return 2U;
}
/**
 * @brief 持 tick 互斥清除主机闹钟回调。
 */
void sys_timer_del(uint16_t timer)
{ (void)timer; pthread_mutex_lock(&s_tick_mutex); s_alarm_tick = NULL; pthread_mutex_unlock(&s_tick_mutex); }
#else
/**
 * @brief 保存闹钟服务回调供主机时钟推进。
 * @param[in] callback 保存给主机时钟推进调用的非空定时函数。
 * @return 正常为标识 2；闹钟定时器失败分区为 0。
 */
uint16_t product_ui_alarm_timer_start(void (*callback)(void *))
{ if (s_timer_fault == 1) return 0U; pthread_mutex_lock(&s_tick_mutex); s_alarm_tick = callback; pthread_mutex_unlock(&s_tick_mutex); return 2U; }
/**
 * @brief 持 tick 互斥清除闹钟服务回调。
 * @param[in] timer 主机兼容定时器标识。
 */
void product_ui_alarm_timer_stop(uint16_t timer)
{ (void)timer; pthread_mutex_lock(&s_tick_mutex); s_alarm_tick = NULL; pthread_mutex_unlock(&s_tick_mutex); }
#endif

/**
 * @brief 持 tick 互斥推进原子设备时间并同步调用时钟与闹钟回调。
 * @note 回调在本函数持有 tick 互斥期间执行，调用方不能把它等同目标 RTOS 调度。
 */
void product_ui_host_alarm_advance(uint32_t milliseconds)
{
    pthread_mutex_lock(&s_tick_mutex);
    atomic_fetch_add(&s_monotonic, milliseconds);
    if (s_clock_tick) s_clock_tick(NULL);
    if (s_alarm_tick) s_alarm_tick(NULL);
    pthread_mutex_unlock(&s_tick_mutex);
}

/**
 * @brief 仅在独立实时模式关闭时随 UI 推进闹钟设备时钟。
 */
void product_ui_host_alarm_ui_tick(uint32_t milliseconds)
{ if (!atomic_load(&s_realtime)) product_ui_host_alarm_advance(milliseconds); }

/**
 * @brief 用 Windows 单调毫秒差在独立线程推进设备时钟，关闭标志后退出。
 * @param[in] context SDK 兼容上下文，本定时器替身当前忽略。
 * @return 线程结束返回 NULL。
 */
static void *product_ui_host_alarm_realtime_worker(void *context)
{
    ULONGLONG previous = GetTickCount64();
    (void)context;
    while (atomic_load(&s_realtime)) {
        ULONGLONG now;
        Sleep(10); now = GetTickCount64();
        product_ui_host_alarm_advance((uint32_t)(now - previous));
        previous = now;
    }
    return NULL;
}

/**
 * @brief 启停独立 pthread 时钟；关闭时 join 等待退出。
 */
int product_ui_host_alarm_realtime(int enabled)
{
    if (enabled) {
        if (atomic_exchange(&s_realtime, 1)) return D_ERR_BUSY;
        if (pthread_create(&s_realtime_thread, NULL, product_ui_host_alarm_realtime_worker, NULL)) {
            atomic_store(&s_realtime, 0); return D_ERR_IO;
        }
    } else if (atomic_exchange(&s_realtime, 0)) pthread_join(s_realtime_thread, NULL);
    return D_OK;
}

#ifndef PRODUCT_UI_ALARM_HOST_REAL_AUDIO
/* 服务测试只替换硬件发声端口；生产音频协调器由独立组合套件验证。 */
static pthread_mutex_t s_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static product_ui_alarm_audio_snapshot_t s_audio = {.ready = 1U};
static uint32_t s_canceled_attempt;
static int s_audio_fault;
static uint64_t s_preview_until;

/**
 * @brief 在音频互斥内设置发声或取消错误分区。
 */
void product_ui_host_alarm_audio_fault(int fault)
{
    pthread_mutex_lock(&s_audio_mutex); s_audio_fault = fault; pthread_mutex_unlock(&s_audio_mutex);
}

/**
 * @brief 持音频互斥复制当前主机发声状态。
 * @param[out] snapshot 调用方提供的非空快照。
 */
void product_ui_alarm_audio_snapshot(product_ui_alarm_audio_snapshot_t *snapshot)
{
    pthread_mutex_lock(&s_audio_mutex); *snapshot = s_audio; pthread_mutex_unlock(&s_audio_mutex);
}

/**
 * @brief 校验事件、尝试及音量，拒绝已取消尝试，记录主机播放占用或注入错误。
 * @param[in] event_id 本次闹钟业务事件编号，须非零。
 * @param[in] attempt 本次发声尝试编号，须非零且晚于已取消边界。
 * @param[in] volume 1 至 100 的音量百分比。
 * @return D_OK 已模拟占用；参数无效为 D_ERR_INVALID_ARG；取消边界内为 D_ERR_BUSY；发声故障为 D_ERR_IO。
 */
d_err_t product_ui_alarm_audio_request(uint32_t event_id, uint32_t attempt, uint8_t volume)
{
    d_err_t result = D_OK;
    if (!event_id || !attempt || volume < 1U || volume > 100U) return D_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_audio_mutex);
    if (attempt == s_canceled_attempt || (uint32_t)(attempt - s_canceled_attempt) >= 0x80000000U) result = D_ERR_BUSY;
    else {
        s_audio.event_id = event_id; s_audio.attempt = attempt;
        s_audio.error = s_audio_fault == 1 ? D_ERR_IO : D_OK;
        s_audio.playing = s_audio.owned = s_audio.error == D_OK;
        s_preview_until = 0U;
        result = s_audio.error;
    }
    pthread_mutex_unlock(&s_audio_mutex);
    return result;
}

/**
 * @brief 推进取消截至编号；匹配占用成功释放，取消故障则保留占用并记录 I/O 错误。
 * @param[in] through_attempt 取消截至的尝试编号，按无符号半区间比较新旧。
 */
void product_ui_alarm_audio_cancel(uint32_t through_attempt)
{
    pthread_mutex_lock(&s_audio_mutex);
    if (through_attempt != s_canceled_attempt && (uint32_t)(through_attempt - s_canceled_attempt) < 0x80000000U)
        s_canceled_attempt = through_attempt;
    if (s_audio.attempt == through_attempt || (uint32_t)(through_attempt - s_audio.attempt) < 0x80000000U) {
        if (s_audio.owned && s_audio_fault == 2) s_audio.error = D_ERR_IO;
        else { s_audio.playing = s_audio.pending = s_audio.owned = 0U; s_audio.error = D_OK; }
    }
    pthread_mutex_unlock(&s_audio_mutex);
}

/**
 * @brief 在没有闹钟音频占用时建立 500 毫秒主机试听窗口，不播放真实声音。
 * @param[in] volume 1 至 100 的音量百分比。
 * @return D_OK 已开始；音量无效为 D_ERR_INVALID_ARG；已有占用为 D_ERR_BUSY；注入故障为 D_ERR_IO。
 */
d_err_t product_ui_alarm_audio_preview(uint8_t volume)
{
    d_err_t result;
    pthread_mutex_lock(&s_audio_mutex);
    result = volume < 1U || volume > 100U ? D_ERR_INVALID_ARG : s_audio.owned ? D_ERR_BUSY : D_OK;
    if (result == D_OK) {
        if (s_audio_fault) result = D_ERR_IO;
        else s_preview_until = atomic_load(&s_monotonic) + 500U;
    }
    pthread_mutex_unlock(&s_audio_mutex);
    return result;
}
/**
 * @brief 持音频互斥清除主机试听截止时间。
 */
void product_ui_alarm_audio_preview_cancel(void)
{ pthread_mutex_lock(&s_audio_mutex); s_preview_until = 0U; pthread_mutex_unlock(&s_audio_mutex); }
/**
 * @brief 读取主机设备时间是否仍小于试听截止时间。
 * @param[out] active 非零表示试听窗口未结束。
 * @return D_OK 已输出；输出为空为 D_ERR_INVALID_ARG。
 */
d_err_t product_ui_alarm_audio_preview_status(uint8_t *active)
{
    if (active == NULL) return D_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_audio_mutex); *active = s_preview_until > atomic_load(&s_monotonic); pthread_mutex_unlock(&s_audio_mutex);
    return D_OK;
}
#endif
