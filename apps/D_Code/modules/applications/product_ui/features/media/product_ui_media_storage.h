/** @file product_ui_media_storage.h @brief UI 自有固定容量媒体值容器，不拥有文件系统、解码器或后台 worker。 */

#ifndef __PRODUCT_UI_MEDIA_STORAGE_H__
#define __PRODUCT_UI_MEDIA_STORAGE_H__

#include "framework/d_error.h"
#include <stdint.h>

#define PRODUCT_UI_MEDIA_QUEUE_CAPACITY        64U
#define PRODUCT_UI_MEDIA_PAGE_CACHE_CAPACITY   32U
#define PRODUCT_UI_MEDIA_TITLE_MAX             64U
#define PRODUCT_UI_MEDIA_ARTIST_MAX            48U
#define PRODUCT_UI_MEDIA_PATH_MAX              128U
#define PRODUCT_UI_MEDIA_QUEUE_INDEX_INVALID   0xFFFFU

typedef enum {
    PRODUCT_UI_SD_CAPABILITY_UNKNOWN = 0,
    PRODUCT_UI_SD_CAPABILITY_UNAVAILABLE,
    PRODUCT_UI_SD_CAPABILITY_READY,
    PRODUCT_UI_SD_CAPABILITY_ERROR,
} product_ui_sd_capability_e;

typedef enum {
    PRODUCT_UI_SD_HOTPLUG_UNKNOWN = 0,
    PRODUCT_UI_SD_HOTPLUG_ABSENT,
    PRODUCT_UI_SD_HOTPLUG_PRESENT,
    PRODUCT_UI_SD_HOTPLUG_INSERTED,
    PRODUCT_UI_SD_HOTPLUG_REMOVED,
    PRODUCT_UI_SD_HOTPLUG_ERROR,
} product_ui_sd_hotplug_state_e;

typedef enum {
    PRODUCT_UI_MEDIA_COMMAND_NONE = 0,
    PRODUCT_UI_MEDIA_COMMAND_SCAN_START,
    PRODUCT_UI_MEDIA_COMMAND_SCAN_CANCEL,
    PRODUCT_UI_MEDIA_COMMAND_QUEUE_SELECT,
    PRODUCT_UI_MEDIA_COMMAND_QUEUE_NEXT,
    PRODUCT_UI_MEDIA_COMMAND_QUEUE_PREVIOUS,
    PRODUCT_UI_MEDIA_COMMAND_PAGE_REQUEST,
} product_ui_media_command_type_e;

typedef struct {
    uint32_t media_id;
    uint32_t duration_ms;
    uint32_t size_bytes;
    char title[PRODUCT_UI_MEDIA_TITLE_MAX];
    char artist[PRODUCT_UI_MEDIA_ARTIST_MAX];
    char path[PRODUCT_UI_MEDIA_PATH_MAX];
} product_ui_media_item_t;

typedef struct {
    uint16_t page_number;
    uint16_t first_queue_index;
    uint16_t item_count;
    uint16_t reserved;
    uint32_t source_version; /**< 同一初始化生命周期内生成索引时的 queue_version。 */
} product_ui_media_page_index_t;

typedef struct {
    product_ui_media_command_type_e type;
    uint32_t request_id;
    uint16_t argument;
    uint16_t reserved;
} product_ui_media_command_t;

typedef struct {
    uint32_t version;
    uint32_t queue_version;
    uint32_t page_cache_version;
    uint32_t hotplug_version;
    uint32_t command_version;
    product_ui_sd_capability_e sd_capability;
    product_ui_sd_hotplug_state_e hotplug_state;
    int32_t sd_error;
    uint16_t queue_count;
    uint16_t current_queue_index;
    uint8_t page_cache_count;
    uint8_t media_present;
    uint8_t command_pending;
    uint8_t reserved;
    product_ui_media_command_t pending_command;
    product_ui_media_item_t queue[PRODUCT_UI_MEDIA_QUEUE_CAPACITY];
    product_ui_media_page_index_t page_cache[
        PRODUCT_UI_MEDIA_PAGE_CACHE_CAPACITY];
} product_ui_media_storage_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t queue_version;
    uint32_t page_cache_version;
    uint32_t hotplug_version;
    uint32_t command_version;
    product_ui_sd_capability_e sd_capability;
    product_ui_sd_hotplug_state_e hotplug_state;
    int32_t sd_error;
    uint16_t queue_count;
    uint16_t current_queue_index;
    uint8_t page_cache_count;
    uint8_t media_present;
    uint8_t command_pending;
} product_ui_media_storage_summary_t;

/**
 * @brief 重建静态容器并置为已初始化，全部版本及请求序列从一开始。
 * @return 当前实现返回 D_OK。
 * @note 重复调用也重置当前数据；没有驱动、扫描、文件系统或持久化操作。全容器由调用方单上下文串行使用。
 */
d_err_t product_ui_media_storage_init(void);

/**
 * @brief 清空快照及请求序列并返回未初始化状态。
 * @note 只清除静态值，无文件、worker 或硬件资源需要释放。
 */
void product_ui_media_storage_deinit(void);

/**
 * @brief 在保持已初始化的前提下重置所有快照和请求序列。
 * @return 已初始化返回 D_OK，否则返回 NOT_READY。
 */
d_err_t product_ui_media_storage_reset(void);

/**
 * @brief 复制整个固定容量媒体状态。
 * @param[out] snapshot 调用方拥有的完整值副本。
 * @return 成功返回 D_OK；空输出为 INVALID_ARG，未初始化为 NOT_READY。
 * @note 非 ISR 安全，无内部锁；摘要与后续条目读取不构成跨线程事务。
 */
d_err_t product_ui_media_storage_snapshot_get(
    product_ui_media_storage_snapshot_t *snapshot);
/**
 * @brief 复制不含队列及页索引数组的状态摘要。
 * @param[out] summary 调用方拥有的摘要副本。
 * @return 成功返回 D_OK；空输出为 INVALID_ARG，未初始化为 NOT_READY。
 */
d_err_t product_ui_media_storage_summary_get(
    product_ui_media_storage_summary_t *summary);
/**
 * @brief 复制当前队列中指定下标的媒体条目。
 * @param[in] index 从零开始的当前队列下标。
 * @param[out] item 条目值副本，失败不写入。
 * @return 成功返回 D_OK；空输出或下标越界为 INVALID_ARG，未初始化为 NOT_READY。
 */
d_err_t product_ui_media_storage_queue_item_get(
    uint16_t index, product_ui_media_item_t *item);
/**
 * @brief 复制当前页缓存中指定下标的页索引。
 * @param[in] index 从零开始的页缓存下标。
 * @param[out] page_index 页索引值副本，失败不写入。
 * @return 成功返回 D_OK；空输出或下标越界为 INVALID_ARG，未初始化为 NOT_READY。
 */
d_err_t product_ui_media_storage_page_index_get(
    uint8_t index, product_ui_media_page_index_t *page_index);

/**
 * @brief 发布适配层观察的 SD 能力，READY 要求已知媒体存在。
 * @param[in] capability 明确的未知、不可用、就绪或错误能力。
 * @param[in] error_code 仅 ERROR 状态保留的非零诊断码；其他状态清零。
 * @return 成功返回 D_OK；未初始化或媒体不存在却发布 READY 为 NOT_READY；非法枚举或 ERROR 搭配零错误码为 INVALID_ARG。
 */
d_err_t product_ui_media_storage_sd_publish(
    product_ui_sd_capability_e capability, int32_t error_code);

/**
 * @brief 发布 SD 热插拔状态并更新能力及容器内容。
 * @param[in] state 热插拔观察状态。
 * @return 成功返回 D_OK；未初始化为 NOT_READY，非法枚举为 INVALID_ARG。
 * @note ABSENT／REMOVED 清除队列、页索引和待处理命令；插入只确认存在，能力仍待后端探测。
 */
d_err_t product_ui_media_storage_hotplug_publish(
    product_ui_sd_hotplug_state_e state);

/**
 * @brief 验证后完整替换静态队列并使所有页索引失效。
 * @param[in] items 本次复制的条目数组；count 为零时可为空。
 * @param[in] count 不超过 QUEUE_CAPACITY 的条目数量。
 * @param[in] current_index 非空队列中的有效下标；空队列必须用 INDEX_INVALID。
 * @return 成功返回 D_OK；未初始化为 NOT_READY；数量、空指针或当前下标无效为 INVALID_ARG。
 * @note 包括同长度替换也推进队列及页缓存代次；拒绝输入不改状态，标题、作者和路径按显示 UTF-8 规则清洗后复制。
 */
d_err_t product_ui_media_storage_queue_replace(
    const product_ui_media_item_t *items, uint16_t count,
    uint16_t current_index);

/**
 * @brief 清空队列与页缓存并推进其内容代次。
 * @return 成功返回 D_OK，未初始化返回 NOT_READY。
 */
d_err_t product_ui_media_storage_queue_clear(void);

/**
 * @brief 验证整个批次的范围与队列代次后再完整替换页索引。
 * @param[in] entries 本次复制的索引数组；count 为零可为空。
 * @param[in] count 不超过 PAGE_CACHE_CAPACITY 的条目数。
 * @return 成功返回 D_OK；未初始化或来源代次过期为 NOT_READY；数量、指针或区间无效为 INVALID_ARG。
 * @note 非空条目均须匹配同一初始化生命周期的当前 queue_version；count 为零直接清空，不检查来源代次。
 */
d_err_t product_ui_media_storage_page_cache_replace(
    const product_ui_media_page_index_t *entries, uint8_t count);

/**
 * @brief 向单个静态待处理值槽提交命令并分配非零请求编号。
 * @param[in] type 待提交的有类型命令，NONE 不可提交。
 * @param[in] argument SELECT 的队列下标或 PAGE_REQUEST 的页号，其余依命令解释。
 * @param[out] request_id 必填；仅成功时写入非零编号，失败保持原值。
 * @return 成功受理为 D_OK；未初始化或能力／队列不可用为 NOT_READY；已有命令为 BUSY；参数或下标无效为 INVALID_ARG。
 * @note 此处没有附着 worker，受理不代表执行或完成；编号只标识当前初始化生命周期内的单槽值。
 */
d_err_t product_ui_media_storage_command_submit(
    product_ui_media_command_type_e type, uint16_t argument,
    uint32_t *request_id);

/**
 * @brief 复制并移除当前待处理命令。
 * @param[out] command 调用方拥有的命令副本；失败不写入。
 * @return 成功取走为 D_OK；空输出为 INVALID_ARG，未初始化为 NOT_READY，无命令为 NOT_FOUND。
 * @note 取走不代表执行或完成确认，执行者须自行定义业务完成合同。
 */
d_err_t product_ui_media_storage_command_take(
    product_ui_media_command_t *command);

#endif /* __PRODUCT_UI_MEDIA_STORAGE_H__ */
