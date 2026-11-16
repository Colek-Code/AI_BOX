/** @file product_ui_time_domain.c @brief UI 串行持有的墙钟与日程状态，闹钟记录仍由绑定运行时持有。 */

#include "product_ui_time_domain.h"
#include "modules/applications/product_ui/features/time/product_ui_time_domain_internal.h"
#include "modules/applications/product_ui/shared/text/product_ui_text.h"
#include <string.h>

typedef struct {
    uint8_t used;
    product_ui_calendar_event_t value;
} product_ui_event_slot_t;
typedef struct {
    uint8_t initialized;
    uint8_t wall_time_valid;
    uint32_t generation;
    uint32_t next_event_id;
    product_ui_time_capability_e capability;
    product_ui_time_unavailable_reason_e unavailable_reason;
    product_ui_datetime_t wall_time;
    product_ui_event_slot_t events[PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS];
} product_ui_time_domain_state_t;
static product_ui_time_domain_state_t s_time_domain;
static const product_ui_time_alarm_backend_t *s_alarm_backend;
static uint32_t s_alarm_generation;
/* 缓存代次位于可清零状态之外，防止显式 init／RESET 复用活页面的旧键；
 * 这是运行时 UI 缓存身份，不是 VM 数据。 */
static uint32_t s_event_revision;

/**
 * @brief 推进独立日程缓存代次并跳过零。
 */
static void product_ui_time_domain_mark_events_changed(void)
{
    s_event_revision++;
    if (s_event_revision == 0U) {
        s_event_revision = 1U;
    }
}

/**
 * @brief 绑定静态闹钟后端并重置已观察的闹钟代次。
 */
void product_ui_time_domain_bind_alarms(const product_ui_time_alarm_backend_t *backend)
{
    s_alarm_backend = backend;
    s_alarm_generation = 0U;
}

/**
 * @brief 清空 UI 自有域并设置总内容代次，同时推进独立日程缓存代次。
 * @param[in] generation 调用方已经处理回绕的非零总内容代次。
 */
static void product_ui_time_domain_clear(uint32_t generation)
{
    memset(&s_time_domain, 0, sizeof(s_time_domain));
    s_time_domain.initialized = 1U;
    s_time_domain.generation = generation;
    s_time_domain.next_event_id = 1U;
    s_time_domain.capability = PRODUCT_UI_TIME_CAPABILITY_UNKNOWN;
    s_time_domain.unavailable_reason = PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED;
    product_ui_time_domain_mark_events_changed();
}

/**
 * @brief 重置 UI 拥有的墙钟能力及日程 RAM 缓存，保留已绑定的持久闹钟。
 */
void product_ui_time_domain_init(void)
{
    product_ui_time_domain_clear(1U);
}

/**
 * @brief 在首次访问时建立默认 UI 域，不重新初始化已有域。
 */
static void product_ui_time_domain_ensure_initialized(void)
{
    if (!s_time_domain.initialized) {
        product_ui_time_domain_init();
    }
}

/**
 * @brief 推进域总内容代次并跳过零。
 */
static void product_ui_time_domain_mark_changed(void)
{
    s_time_domain.generation++;
    if (s_time_domain.generation == 0U) {
        s_time_domain.generation = 1U;
    }
}

/**
 * @brief 在已使用日程槽中查找编号。
 * @param[in] id 待查日程编号。
 * @return 找到返回物理槽下标，否则返回 -1。
 */
static int product_ui_time_find_event(uint32_t id)
{
    uint32_t index;
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (s_time_domain.events[index].used &&
            (s_time_domain.events[index].value.id == id)) {
            return (int)index;
        }
    }
    return -1;
}

/**
 * @brief 查找第一个未使用的日程槽。
 * @return 空槽下标；全部占用返回 -1。
 */
static int product_ui_time_find_free_event(void)
{
    uint32_t index;
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (!s_time_domain.events[index].used) {
            return (int)index;
        }
    }
    return -1;
}

/**
 * @brief 在固定次数内分配未使用且非零的日程编号。
 * @return 可用编号；找不到时为零。
 * @note 候选计数会回绕并跳过零，不按编号大小表达时间顺序。
 */
static uint32_t product_ui_time_allocate_event_id(void)
{
    uint32_t attempt;
    uint32_t candidate;
    for (attempt = 0U; attempt <= PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; attempt++) {
        candidate = s_time_domain.next_event_id++;
        if (s_time_domain.next_event_id == 0U) {
            s_time_domain.next_event_id = 1U;
        }
        if ((candidate != 0U) && (product_ui_time_find_event(candidate) < 0)) {
            return candidate;
        }
    }
    return 0U;
}

/**
 * @brief 验证日程、分配编号及空槽，清洗有限文字后复制到 RAM。
 * @param[in] input 借用日程值；编号为零请求新编号。
 * @param[out] affected_id 调用方保证非空，成功时接收保存编号。
 * @return 成功返回 TIME_OK；无效为 INVALID_ARGUMENT，编号或槽耗尽为 CAPACITY。
 * @note 失败可能已消耗候选编号，但不发布记录及日程代次；调用方成功后统一推进代次。
 */
static product_ui_time_result_e product_ui_time_upsert_event(
    const product_ui_calendar_event_t *input, uint32_t *affected_id)
{
    product_ui_calendar_event_t value;
    int index;
    if (product_ui_time_domain_validate_event(input) != PRODUCT_UI_TIME_OK) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    value = *input;
    if (value.id == 0U) {
        value.id = product_ui_time_allocate_event_id();
        if (value.id == 0U) {
            return PRODUCT_UI_TIME_ERR_CAPACITY;
        }
    }
    index = product_ui_time_find_event(value.id);
    if (index < 0) {
        index = product_ui_time_find_free_event();
        if (index < 0) {
            return PRODUCT_UI_TIME_ERR_CAPACITY;
        }
    }
    product_ui_utf8_copy_n(
        value.title, sizeof(value.title), input->title, sizeof(input->title));
    product_ui_utf8_copy_n(value.location, sizeof(value.location),
                           input->location, sizeof(input->location));
    s_time_domain.events[index].used = 1U;
    s_time_domain.events[index].value = value;
    *affected_id = value.id;
    return PRODUCT_UI_TIME_OK;
}

/**
 * @brief 验证能力与原因组合，非可用状态下撤销墙钟值。
 * @param[in] command 调用链保证非空的能力命令。
 * @return 成功返回 TIME_OK；非法组合返回 INVALID_ARGUMENT 且不修改状态。
 */
static product_ui_time_result_e product_ui_time_set_capability(
    const product_ui_time_capability_command_t *command)
{
    if (command->capability == PRODUCT_UI_TIME_CAPABILITY_AVAILABLE) {
        if (command->unavailable_reason != PRODUCT_UI_TIME_UNAVAILABLE_NONE) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
    } else if (command->capability == PRODUCT_UI_TIME_CAPABILITY_UNKNOWN) {
        if ((command->unavailable_reason != PRODUCT_UI_TIME_UNAVAILABLE_NONE) &&
            (command->unavailable_reason != PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED)) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
    } else if (command->capability == PRODUCT_UI_TIME_CAPABILITY_UNAVAILABLE) {
        if ((command->unavailable_reason == PRODUCT_UI_TIME_UNAVAILABLE_NONE) ||
            (command->unavailable_reason == PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED) ||
            (command->unavailable_reason > PRODUCT_UI_TIME_UNAVAILABLE_SOURCE_INVALID)) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
    } else {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }

    s_time_domain.capability = command->capability;
    s_time_domain.unavailable_reason = command->unavailable_reason;
    if (command->capability == PRODUCT_UI_TIME_CAPABILITY_UNKNOWN) {
        s_time_domain.unavailable_reason = PRODUCT_UI_TIME_UNAVAILABLE_NOT_PROBED;
    }
    if (command->capability != PRODUCT_UI_TIME_CAPABILITY_AVAILABLE) {
        s_time_domain.wall_time_valid = 0U;
        memset(&s_time_domain.wall_time, 0, sizeof(s_time_domain.wall_time));
    }
    return PRODUCT_UI_TIME_OK;
}

/**
 * @brief 在可选输出中记录命令返回值、当前代次及受影响编号。
 * @param[out] result 可为空的命令结果。
 * @param[in] status 已确定的执行结果。
 * @param[in] affected_id 本次涉及的记录编号，无对应记录时为零。
 * @return 原样返回 status。
 */
static product_ui_time_result_e product_ui_time_finish_command(
    product_ui_time_domain_command_result_t *result,
    product_ui_time_result_e status,
    uint32_t affected_id)
{
    if (result != NULL) {
        result->result = status;
        result->generation = s_time_domain.generation;
        result->affected_id = affected_id;
    }
    return status;
}

/**
 * @brief 处理重置、能力、墙钟设置及墙钟失效命令。
 * @param[in] command 有效命令指针。
 * @param[out] handled 是否由本分支识别该命令，与成功与否分开。
 * @return 处理结果；非系统命令返回 TIME_OK 并清除 handled。
 */
static product_ui_time_result_e product_ui_time_execute_system(
    const product_ui_time_domain_command_t *command, uint8_t *handled)
{
    product_ui_time_result_e status;
    uint32_t generation;

    *handled = 1U;
    switch (command->type) {
    case PRODUCT_UI_TIME_COMMAND_RESET:
        generation = s_time_domain.generation + 1U;
        product_ui_time_domain_clear(generation == 0U ? 1U : generation);
        return PRODUCT_UI_TIME_OK;
    case PRODUCT_UI_TIME_COMMAND_SET_CAPABILITY:
        return product_ui_time_set_capability(&command->data.capability);
    case PRODUCT_UI_TIME_COMMAND_SET_WALL_TIME:
        if (s_time_domain.capability != PRODUCT_UI_TIME_CAPABILITY_AVAILABLE) {
            return PRODUCT_UI_TIME_ERR_UNAVAILABLE;
        }
        status = product_ui_time_domain_validate_datetime(&command->data.wall_time);
        if (status == PRODUCT_UI_TIME_OK) {
            s_time_domain.wall_time = command->data.wall_time;
            s_time_domain.wall_time_valid = 1U;
        }
        return status;
    case PRODUCT_UI_TIME_COMMAND_INVALIDATE_WALL_TIME:
        s_time_domain.wall_time_valid = 0U;
        memset(&s_time_domain.wall_time, 0, sizeof(s_time_domain.wall_time));
        return PRODUCT_UI_TIME_OK;
    default:
        *handled = 0U;
        return PRODUCT_UI_TIME_OK;
    }
}

/**
 * @brief 将闹钟变更同步交给绑定运行时，域内不保存副本。
 * @param[in] command 有效命令指针。
 * @param[out] affected_id 供后端写入的记录编号。
 * @param[out] handled 命令是否属于闹钟变更。
 * @return 识别且有后端时返回后端结果；缺后端为 UNAVAILABLE；非闹钟命令为 TIME_OK。
 */
static product_ui_time_result_e product_ui_time_execute_alarm(
    const product_ui_time_domain_command_t *command, uint32_t *affected_id,
    uint8_t *handled)
{
    *handled = command->type == PRODUCT_UI_TIME_COMMAND_UPSERT_ALARM ||
        command->type == PRODUCT_UI_TIME_COMMAND_REMOVE_ALARM ||
        command->type == PRODUCT_UI_TIME_COMMAND_SET_ALARM_ENABLED;
    if (!*handled) return PRODUCT_UI_TIME_OK;
    return s_alarm_backend != NULL && s_alarm_backend->execute != NULL ?
        s_alarm_backend->execute(command, affected_id) : PRODUCT_UI_TIME_ERR_UNAVAILABLE;
}

/**
 * @brief 处理日程新增更新、删除或完成位变更。
 * @param[in] command 有效命令指针。
 * @param[out] affected_id 受影响日程编号；失败时不保证为零。
 * @param[out] handled 是否由日程分支识别命令。
 * @return 对应执行结果；非日程命令返回 TIME_OK 并清除 handled。
 */
static product_ui_time_result_e product_ui_time_execute_event(
    const product_ui_time_domain_command_t *command, uint32_t *affected_id,
    uint8_t *handled)
{
    int index;

    *handled = 1U;
    if (command->type == PRODUCT_UI_TIME_COMMAND_UPSERT_EVENT) {
        return product_ui_time_upsert_event(&command->data.event, affected_id);
    }
    if ((command->type != PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT) &&
        (command->type != PRODUCT_UI_TIME_COMMAND_SET_EVENT_COMPLETED)) {
        *handled = 0U;
        return PRODUCT_UI_TIME_OK;
    }
    *affected_id = command->type == PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT ?
                   command->data.id.id : command->data.event_completed.id;
    index = product_ui_time_find_event(*affected_id);
    if ((*affected_id == 0U) || (index < 0)) {
        return PRODUCT_UI_TIME_ERR_NOT_FOUND;
    }
    if (command->type == PRODUCT_UI_TIME_COMMAND_REMOVE_EVENT) {
        memset(&s_time_domain.events[index], 0, sizeof(s_time_domain.events[index]));
        return PRODUCT_UI_TIME_OK;
    }
    if (command->type == PRODUCT_UI_TIME_COMMAND_SET_EVENT_COMPLETED) {
        if (command->data.event_completed.completed > 1U) {
            return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
        }
        s_time_domain.events[index].value.completed =
            command->data.event_completed.completed;
        return PRODUCT_UI_TIME_OK;
    }
    *handled = 0U;
    return PRODUCT_UI_TIME_OK;
}

/**
 * @brief 同步执行一条有限状态变更并输出结果、当前代次及受影响编号。
 */
product_ui_time_result_e product_ui_time_domain_execute(
    const product_ui_time_domain_command_t *command,
    product_ui_time_domain_command_result_t *result)
{
    product_ui_time_result_e status = PRODUCT_UI_TIME_OK;
    uint32_t affected_id = 0U;
    uint8_t handled;
    product_ui_time_domain_ensure_initialized();
    if (command == NULL) {
        return product_ui_time_finish_command(
            result, PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT, 0U);
    }

    status = product_ui_time_execute_system(command, &handled);
    if (!handled) {
        status = product_ui_time_execute_alarm(command, &affected_id, &handled);
    }
    if (!handled) {
        status = product_ui_time_execute_event(command, &affected_id, &handled);
        if (handled && status == PRODUCT_UI_TIME_OK) {
            product_ui_time_domain_mark_events_changed();
        }
    }
    if (!handled) {
        status = PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }

    if (status == PRODUCT_UI_TIME_OK) {
        if (command->type == PRODUCT_UI_TIME_COMMAND_RESET) {
            affected_id = 0U;
        } else {
            product_ui_time_domain_mark_changed();
        }
    }
    return product_ui_time_finish_command(result, status, affected_id);
}

/**
 * @brief 读取不含记录数组的域摘要，并观察闹钟后端代次。
 */
product_ui_time_result_e product_ui_time_domain_get_summary(
    product_ui_time_domain_summary_t *summary)
{
    uint32_t index;
    product_ui_time_alarm_summary_t alarms = {0};

    if (summary == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    memset(summary, 0, sizeof(*summary));
    summary->generation = s_time_domain.generation;
    summary->capability = s_time_domain.capability;
    summary->unavailable_reason = s_time_domain.unavailable_reason;
    summary->wall_time_valid = s_time_domain.wall_time_valid;
    summary->wall_time = s_time_domain.wall_time;
    if (s_alarm_backend != NULL && s_alarm_backend->copy != NULL &&
        s_alarm_backend->copy(NULL, &alarms) == PRODUCT_UI_TIME_OK) {
        summary->alarm_runtime_available = alarms.runtime_available && summary->wall_time_valid &&
            summary->capability == PRODUCT_UI_TIME_CAPABILITY_AVAILABLE;
        summary->alarm_count = alarms.count;
        if (summary->alarm_runtime_available) {
            summary->next_alarm_valid = alarms.next_valid;
            summary->next_alarm_id = alarms.next_id;
            summary->next_alarm_at = alarms.next_at;
        }
        if (s_alarm_generation != alarms.generation) {
            s_alarm_generation = alarms.generation;
            product_ui_time_domain_mark_changed();
            summary->generation = s_time_domain.generation;
        }
    }
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        summary->event_count += s_time_domain.events[index].used ? 1U : 0U;
    }
    return PRODUCT_UI_TIME_OK;
}

/**
 * @brief 从绑定闹钟后端复制指定逻辑下标的记录。
 */
product_ui_time_result_e product_ui_time_domain_get_alarm(
    uint8_t logical_index, product_ui_alarm_t *alarm)
{
    if (alarm == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    return s_alarm_backend != NULL && s_alarm_backend->get != NULL ?
        s_alarm_backend->get(logical_index, alarm) : PRODUCT_UI_TIME_ERR_UNAVAILABLE;
}

/**
 * @brief 读取仅由初始化／RESET 及成功日程变更推进的非零缓存代次。
 */
uint32_t product_ui_time_domain_get_event_revision(void)
{
    product_ui_time_domain_ensure_initialized();
    return s_event_revision;
}

/**
 * @brief 按有效槽顺序复制指定逻辑下标的日程。
 */
product_ui_time_result_e product_ui_time_domain_get_event(
    uint8_t logical_index, product_ui_calendar_event_t *event)
{
    uint8_t current = 0U;
    uint32_t index;

    if (event == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (!s_time_domain.events[index].used) {
            continue;
        }
        if (current == logical_index) {
            *event = s_time_domain.events[index].value;
            return PRODUCT_UI_TIME_OK;
        }
        current++;
    }
    return PRODUCT_UI_TIME_ERR_NOT_FOUND;
}

/**
 * @brief 复制固定容量墙钟、闹钟与日程状态。
 */
product_ui_time_result_e product_ui_time_domain_get_snapshot(
    product_ui_time_domain_snapshot_t *snapshot)
{
    product_ui_time_domain_summary_t summary;
    uint32_t index;
    if (snapshot == NULL) {
        return PRODUCT_UI_TIME_ERR_INVALID_ARGUMENT;
    }
    product_ui_time_domain_ensure_initialized();
    memset(snapshot, 0, sizeof(*snapshot));
    product_ui_time_domain_get_summary(&summary);
    snapshot->generation = summary.generation;
    snapshot->capability = summary.capability;
    snapshot->unavailable_reason = summary.unavailable_reason;
    snapshot->wall_time_valid = summary.wall_time_valid;
    snapshot->alarm_runtime_available = summary.alarm_runtime_available;
    snapshot->wall_time = summary.wall_time;
    snapshot->next_alarm_valid = summary.next_alarm_valid;
    snapshot->next_alarm_id = summary.next_alarm_id;
    snapshot->next_alarm_at = summary.next_alarm_at;

    if (s_alarm_backend != NULL && s_alarm_backend->copy != NULL) {
        product_ui_time_alarm_summary_t alarms = {0};
        if (s_alarm_backend->copy(snapshot->alarms, &alarms) == PRODUCT_UI_TIME_OK) {
            snapshot->alarm_count = alarms.count;
            snapshot->alarm_runtime_available = alarms.runtime_available && snapshot->wall_time_valid &&
                snapshot->capability == PRODUCT_UI_TIME_CAPABILITY_AVAILABLE;
            if (snapshot->alarm_runtime_available) {
                snapshot->next_alarm_valid = alarms.next_valid;
                snapshot->next_alarm_id = alarms.next_id;
                snapshot->next_alarm_at = alarms.next_at;
            }
        }
    }
    for (index = 0U; index < PRODUCT_UI_TIME_DOMAIN_MAX_EVENTS; index++) {
        if (s_time_domain.events[index].used) {
            snapshot->events[snapshot->event_count++] =
                s_time_domain.events[index].value;
        }
    }
    return PRODUCT_UI_TIME_OK;
}
