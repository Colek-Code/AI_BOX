/** @file product_ui_media_storage.c @brief UI 自有固定容量媒体值容器，不拥有文件系统、解码器或后台 worker。 */

#include "product_ui_media_storage.h"
#include "modules/applications/product_ui/shared/text/product_ui_text.h"
#include <string.h>

/* 单一应用上下文拥有的值容器，不承担文件系统或 worker 生命周期。 */
typedef struct {
    product_ui_media_storage_snapshot_t snapshot;
    uint32_t next_request_id;
    uint8_t is_initialized;
} product_ui_media_storage_state_t;

static product_ui_media_storage_state_t s_media_storage;

/**
 * @brief 推进非零版本／请求序列，回绕时跳过零。
 * @param[in,out] version 调用链保证非空的版本字段。
 */
static void product_ui_media_storage_version_increment(uint32_t *version)
{
    (*version)++;
    if (*version == 0U) {
        *version = 1U;
    }
}

/**
 * @brief 推进容器总体内容代次。
 */
static void product_ui_media_storage_snapshot_changed(void)
{
    product_ui_media_storage_version_increment(&s_media_storage.snapshot.version);
}

/**
 * @brief 清空全部媒体条目、设置无效当前下标并推进队列代次。
 */
static void product_ui_media_storage_queue_clear_internal(void)
{
    memset(s_media_storage.snapshot.queue, 0, sizeof(s_media_storage.snapshot.queue));
    s_media_storage.snapshot.queue_count = 0U;
    s_media_storage.snapshot.current_queue_index = PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID;
    product_ui_media_storage_version_increment(&s_media_storage.snapshot.queue_version);
}

/**
 * @brief 清空页索引数组及数量，并推进页缓存代次。
 */
static void product_ui_media_storage_page_cache_clear_internal(void)
{
    memset(s_media_storage.snapshot.page_cache, 0, sizeof(s_media_storage.snapshot.page_cache));
    s_media_storage.snapshot.page_cache_count = 0U;
    product_ui_media_storage_version_increment(&s_media_storage.snapshot.page_cache_version);
}

/**
 * @brief 清空待处理命令和占用标志，并推进命令代次。
 */
static void product_ui_media_storage_command_clear_internal(void)
{
    memset(&s_media_storage.snapshot.pending_command, 0,
           sizeof(s_media_storage.snapshot.pending_command));
    s_media_storage.snapshot.command_pending = 0U;
    product_ui_media_storage_version_increment(&s_media_storage.snapshot.command_version);
}

/**
 * @brief 重置快照、各独立版本和请求序列，不改变外层初始化标志。
 */
static void product_ui_media_storage_state_initialize(void)
{
    memset(&s_media_storage.snapshot, 0, sizeof(s_media_storage.snapshot));
    s_media_storage.snapshot.version = 1U;
    s_media_storage.snapshot.queue_version = 1U;
    s_media_storage.snapshot.page_cache_version = 1U;
    s_media_storage.snapshot.hotplug_version = 1U;
    s_media_storage.snapshot.command_version = 1U;
    s_media_storage.snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNKNOWN;
    s_media_storage.snapshot.hotplug_state = PRODUCT_UI_SD_HOTPLUG_UNKNOWN;
    s_media_storage.snapshot.current_queue_index = PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID;
    s_media_storage.next_request_id = 1U;
}

/**
 * @brief 复制媒体值并按各固定缓冲上限清洗标题、作者和路径。
 * @param[out] output 调用方提供的目标条目，不与输入重叠。
 * @param[in] input 本次借用的源条目。
 */
static void product_ui_media_storage_item_copy(
    product_ui_media_item_t *output, const product_ui_media_item_t *input)
{
    *output = *input;
    product_ui_utf8_copy_n(output->title, sizeof(output->title),
                           input->title, sizeof(input->title));
    product_ui_utf8_copy_n(output->artist, sizeof(output->artist),
                           input->artist, sizeof(input->artist));
    product_ui_utf8_copy_n(output->path, sizeof(output->path),
                           input->path, sizeof(input->path));
}

/**
 * @brief 检查 SD 能力枚举是否在声明范围内。
 * @param[in] capability 待检查的 SD 能力。
 * @return 有效返回非零，否则为零。
 */
static uint8_t product_ui_media_storage_sd_state_valid(
    product_ui_sd_capability_e capability)
{
    return capability >= PRODUCT_UI_SD_CAPABILITY_UNKNOWN &&
           capability <= PRODUCT_UI_SD_CAPABILITY_ERROR;
}

/**
 * @brief 检查 SD 热插拔枚举是否在声明范围内。
 * @param[in] state 待检查的热插拔状态。
 * @return 有效返回非零，否则为零。
 */
static uint8_t product_ui_media_storage_hotplug_state_valid(
    product_ui_sd_hotplug_state_e state)
{
    return state >= PRODUCT_UI_SD_HOTPLUG_UNKNOWN &&
           state <= PRODUCT_UI_SD_HOTPLUG_ERROR;
}

/**
 * @brief 验证命令类型及其对当前 SD 能力、队列和选择下标的要求。
 * @param[in] type 待提交命令。
 * @param[in] argument 选择下标或命令参数。
 * @return 允许受理返回 D_OK，非法参数为 INVALID_ARG，条件未就绪为 NOT_READY。
 */
static d_err_t product_ui_media_storage_command_validate(
    product_ui_media_command_type_e type, uint16_t argument)
{
    if ((type <= PRODUCT_UI_MEDIA_COMMAND_NONE) ||
        (type > PRODUCT_UI_MEDIA_COMMAND_PAGE_REQUEST)) {
        return D_ERR_INVALID_ARG;
    }
    if (((type == PRODUCT_UI_MEDIA_COMMAND_SCAN_START) ||
         (type == PRODUCT_UI_MEDIA_COMMAND_PAGE_REQUEST)) &&
        (s_media_storage.snapshot.sd_capability != PRODUCT_UI_SD_CAPABILITY_READY)) {
        return D_ERR_NOT_READY;
    }
    if ((type == PRODUCT_UI_MEDIA_COMMAND_QUEUE_SELECT) &&
        (argument >= s_media_storage.snapshot.queue_count)) {
        return D_ERR_INVALID_ARG;
    }
    if (((type == PRODUCT_UI_MEDIA_COMMAND_QUEUE_NEXT) ||
         (type == PRODUCT_UI_MEDIA_COMMAND_QUEUE_PREVIOUS)) &&
        (s_media_storage.snapshot.queue_count == 0U)) {
        return D_ERR_NOT_READY;
    }
    return D_OK;
}

/**
 * @brief 重建静态容器并置为已初始化，全部版本及请求序列从一开始。
 */
d_err_t product_ui_media_storage_init(void)
{
    product_ui_media_storage_state_initialize();
    s_media_storage.is_initialized = 1U;
    return D_OK;
}

/**
 * @brief 清空快照及请求序列并返回未初始化状态。
 */
void product_ui_media_storage_deinit(void)
{
    memset(&s_media_storage.snapshot, 0, sizeof(s_media_storage.snapshot));
    s_media_storage.next_request_id = 0U;
    s_media_storage.is_initialized = 0U;
}

/**
 * @brief 在保持已初始化的前提下重置所有快照和请求序列。
 */
d_err_t product_ui_media_storage_reset(void)
{
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    product_ui_media_storage_state_initialize();
    return D_OK;
}

/**
 * @brief 复制整个固定容量媒体状态。
 */
d_err_t product_ui_media_storage_snapshot_get(
    product_ui_media_storage_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    *snapshot = s_media_storage.snapshot;
    return D_OK;
}

/**
 * @brief 复制不含队列及页索引数组的状态摘要。
 */
d_err_t product_ui_media_storage_summary_get(
    product_ui_media_storage_summary_t *summary)
{
    if (summary == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    memset(summary, 0, sizeof(*summary));
    summary->version = s_media_storage.snapshot.version;
    summary->queue_version = s_media_storage.snapshot.queue_version;
    summary->page_cache_version = s_media_storage.snapshot.page_cache_version;
    summary->hotplug_version = s_media_storage.snapshot.hotplug_version;
    summary->command_version = s_media_storage.snapshot.command_version;
    summary->sd_capability = s_media_storage.snapshot.sd_capability;
    summary->hotplug_state = s_media_storage.snapshot.hotplug_state;
    summary->sd_error = s_media_storage.snapshot.sd_error;
    summary->queue_count = s_media_storage.snapshot.queue_count;
    summary->current_queue_index = s_media_storage.snapshot.current_queue_index;
    summary->page_cache_count = s_media_storage.snapshot.page_cache_count;
    summary->media_present = s_media_storage.snapshot.media_present;
    summary->command_pending = s_media_storage.snapshot.command_pending;
    return D_OK;
}

/**
 * @brief 复制当前队列中指定下标的媒体条目。
 */
d_err_t product_ui_media_storage_queue_item_get(
    uint16_t index, product_ui_media_item_t *item)
{
    if (item == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if (index >= s_media_storage.snapshot.queue_count) {
        return D_ERR_INVALID_ARG;
    }
    *item = s_media_storage.snapshot.queue[index];
    return D_OK;
}

/**
 * @brief 复制当前页缓存中指定下标的页索引。
 */
d_err_t product_ui_media_storage_page_index_get(
    uint8_t index, product_ui_media_page_index_t *page_index)
{
    if (page_index == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if (index >= s_media_storage.snapshot.page_cache_count) {
        return D_ERR_INVALID_ARG;
    }
    *page_index = s_media_storage.snapshot.page_cache[index];
    return D_OK;
}

/**
 * @brief 发布适配层观察的 SD 能力，READY 要求已知媒体存在。
 */
d_err_t product_ui_media_storage_sd_publish(
    product_ui_sd_capability_e capability, int32_t error_code)
{
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if (!product_ui_media_storage_sd_state_valid(capability)) {
        return D_ERR_INVALID_ARG;
    }
    if ((capability == PRODUCT_UI_SD_CAPABILITY_READY) &&
        !s_media_storage.snapshot.media_present) {
        return D_ERR_NOT_READY;
    }
    if ((capability == PRODUCT_UI_SD_CAPABILITY_ERROR) &&
        (error_code == 0)) {
        return D_ERR_INVALID_ARG;
    }

    s_media_storage.snapshot.sd_capability = capability;
    s_media_storage.snapshot.sd_error =
        (capability == PRODUCT_UI_SD_CAPABILITY_ERROR) ? error_code : 0;
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

/**
 * @brief 发布 SD 热插拔状态并更新能力及容器内容。
 */
d_err_t product_ui_media_storage_hotplug_publish(
    product_ui_sd_hotplug_state_e state)
{
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if (!product_ui_media_storage_hotplug_state_valid(state)) {
        return D_ERR_INVALID_ARG;
    }

    s_media_storage.snapshot.hotplug_state = state;
    switch (state) {
    case PRODUCT_UI_SD_HOTPLUG_PRESENT:
    case PRODUCT_UI_SD_HOTPLUG_INSERTED:
        s_media_storage.snapshot.media_present = 1U;
        s_media_storage.snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNKNOWN;
        s_media_storage.snapshot.sd_error = 0;
        break;
    case PRODUCT_UI_SD_HOTPLUG_ABSENT:
    case PRODUCT_UI_SD_HOTPLUG_REMOVED:
        s_media_storage.snapshot.media_present = 0U;
        s_media_storage.snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNAVAILABLE;
        s_media_storage.snapshot.sd_error = 0;
        product_ui_media_storage_queue_clear_internal();
        product_ui_media_storage_page_cache_clear_internal();
        if (s_media_storage.snapshot.command_pending) {
            product_ui_media_storage_command_clear_internal();
        }
        break;
    case PRODUCT_UI_SD_HOTPLUG_ERROR:
        s_media_storage.snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_ERROR;
        s_media_storage.snapshot.sd_error = D_ERR_IO;
        break;
    case PRODUCT_UI_SD_HOTPLUG_UNKNOWN:
    default:
        s_media_storage.snapshot.media_present = 0U;
        s_media_storage.snapshot.sd_capability = PRODUCT_UI_SD_CAPABILITY_UNKNOWN;
        s_media_storage.snapshot.sd_error = 0;
        break;
    }
    product_ui_media_storage_version_increment(&s_media_storage.snapshot.hotplug_version);
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

/**
 * @brief 验证后完整替换静态队列并使所有页索引失效。
 */
d_err_t product_ui_media_storage_queue_replace(
    const product_ui_media_item_t *items, uint16_t count,
    uint16_t current_index)
{
    uint16_t index;

    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if ((count > PRODUCT_UI_MEDIA_QUEUE_CAPACITY) ||
        ((count > 0U) && (items == NULL))) {
        return D_ERR_INVALID_ARG;
    }
    if (((count == 0U) &&
         (current_index != PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID)) ||
        ((count > 0U) && (current_index >= count))) {
        return D_ERR_INVALID_ARG;
    }

    memset(s_media_storage.snapshot.queue, 0, sizeof(s_media_storage.snapshot.queue));
    if (count > 0U) {
        for (index = 0U; index < count; index++) {
            product_ui_media_storage_item_copy(&s_media_storage.snapshot.queue[index],
                                               &items[index]);
        }
    }
    s_media_storage.snapshot.queue_count = count;
    s_media_storage.snapshot.current_queue_index = current_index;
    product_ui_media_storage_version_increment(&s_media_storage.snapshot.queue_version);
    /* 替换即改变队列身份，即使队列长度未变也必须使旧页缓存失效。 */
    product_ui_media_storage_page_cache_clear_internal();
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

/**
 * @brief 清空队列与页缓存并推进其内容代次。
 */
d_err_t product_ui_media_storage_queue_clear(void)
{
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    product_ui_media_storage_queue_clear_internal();
    product_ui_media_storage_page_cache_clear_internal();
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

/**
 * @brief 验证整个批次的范围与队列代次后再完整替换页索引。
 */
d_err_t product_ui_media_storage_page_cache_replace(
    const product_ui_media_page_index_t *entries, uint8_t count)
{
    uint8_t index;
    uint32_t range_end;

    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if ((count > PRODUCT_UI_MEDIA_PAGE_CACHE_CAPACITY) ||
        ((count > 0U) && (entries == NULL))) {
        return D_ERR_INVALID_ARG;
    }
    for (index = 0U; index < count; index++) {
        range_end = (uint32_t)entries[index].first_queue_index +
                    entries[index].item_count;
        if (range_end > s_media_storage.snapshot.queue_count) {
            return D_ERR_INVALID_ARG;
        }
    }
    for (index = 0U; index < count; index++) {
        if (entries[index].source_version != s_media_storage.snapshot.queue_version) {
            return D_ERR_NOT_READY;
        }
    }

    memset(s_media_storage.snapshot.page_cache, 0, sizeof(s_media_storage.snapshot.page_cache));
    if (count > 0U) {
        memcpy(s_media_storage.snapshot.page_cache, entries,
               (size_t)count * sizeof(s_media_storage.snapshot.page_cache[0]));
    }
    s_media_storage.snapshot.page_cache_count = count;
    product_ui_media_storage_version_increment(
        &s_media_storage.snapshot.page_cache_version);
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

/**
 * @brief 向单个静态待处理值槽提交命令并分配非零请求编号。
 */
d_err_t product_ui_media_storage_command_submit(
    product_ui_media_command_type_e type, uint16_t argument,
    uint32_t *request_id)
{
    d_err_t result;

    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if (request_id == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (s_media_storage.snapshot.command_pending) {
        return D_ERR_BUSY;
    }
    result = product_ui_media_storage_command_validate(type, argument);
    if (result != D_OK) {
        return result;
    }

    s_media_storage.snapshot.pending_command.type = type;
    s_media_storage.snapshot.pending_command.argument = argument;
    s_media_storage.snapshot.pending_command.request_id = s_media_storage.next_request_id;
    s_media_storage.snapshot.command_pending = 1U;
    *request_id = s_media_storage.next_request_id;
    product_ui_media_storage_version_increment(&s_media_storage.next_request_id);
    product_ui_media_storage_version_increment(&s_media_storage.snapshot.command_version);
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}

/**
 * @brief 复制并移除当前待处理命令。
 */
d_err_t product_ui_media_storage_command_take(
    product_ui_media_command_t *command)
{
    if (command == NULL) {
        return D_ERR_INVALID_ARG;
    }
    if (!s_media_storage.is_initialized) {
        return D_ERR_NOT_READY;
    }
    if (!s_media_storage.snapshot.command_pending) {
        return D_ERR_NOT_FOUND;
    }

    *command = s_media_storage.snapshot.pending_command;
    product_ui_media_storage_command_clear_internal();
    product_ui_media_storage_snapshot_changed();
    return D_OK;
}
