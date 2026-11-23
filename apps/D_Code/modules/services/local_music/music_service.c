/** @file music_service.c @brief 由单个工作任务持有扫描、文件、解码器及续播状态。 */
#include "config/app_config.h"
#if D_FEATURE_LOCAL_MUSIC
#include "music_service.h"
#include "music_backend.h"
#include "platform/storage/d_storage.h"
#include "modules/services/audio/d_device_volume.h"
#include "system/includes.h"
#include "system/spinlock.h"
#include <string.h>

#define MUSIC_TASK_PRIORITY 12
#define MUSIC_TASK_STACK_WORDS 1536
#define MUSIC_POLL_TICKS 5
static DEFINE_SPINLOCK(s_music_lock);
static struct {
    music_config_t config;
    music_snapshot_t state;
    music_page_t page;
    music_command_e command;
    uint16_t requested_index, requested_page;
    uint32_t command_generation, page_generation;
    uint32_t media_epoch;
    uint32_t hold_tickets[5];
    uint32_t volume_requested, volume_completed;
    uint32_t volume_started_ms, volume_timeout_ms;
    d_err_t volume_result;
    uint8_t volume, requested_volume, volume_expired, attempted, scan_pending, stop_pending;
    uint8_t catalog_clear_pending; /* 由工作任务持有，媒体 STOP 失败后继续保留。 */
    uint8_t page_read_active; /* SDK 在 fselect 期间可能复用扫描取消回调。 */
    int task_pid;
} s_music;
static void music_worker(void *context);
static int music_page_blocked_locked(void);

/**
 * @brief 服务就绪时尝试通知音乐工作队列。
 */
static void music_wake(void)
{
    if (s_music.state.ready) (void)os_taskq_post("local_music", 1, 1);
}

/**
 * @brief 推进非零状态版本。
 * @note 调用者持有 s_music_lock。
 */
static void music_revision_locked(void)
{
    if (++s_music.state.revision == 0U) ++s_music.state.revision;
}

/**
 * @brief 在初始化前绑定本地音乐策略，不创建任务或操作 SDK。；完整合同见 music_service.h。
 */
d_err_t music_service_configure(const music_config_t *config)
{
    if (config == NULL || !config->maximum_tracks || config->permit == NULL || !config->formats ||
        (config->formats & ~(MUSIC_FORMAT_MP3 | MUSIC_FORMAT_WAV))) return D_ERR_INVALID_ARG;
    spin_lock(&s_music_lock);
    if (s_music.attempted) { spin_unlock(&s_music_lock); return D_ERR_BUSY; }
    s_music.config = *config;
    spin_unlock(&s_music_lock);
    return D_OK;
}

/**
 * @brief 在锁内复制不含 SDK 指针的音乐状态。；完整合同见 music_service.h。
 */
void music_service_get_snapshot(music_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    spin_lock(&s_music_lock);
    *snapshot = s_music.state;
    spin_unlock(&s_music_lock);
}

/**
 * @brief 在锁内复制当前已发布的分页值。；完整合同见 music_service.h。
 */
void music_service_get_page(music_page_t *page)
{
    if (page == NULL) return;
    spin_lock(&s_music_lock);
    *page = s_music.page;
    spin_unlock(&s_music_lock);
}

/**
 * @brief 受理当前媒体代次的一条播放命令。；完整合同见 music_service.h。
 */
d_err_t music_service_request(music_command_e command, uint32_t generation,
                             uint16_t index, uint32_t *request_id)
{
    d_err_t result = D_OK;
    if (command < MUSIC_SELECT || command > MUSIC_PLAY_PAUSE || request_id == NULL) return D_ERR_INVALID_ARG;
    spin_lock(&s_music_lock);
    if (!s_music.state.ready || !s_music.state.online ||
        (command != MUSIC_RESCAN && !s_music.state.mounted) || generation != s_music.state.media_generation)
        result = D_ERR_NOT_READY;
    else if (s_music.state.held || s_music.state.busy || s_music.stop_pending ||
             s_music.state.request_id != s_music.state.completed_id) result = D_ERR_BUSY;
    else if (command == MUSIC_SELECT && index >= s_music.state.count) result = D_ERR_INVALID_ARG;
    else if ((command == MUSIC_PLAY_PAUSE && s_music.state.count == 0U) ||
             ((command == MUSIC_TOGGLE || command == MUSIC_NEXT || command == MUSIC_PREVIOUS) &&
              s_music.state.current_index == MUSIC_INDEX_NONE)) result = D_ERR_NOT_READY;
    if (result == D_OK) {
        s_music.command = command;
        s_music.command_generation = generation;
        s_music.requested_index = index;
        if (++s_music.state.request_id == 0U) ++s_music.state.request_id;
        *request_id = s_music.state.request_id;
        music_revision_locked();
    }
    spin_unlock(&s_music_lock);
    if (result == D_OK) music_wake();
    return result;
}

/**
 * @brief 提交曲目分页值请求，新请求可覆盖旧待处理请求。；完整合同见 music_service.h。
 */
d_err_t music_service_request_page(uint32_t generation, uint16_t first, uint32_t *request_id)
{
    if (request_id == NULL) return D_ERR_INVALID_ARG;
    spin_lock(&s_music_lock);
    if (!s_music.state.ready || generation != s_music.state.media_generation || first >= s_music.state.count) {
        spin_unlock(&s_music_lock); return D_ERR_NOT_READY;
    }
    s_music.requested_page = first;
    s_music.page_generation = generation;
    if (++s_music.state.page_request_id == 0U) ++s_music.state.page_request_id;
    *request_id = s_music.state.page_request_id;
    spin_unlock(&s_music_lock);
    music_wake();
    return D_OK;
}

/**
 * @brief 将单个有效占用原因位映射到票据槽。
 * @param[in] reason 一个 MUSIC_HOLD_* 原因位。
 * @return 有效槽号为 0～4，无效组合返回 -1。
 */
static int music_hold_index(uint8_t reason)
{
    int index;
    for (index = 0; index < 5; ++index) if (reason == (1U << index)) return index;
    return -1;
}

/**
 * @brief 以单个原因位和非零占用票据禁止播放，并请求停止。；完整合同见 music_service.h。
 */
d_err_t music_service_hold_begin(uint8_t reason, uint32_t ticket)
{
    int index = music_hold_index(reason);
    uint32_t previous;
    if (index < 0 || !ticket) return D_ERR_INVALID_ARG;
    spin_lock(&s_music_lock);
    previous = s_music.hold_tickets[index];
    if (previous && ((ticket != previous && (uint32_t)(ticket - previous) >= 0x80000000U) ||
        (ticket == previous && !(s_music.state.held & reason)))) {
        spin_unlock(&s_music_lock); return D_ERR_NOT_FOUND;
    }
    if (ticket == previous && !(s_music.state.owned && s_music.state.error != D_OK)) {
        spin_unlock(&s_music_lock); return D_OK;
    }
    s_music.hold_tickets[index] = ticket;
    s_music.state.held |= reason;
    s_music.stop_pending = 1U;
    music_revision_locked();
    spin_unlock(&s_music_lock);
    music_wake();
    return D_OK;
}

/**
 * @brief 非阻塞检查工作任务是否已完成停止并释放解码资源。；完整合同见 music_service.h。
 */
d_err_t music_service_hold_poll(uint8_t *ready)
{
    d_err_t result = D_OK;
    if (ready == NULL) return D_ERR_INVALID_ARG;
    spin_lock(&s_music_lock);
    *ready = !s_music.state.busy && !s_music.state.owned &&
        (!s_music.state.ready || !s_music.stop_pending);
    if (s_music.state.owned && s_music.state.error != D_OK) result = s_music.state.error;
    spin_unlock(&s_music_lock);
    return result;
}

/**
 * @brief 解除匹配原因与票据的占用。；完整合同见 music_service.h。
 */
d_err_t music_service_hold_release(uint8_t reason, uint32_t ticket)
{
    int index = music_hold_index(reason);
    d_err_t result;
    if (index < 0 || !ticket) return D_ERR_INVALID_ARG;
    spin_lock(&s_music_lock);
    if (s_music.hold_tickets[index] != ticket) {
        spin_unlock(&s_music_lock); return D_ERR_NOT_FOUND;
    }
    if (!(s_music.state.held & reason)) { spin_unlock(&s_music_lock); return D_OK; }
    if (s_music.state.busy || s_music.state.owned || (s_music.state.ready && s_music.stop_pending)) {
        result = s_music.state.owned && s_music.state.error != D_OK ? s_music.state.error : D_ERR_BUSY;
        spin_unlock(&s_music_lock); return result;
    }
    s_music.state.held &= (uint8_t)~reason;
    music_revision_locked();
    spin_unlock(&s_music_lock);
    music_wake();
    return D_OK;
}

/**
 * @brief 由音量工作任务提交音量，并等待音乐任务串行执行。；完整合同见 music_service.h。
 */
d_err_t music_service_apply_volume(uint8_t percent, uint32_t timeout_ms)
{
    uint32_t request, started = (uint32_t)timer_get_ms();
    d_err_t result;
    if (percent > 100U) return D_ERR_INVALID_ARG;
    spin_lock(&s_music_lock);
    if (!s_music.state.ready) { spin_unlock(&s_music_lock); return D_ERR_NOT_READY; }
    if (s_music.volume_requested != s_music.volume_completed) {
        spin_unlock(&s_music_lock); return D_ERR_BUSY;
    }
    s_music.requested_volume = percent;
    s_music.volume_expired = 0U;
    s_music.volume_started_ms = started;
    s_music.volume_timeout_ms = timeout_ms;
    request = ++s_music.volume_requested;
    spin_unlock(&s_music_lock);
    music_wake();
    for (;;) {
        spin_lock(&s_music_lock);
        if (s_music.volume_completed == request) {
            result = s_music.volume_result;
            spin_unlock(&s_music_lock); return result;
        }
        spin_unlock(&s_music_lock);
        if ((uint32_t)((uint32_t)timer_get_ms() - started) >= timeout_ms) {
            spin_lock(&s_music_lock);
            if (s_music.volume_completed == request) result = s_music.volume_result;
            else { s_music.volume_expired = 1U; result = D_ERR_TIMEOUT; }
            spin_unlock(&s_music_lock);
            return result;
        }
        os_time_dly(1);
    }
}

/**
 * @brief 创建持久本地音乐工作任务，扫描不自动启动播放。；完整合同见 music_service.h。
 */
d_err_t music_service_init(void)
{
    d_err_t result;
    spin_lock(&s_music_lock);
    if (s_music.attempted) {
        result = s_music.state.ready ? D_OK : D_ERR_NOT_READY;
        spin_unlock(&s_music_lock); return result;
    }
    if (!s_music.config.maximum_tracks) { spin_unlock(&s_music_lock); return D_ERR_NOT_READY; }
    s_music.attempted = 1U;
    s_music.state.current_index = MUSIC_INDEX_NONE;
    s_music.volume = d_device_volume_get();
    spin_unlock(&s_music_lock);
    result = thread_fork("local_music", MUSIC_TASK_PRIORITY, MUSIC_TASK_STACK_WORDS, 128,
                         &s_music.task_pid, music_worker, NULL) == OS_NO_ERR ? D_OK : D_ERR_IO;
    spin_lock(&s_music_lock);
    s_music.state.ready = result == D_OK;
    s_music.state.error = result;
    music_revision_locked();
    spin_unlock(&s_music_lock);
    return result;
}

/**
 * @brief 按当前文件系统操作检查占用与 SD 媒体代次。
 * @return 当前扫描或选文件操作应取消时返回非零。
 * @note SDK 可在 fscan_interrupt 返回后的 fselect 中继续调用此探针。
 */
static int music_scan_canceled(void)
{
    uint32_t generation;
    uint8_t held;
    spin_lock(&s_music_lock);
    /* SDK 在 fscan_interrupt 返回后仍保留回调，并在按编号选文件时调用；
     * 因此按工作任务当前的文件系统操作核对取消条件。 */
    held = s_music.page_read_active ? music_page_blocked_locked() : s_music.state.held;
    generation = s_music.media_epoch;
    spin_unlock(&s_music_lock);
    return held || !d_storage_sd_matches(0U, generation);
}

/**
 * @brief 检查已稳定目录的分页读取是否被维护或未完停止阻塞。
 * @return 当前禁止分页读取时返回非零。
 * @note 调用者持有 s_music_lock；语音与闹铃占用本身不禁止浏览目录。
 */
static int music_page_blocked_locked(void)
{
    return !s_music.state.online || !s_music.state.mounted ||
        (s_music.state.held & (MUSIC_HOLD_MAINTENANCE | MUSIC_HOLD_STARTUP | MUSIC_HOLD_VOLUME)) ||
        s_music.stop_pending || s_music.catalog_clear_pending || s_music.state.state == MUSIC_STOPPING;
}

/**
 * @brief 检查分页操作准入与 SD 代次是否仍有效。
 * @return 分页已阻塞或媒体代次变化时返回非零。
 */
static int music_page_canceled(void)
{
    uint32_t generation;
    int blocked;
    spin_lock(&s_music_lock);
    blocked = music_page_blocked_locked();
    generation = s_music.media_epoch;
    spin_unlock(&s_music_lock);
    return blocked || !d_storage_sd_matches(0U, generation);
}

/**
 * @brief 核对媒体和占用后执行上层播放准入探针。
 * @return 本地取消返回 D_ERR_BUSY；否则返回上层 permit 的实际结果。
 */
static d_err_t music_permit(void)
{
    if (music_scan_canceled()) return D_ERR_BUSY;
    return s_music.config.permit();
}

/**
 * @brief 将工作任务拥有的后端状态复制到公开值快照。
 * @param[in] result 要发布的当前操作错误。
 */
static void music_publish_backend(d_err_t result)
{
    uint32_t position, duration;
    music_backend_progress(&position, &duration);
    spin_lock(&s_music_lock);
    s_music.state.owned = music_backend_owned();
    s_music.state.can_resume = music_backend_can_resume();
    s_music.state.position_seconds = position;
    s_music.state.duration_seconds = duration;
    s_music.state.error = result;
    music_revision_locked();
    spin_unlock(&s_music_lock);
}

/**
 * @brief 停止解码器并发布独立的断点结果与资源状态。
 * @param[in] save 非零尝试保存续播位置。
 * @return 后端 STOP 的实际结果。
 * @note 只有 STOP 成功后才调用上层 released 回调。
 */
static d_err_t music_stop(uint8_t save)
{
    d_err_t resume_error = D_OK;
    d_err_t result = music_backend_stop(save, &resume_error);
    spin_lock(&s_music_lock);
    if (save) s_music.state.resume_error = resume_error;
    s_music.state.state = result != D_OK ? MUSIC_STOPPING :
        s_music.state.current_index == MUSIC_INDEX_NONE ? MUSIC_READY : MUSIC_PAUSED;
    spin_unlock(&s_music_lock);
    music_publish_backend(result);
    if (result == D_OK && s_music.config.released != NULL) s_music.config.released();
    return result;
}

/**
 * @brief 释放扫描与断点并清空当前曲目、分页和目录状态。
 * @note 由工作任务在成功停止后调用，不用目录清空代替资源停止。
 */
static void music_clear_catalog(void)
{
    music_backend_scan_release();
    music_backend_forget_position();
    spin_lock(&s_music_lock);
    s_music.state.count = s_music.state.discovered_count = 0U;
    s_music.state.current_index = MUSIC_INDEX_NONE;
    s_music.state.can_resume = s_music.state.limited = 0U;
    s_music.state.position_seconds = s_music.state.duration_seconds = 0U;
    s_music.state.page_count = 0U;
    s_music.state.page_completed_id = s_music.state.page_request_id;
    s_music.state.page_result = D_ERR_NOT_READY;
    memset(&s_music.page, 0, sizeof(s_music.page));
    memset(&s_music.state.current, 0, sizeof(s_music.state.current));
    s_music.catalog_clear_pending = 0U;
    spin_unlock(&s_music_lock);
}

/**
 * @brief 准备 TF 媒体并发布有界曲目目录。
 * @return 扫描或准备的实际结果；中途占用或媒体变化返回 D_ERR_BUSY。
 * @note 占用取消但媒体仍在线时保留待重扫意图；扫描不会启动播放。
 */
static d_err_t music_scan(void)
{
    d_storage_sd_snapshot_t media = {0};
    uint16_t discovered = 0U;
    d_err_t result;
    spin_lock(&s_music_lock);
    s_music.state.state = MUSIC_SCANNING;
    s_music.state.busy = 1U;
    s_music.scan_pending = 0U;
    music_revision_locked();
    spin_unlock(&s_music_lock);
    result = d_storage_sd_prepare(0U, &media);
    if (result == D_OK && !music_scan_canceled())
        result = music_backend_scan(s_music.config.formats, music_scan_canceled, &discovered);
    if (music_scan_canceled()) result = D_ERR_BUSY;
    spin_lock(&s_music_lock);
    s_music.state.mounted = media.mounted;
    s_music.state.discovered_count = result == D_OK ? discovered : 0U;
    s_music.state.count = result == D_OK ?
        (discovered > s_music.config.maximum_tracks ? s_music.config.maximum_tracks : discovered) : 0U;
    s_music.state.limited = discovered > s_music.config.maximum_tracks;
    s_music.state.state = result == D_OK ? MUSIC_READY : MUSIC_ERROR;
    s_music.state.error = result;
    s_music.state.busy = 0U;
    /* 占用只取消本次扫描，不代表用户拔卡；解除占用后重新扫描。 */
    if (result == D_ERR_BUSY && s_music.state.online) s_music.scan_pending = 1U;
    music_revision_locked();
    spin_unlock(&s_music_lock);
    return result;
}

/**
 * @brief 在工作任务中读取并发布匹配请求编号及媒体代次的曲目页。
 * @note 占用中断时保留值请求，等待解除后重试；旧代次请求结算为未就绪。
 */
static void music_read_page(void)
{
    music_page_t page = {0};
    d_err_t result = D_OK;
    uint16_t total, index;
    spin_lock(&s_music_lock);
    if (s_music.state.page_request_id == s_music.state.page_completed_id || s_music.state.busy) {
        spin_unlock(&s_music_lock); return;
    }
    /* 即使没有新页面替代，也结算旧代次的值请求；
     * 资源清理仍必须等待工作任务确认 STOP 成功。 */
    if (s_music.page_generation != s_music.state.media_generation) {
        s_music.state.page_completed_id = s_music.state.page_request_id;
        s_music.state.page_count = 0U;
        s_music.state.page_result = D_ERR_NOT_READY;
        memset(&s_music.page, 0, sizeof(s_music.page));
        music_revision_locked();
        spin_unlock(&s_music_lock);
        return;
    }
    if (music_page_blocked_locked()) {
        spin_unlock(&s_music_lock);
        return;
    }
    page.request_id = s_music.state.page_request_id;
    page.generation = s_music.page_generation;
    page.first = s_music.requested_page;
    total = s_music.state.count;
    s_music.state.busy = 1U;
    s_music.page_read_active = 1U;
    spin_unlock(&s_music_lock);
    for (index = page.first; index < total && page.count < MUSIC_PAGE_CAPACITY; ++index) {
        if (music_page_canceled()) { result = D_ERR_BUSY; break; }
        result = music_backend_item(index, &page.items[page.count]);
        if (result != D_OK) break;
        ++page.count;
    }
    if (music_page_canceled()) result = D_ERR_BUSY;
    spin_lock(&s_music_lock);
    s_music.page_read_active = 0U;
    /* 新占用可能先于此处加锁到达；保留分页请求，
     * 待清理后继续，避免把临时取消锁存成 I/O 错误。 */
    if (result != D_ERR_BUSY && !music_page_blocked_locked() &&
        page.request_id == s_music.state.page_request_id && page.generation == s_music.state.media_generation) {
        s_music.page = page;
        s_music.state.page_first = page.first;
        s_music.state.page_count = result == D_OK ? page.count : 0U;
        s_music.state.page_completed_id = page.request_id;
        s_music.state.page_result = result;
        music_revision_locked();
    }
    s_music.state.busy = 0U;
    spin_unlock(&s_music_lock);
}

/**
 * @brief 先停止旧解码器，再通过策略准入打开指定曲目。
 * @param[in] index 曲目索引。
 * @param[in] resume 非零使用已有续播断点。
 * @return 启动结果；失败时仍可能保留后端资源，快照发布真实 owned。
 */
static d_err_t music_play(uint16_t index, uint8_t resume)
{
    music_item_t item;
    d_err_t result = music_stop(0U);
    if (result != D_OK) return result;
    if (!resume) music_backend_forget_position();
    result = music_permit();
    if (result == D_OK && s_music.config.prepare != NULL) result = s_music.config.prepare();
    if (result == D_OK) result = music_permit();
    if (result == D_OK) result = music_backend_item(index, &item);
    if (result == D_OK) {
        spin_lock(&s_music_lock);
        s_music.state.current = item;
        s_music.state.current_index = index;
        s_music.state.resume_error = D_OK;
        spin_unlock(&s_music_lock);
        result = music_backend_start(index, s_music.volume, resume, music_permit);
    }
    spin_lock(&s_music_lock);
    s_music.state.state = result == D_OK ? MUSIC_PLAYING : music_backend_owned() ? MUSIC_STOPPING : MUSIC_ERROR;
    spin_unlock(&s_music_lock);
    music_publish_backend(result);
    if (result != D_OK && !music_backend_owned() && s_music.config.released != NULL) s_music.config.released();
    return result;
}

/**
 * @brief 按方向循环尝试曲目，最多遍历一轮目录。
 * @param[in] direction 正值为下一曲，非正值为上一曲。
 * @return 首个成功或不可继续操作的结果；无曲目返回 D_ERR_NOT_FOUND。
 * @note 资源仍占用或准入撤销时立即停止遍历。
 */
static d_err_t music_step(int direction)
{
    uint16_t attempt, index, count;
    music_snapshot_t state;
    d_err_t result = D_ERR_NOT_FOUND;
    music_service_get_snapshot(&state);
    count = state.count;
    index = state.current_index;
    for (attempt = 0U; attempt < count; ++attempt) {
        if (direction > 0) index = index + 1U < count ? index + 1U : 0U;
        else index = index > 0U && index < count ? index - 1U : count - 1U;
        result = music_play(index, 0U);
        if (result == D_OK || music_backend_owned() || music_permit() != D_OK) break;
    }
    return result;
}

/**
 * @brief 执行一条待处理命令并发布 completed_id 与结果。
 */
static void music_command_process(void)
{
    music_command_e command;
    uint16_t index;
    uint32_t request, generation;
    music_snapshot_t state;
    d_err_t result;
    spin_lock(&s_music_lock);
    state = s_music.state;
    if (state.request_id == state.completed_id || state.busy) { spin_unlock(&s_music_lock); return; }
    request = state.request_id;
    command = s_music.command;
    generation = s_music.command_generation;
    index = s_music.requested_index;
    s_music.state.busy = 1U;
    spin_unlock(&s_music_lock);
    result = generation != state.media_generation || state.held ? D_ERR_BUSY : D_OK;
    if (result == D_OK) {
        if (command == MUSIC_SELECT) result = music_play(index, 0U);
        else if (command == MUSIC_TOGGLE || command == MUSIC_PLAY_PAUSE) {
            if (state.state == MUSIC_PLAYING) {
                result = music_stop(1U);
            } else if (state.can_resume && state.current_index != MUSIC_INDEX_NONE) {
                result = music_play(state.current_index, 1U);
            } else if (command == MUSIC_PLAY_PAUSE) {
                result = music_play(state.current_index == MUSIC_INDEX_NONE ? 0U : state.current_index, 0U);
            } else {
                result = D_ERR_NOT_READY;
            }
        } else if (command == MUSIC_NEXT || command == MUSIC_PREVIOUS) result = music_step(command == MUSIC_NEXT ? 1 : -1);
        else {
            result = music_stop(0U);
            if (result == D_OK) {
                music_clear_catalog();
                spin_lock(&s_music_lock);
                if (++s_music.state.media_generation == 0U) ++s_music.state.media_generation;
                spin_unlock(&s_music_lock);
                result = music_scan();
            }
        }
    }
    spin_lock(&s_music_lock);
    s_music.state.command_result = result;
    s_music.state.completed_id = request;
    s_music.state.busy = 0U;
    music_revision_locked();
    spin_unlock(&s_music_lock);
}

/**
 * @brief 串行执行音量请求，并把期限后的结果固定为超时。
 * @note 迟到写入不算已确认音量；必要时先停止播放再允许后续复用。
 */
static void music_volume_process(void)
{
    uint32_t request;
    uint8_t volume, expired;
    d_err_t result;
    spin_lock(&s_music_lock);
    request = s_music.volume_requested;
    volume = s_music.requested_volume;
    if (request == s_music.volume_completed) { spin_unlock(&s_music_lock); return; }
    expired = s_music.volume_expired ||
        (uint32_t)((uint32_t)timer_get_ms() - s_music.volume_started_ms) >= s_music.volume_timeout_ms;
    spin_unlock(&s_music_lock);
    result = expired ? D_ERR_TIMEOUT : music_backend_volume(volume);
    spin_lock(&s_music_lock);
    if (s_music.volume_expired ||
        (uint32_t)((uint32_t)timer_get_ms() - s_music.volume_started_ms) >= s_music.volume_timeout_ms) {
        result = D_ERR_TIMEOUT;
        /* SDK 迟到写入不能算已确认设置；下一次播放复用前先停止，
         * 保持已确认音量缓存不变。 */
        if (!expired && s_music.state.owned) s_music.stop_pending = 1U;
    }
    if (result == D_OK) s_music.volume = volume;
    s_music.volume_result = result;
    s_music.volume_completed = request;
    spin_unlock(&s_music_lock);
}

/**
 * @brief 按媒体变化、停止、音量、扫描、播放命令与分页顺序推进一轮。
 * @note 所有 SDK 对象由当前工作任务持有；停止失败后保留待清目录，不能提前释放。
 */
static void music_cycle(void)
{
    d_storage_sd_snapshot_t media = {0};
    music_snapshot_t state;
    uint8_t stop, scan, changed;
    d_err_t result;
    static uint16_t bad_tracks;
    music_service_get_snapshot(&state);
    if (!state.ready) return;
    (void)d_storage_sd_snapshot(0U, &media);
    spin_lock(&s_music_lock);
    changed = media.generation != s_music.media_epoch;
    if (changed) {
        s_music.media_epoch = media.generation;
        if (++s_music.state.media_generation == 0U) ++s_music.state.media_generation;
        s_music.state.online = media.online;
        s_music.state.mounted = media.mounted;
        s_music.state.count = 0U;
        s_music.state.current_index = MUSIC_INDEX_NONE;
        s_music.scan_pending = media.online;
        s_music.catalog_clear_pending = 1U;
        s_music.stop_pending = 1U;
        s_music.state.completed_id = s_music.state.request_id;
        s_music.state.command_result = D_ERR_NOT_READY;
        music_revision_locked();
    }
    stop = s_music.stop_pending;
    if (stop) { s_music.stop_pending = 0U; s_music.state.busy = 1U; }
    spin_unlock(&s_music_lock);
    if (stop) {
        result = music_stop(media.online && state.current_index != MUSIC_INDEX_NONE &&
                            !changed && music_backend_owned());
        if (result == D_OK && s_music.catalog_clear_pending) music_clear_catalog();
        spin_lock(&s_music_lock);
        s_music.state.busy = 0U;
        if (!media.online && result == D_OK) s_music.state.state = MUSIC_ABSENT;
        spin_unlock(&s_music_lock);
    }
    music_volume_process();
    spin_lock(&s_music_lock);
    scan = s_music.scan_pending && !s_music.state.held && !s_music.state.owned;
    spin_unlock(&s_music_lock);
    if (scan) music_scan();
    music_service_get_snapshot(&state);
    if (!state.held && state.online && !state.busy) {
        if (state.request_id != state.completed_id) {
            bad_tracks = 0U;
            music_command_process();
        } else if (state.state == MUSIC_PLAYING && music_backend_ended(&result)) {
            if (result != D_OK) ++bad_tracks;
            else bad_tracks = 0U;
            spin_lock(&s_music_lock); s_music.state.busy = 1U; spin_unlock(&s_music_lock);
            if (bad_tracks >= state.count || (!s_music.config.repeat_all && state.current_index + 1U >= state.count)) {
                if (music_stop(0U) == D_OK) {
                    spin_lock(&s_music_lock);
                    s_music.state.error = bad_tracks ? D_ERR_IO : D_OK;
                    s_music.state.state = bad_tracks ? MUSIC_ERROR : MUSIC_READY;
                    spin_unlock(&s_music_lock);
                }
            } else (void)music_step(1);
            spin_lock(&s_music_lock); s_music.state.busy = 0U; spin_unlock(&s_music_lock);
        }
    }
    music_read_page();
    music_service_get_snapshot(&state);
    if (state.state == MUSIC_PLAYING) {
        uint32_t position, duration;
        music_backend_progress(&position, &duration);
        if (position != state.position_seconds) music_publish_backend(D_OK);
    }
    /* 仅在资源所有者工作任务完成 STOP 后结算释放意图。 */
    if (!state.owned && !state.busy && s_music.config.released != NULL) s_music.config.released();
}

/**
 * @brief 持续接收同队列 SDK 回调并推进本地音乐状态。
 * @param[in] context 任务兼容参数，本实现不使用。
 */
static void music_worker(void *context)
{
    int messages[16];
    (void)context;
    for (;;) {
        /* SDK 回调由同一队列派发，FILE 指针不跨所有者传递。 */
        (void)__os_taskq_pend(messages, 16, MUSIC_POLL_TICKS);
        music_cycle();
    }
}
#endif
