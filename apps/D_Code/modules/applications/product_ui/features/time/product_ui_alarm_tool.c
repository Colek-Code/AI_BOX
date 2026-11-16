/** @file product_ui_alarm_tool.c @brief Qwen worker 串行持有的产品桥接，全部闹钟记录仍由服务拥有。 */
#include "config/app_config.h"
#if D_FEATURE_AI_ALARM
#include "product_ui_alarm_tool.h"
#include "product_ui_clock.h"
#include "product_ui_time_domain_internal.h"
#include "cJSON.h"
#include "framework/compat/d_stdio_compat.h"
#include "framework/d_log.h"
#include <stdarg.h>
#include <string.h>

#define ALARM_RESULT_TEXT_SIZE 3072U
#define ALARM_QUERY_PAGINATION ((d_err_t)-41)
static product_ui_alarm_snapshot_t s_view;
static product_ui_alarm_record_t s_records[PRODUCT_UI_ALARM_COUNT];
/* 产品端口仅由 Qwen worker 串行调用，此缓冲不归 UI 所有。 */
static char s_result_text[ALARM_RESULT_TEXT_SIZE];
static uint32_t s_result_text_length;
static struct {
    uint32_t id[PRODUCT_UI_ALARM_COUNT], revision[PRODUCT_UI_ALARM_COUNT];
    uint32_t pending, retry_seen;
    char result[QWEN_TOOL_RESULT];
    uint8_t ready;
} s_tool;

static const char *const s_names[] = {"local_alarm_query", "local_alarm_create", "local_alarm_update",
    "local_alarm_delete", "local_alarm_set_enabled", "local_alarm_retry_save"};
static int add_record(cJSON *array, const product_ui_alarm_record_t *record);

/**
 * @brief 清空 worker 自有结果文字及已用长度。
 */
static void reset_result_text(void)
{
    s_result_text[0] = '\0';
    s_result_text_length = 0U;
}

/**
 * @brief 向固定结果缓冲格式化追加文字，检查格式化返回长度是否完整。
 * @param[in] format 可信格式串及相应可变参数。
 * @return 完整追加返回 1，格式化失败或空间不足返回 0。
 * @note 失败可能留下截断字节，但不推进已用长度；上层必须放弃本次结果，不可宣称原缓冲完全未改。
 */
static int append_result_text(const char *format, ...)
{
    va_list args;
    uint32_t remaining = sizeof(s_result_text) - s_result_text_length;
    int length;
    va_start(args, format);
    length = vsnprintf(s_result_text + s_result_text_length, remaining, format, args);
    va_end(args);
    if (length < 0 || (uint32_t)length >= remaining) return 0;
    s_result_text_length += (uint32_t)length;
    return 1;
}

/**
 * @brief 把星期掩码追加为简短可读说明。
 * @param[in] mask 周日为 bit0 的七位星期掩码。
 * @return 全部文字追加成功为 1，容量不足为 0。
 */
static int describe_weekdays(uint8_t mask)
{
    static const char *const names[] = {"日", "一", "二", "三", "四", "五", "六"};
    uint8_t day, first = 1U;
    if (mask == 127U) return append_result_text("每天");
    if (mask == 62U) return append_result_text("周一至周五");
    if (!mask) return append_result_text("不响铃");
    for (day = 1U; day <= 7U; day++) {
        if (!(mask & (1U << (day % 7U)))) continue;
        if (!append_result_text("%s%s", first ? "周" : "、", names[day % 7U])) return 0;
        first = 0U;
    }
    return 1;
}

/**
 * @brief 把单条规则身份、时分、使能及计划追加到有限说明文字。
 * @param[in] record 已验证的借用规则记录。
 * @return 全部追加成功为 1，任一容量失败为 0。
 */
static int describe_record(const product_ui_alarm_record_t *record)
{
    const product_ui_alarm_t *rule = &record->rule;
    if (!append_result_text("ID%u/版本%u，标签“%s”，%02u:%02u，%s，",
        (unsigned)rule->id, (unsigned)record->rule_revision, rule->label,
        (unsigned)rule->hour, (unsigned)rule->minute, rule->enabled ? "已开启" : "已关闭")) return 0;
    if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE)
        return append_result_text("%04u-%02u-%02u单次；",
            (unsigned)rule->date.year, (unsigned)rule->date.month, (unsigned)rule->date.day);
    if (rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_WEEKLY)
        return describe_weekdays(rule->repeat_mask) && append_result_text("重复；");
    return append_result_text("双周，第一周起点%04u-%02u-%02u，第一周",
            (unsigned)rule->date.year, (unsigned)rule->date.month, (unsigned)rule->date.day) &&
        describe_weekdays(rule->repeat_mask) && append_result_text("，第二周") &&
        describe_weekdays(rule->alternate_mask) && append_result_text("；");
}

/**
 * @brief 把一个 JSON 值交给数组，失败时删除尚未成功附着的值。
 * @param[in,out] array 调用方拥有的 JSON 数组。
 * @param[in] value 成功时所有权转移给 array，失败时本函数删除，不再由调用方使用。
 * @return 附着成功为 1，值为空或添加失败为 0。
 */
static int array_value(cJSON *array, cJSON *value)
{
    if (!value || !cJSON_AddItemToArray(array, value)) {
        cJSON_Delete(value);
        return 0;
    }
    return 1;
}

/**
 * @brief 把星期掩码编码为一至七字符串并添加到 JSON 数组。
 * @param[in,out] array 接收新字符串的数组。
 * @param[in] mask 星期位掩码。
 * @return 添加成功为 1，分配或附着失败为 0。
 */
static int array_weekdays(cJSON *array, uint8_t mask)
{
    char days[8];
    uint8_t day, length = 0U;
    for (day = 1U; day <= 7U; day++)
        if (mask & (1U << (day % 7U))) days[length++] = (char)('0' + day);
    days[length] = '\0';
    return array_value(array, cJSON_CreateString(days));
}

/**
 * @brief 将一条规则按既定列顺序编码为紧凑查询行。
 * @param[in,out] array 查询行数组，成功附着的部分行由其拥有。
 * @param[in] record 已验证的借用规则记录。
 * @return 完整编码为 1，分配、格式或附着失败为 0。
 * @note 失败可能已附着部分行，上层删除整个结果树；不改协议字段、列顺序或原记录。
 */
static int add_query_row(cJSON *array, const product_ui_alarm_record_t *record)
{
    const product_ui_alarm_t *rule = &record->rule;
    cJSON *row = cJSON_CreateArray(), *repeat;
    char time[8], date[16];
    if (!array_value(array, row)) return 0;
    if (snprintf(time, sizeof(time), "%02u:%02u",
        (unsigned)rule->hour, (unsigned)rule->minute) != 5) return 0;
    if (!array_value(row, cJSON_CreateNumber(rule->id)) ||
        !array_value(row, cJSON_CreateNumber(record->rule_revision)) ||
        !array_value(row, cJSON_CreateString(time)) ||
        !array_value(row, cJSON_CreateBool(rule->enabled)) ||
        !array_value(row, cJSON_CreateString(rule->label))) return 0;
    repeat = cJSON_CreateArray();
    if (!array_value(row, repeat)) return 0;
    if (!array_value(repeat, cJSON_CreateString(rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_ONCE ?
        "once" : rule->schedule == PRODUCT_UI_ALARM_SCHEDULE_WEEKLY ? "weekly" : "biweekly"))) return 0;
    if (rule->schedule != PRODUCT_UI_ALARM_SCHEDULE_WEEKLY) {
        if (snprintf(date, sizeof(date), "%04u-%02u-%02u", (unsigned)rule->date.year,
            (unsigned)rule->date.month, (unsigned)rule->date.day) != 10) return 0;
        if (!array_value(repeat, cJSON_CreateString(date))) return 0;
    }
    if (rule->schedule != PRODUCT_UI_ALARM_SCHEDULE_ONCE && !array_weekdays(repeat, rule->repeat_mask)) return 0;
    return rule->schedule != PRODUCT_UI_ALARM_SCHEDULE_FORTNIGHT || array_weekdays(repeat, rule->alternate_mask);
}

/**
 * @brief 将本地结果状态键映射为静态用户说明。
 * @param[in] status 调用链保证非空的状态键。
 * @return 对应中文文字的借用指针，未列键使用参数不支持说明。
 */
static const char *outcome_text(const char *status)
{
    static const struct { const char *status, *text; } messages[] = {
        {"saved", "操作已保存。"},
        {"applied_not_saved", "内存已变更，但尚未保存，重启可能丢失。"},
        {"saved_then_changed", "原操作曾保存，随后记录已变化，请重新查询。"},
        {"superseded", "原操作已被后续修改替代，请重新查询。"},
        {"already_in_requested_state", "当前状态已符合要求，无需重复修改。"},
        {"query_only", "当前仅开放查询，请在设备屏幕上修改闹钟。"},
        {"conflict_or_pending_save", "记录已变化或仍有保存待处理，请重新查询。"},
        {"not_found", "指定记录或操作已不存在，请重新查询。"},
        {"capacity_full", "闹钟容量或结果容量不足，本次请求未完成。"},
        {"storage_error", "闹钟存储异常，无法完成本次请求。"},
        {"storage_protected", "闹钟存储版本不兼容，无法确认现有记录。"},
        {"not_ready", "设备闹钟服务或所需时间尚未就绪，本次请求未完成。"},
        {"pagination_removed", "查询已改为一次返回全部闹钟，请不带分页参数重新查询。"},
    };
    unsigned i;
    for (i = 0U; i < sizeof(messages) / sizeof(messages[0]); i++)
        if (!strcmp(status, messages[i].status)) return messages[i].text;
    return "参数不完整、不合法或规则暂不支持，本次请求未完成。";
}

/**
 * @brief 精确检查工具名称是否属于本端口静态六项名称表。
 * @param[in] name 调用方保证非空的工具名称。
 * @return 接受为 1，否则为 0。
 */
static int tool_accepts(const char *name)
{
    unsigned i;
    for (i = 0U; i < sizeof(s_names) / sizeof(s_names[0]); i++) if (!strcmp(name, s_names[i])) return 1;
    return 0;
}

/**
 * @brief 检查调用中的每个参数名是否均位于给定白名单。
 * @param[in] call 有界工具调用。
 * @param[in] allowed 参数名白名单。
 * @param[in] count 白名单条目数。
 * @return 全部允许为 1，任一不在列表为 0。
 */
static int allowed_arguments(const qwen_tool_call_t *call, const char *const *allowed, unsigned count)
{
    unsigned i, j;
    for (i = 0U; i < call->count; i++) {
        for (j = 0U; j < count; j++) if (!strcmp(call->args[i].name, allowed[j])) break;
        if (j == count) return 0;
    }
    return 1;
}

/**
 * @brief 把结果树序列化到 worker 固定缓冲，并无条件删除传入树。
 * @param[in] object 本函数接管的 JSON 树，返回后不再有效。
 * @return 完整序列化为 D_OK，空树或缓冲不足为 FULL。
 * @note 仅序列化成功才设置 ready；不向外转移 JSON 节点。
 */
static d_err_t emit(cJSON *object)
{
    int success = object && cJSON_PrintPreallocated(object, s_tool.result, sizeof(s_tool.result), 0);
    cJSON_Delete(object);
    s_tool.ready = success != 0;
    return success ? D_OK : D_ERR_FULL;
}

/**
 * @brief 将本地错误映射为固定工具状态键。
 * @param[in] error 本地闹钟或参数错误。
 * @return 静态协议状态键的借用指针。
 */
static const char *error_name(d_err_t error)
{
    if (error == D_ERR_BUSY) return "conflict_or_pending_save";
    if (error == D_ERR_NOT_FOUND) return "not_found";
    if (error == D_ERR_FULL) return "capacity_full";
    if (error == D_ERR_IO) return "storage_error";
    if (error == PRODUCT_UI_ALARM_ERR_VERSION) return "storage_protected";
    if (error == D_ERR_NOT_READY) return "not_ready";
    if (error == ALARM_QUERY_PAGINATION) return "pagination_removed";
    return "invalid_or_unsupported_arguments";
}

/**
 * @brief 组织已应用／已保存分离的结果，并按需附加当前规则及操作回执。
 * @param[in] status 固定结果状态键。
 * @param[in] success 本次目标是否成功完成。
 * @param[in] applied 是否已经改变本机 RAM 状态。
 * @param[in] persisted 相应变更是否已确认保存。
 * @param[in] receipt 可为空的借用本地操作回执。
 * @return 完整生成为 D_OK；分配失败为 NO_MEMORY，文字或结果缓冲不足为 FULL。
 * @note 中途失败删除自有 JSON 树；所有输出字符串及字段沿用既定工具协议。
 */
static d_err_t outcome(const char *status, uint8_t success, uint8_t applied, uint8_t persisted,
    const product_ui_alarm_receipt_t *receipt)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return D_ERR_NO_MEMORY;
    reset_result_text();
    if (!append_result_text("%s", outcome_text(status)) ||
        (receipt && !append_result_text("operation=%u，ID=%u。", (unsigned)receipt->operation, (unsigned)receipt->id))) {
        cJSON_Delete(root); return D_ERR_FULL;
    }
    if (!cJSON_AddBoolToObject(root, "success", success) || !cJSON_AddStringToObject(root, "status", status) ||
        !cJSON_AddBoolToObject(root, "applied", applied) || !cJSON_AddBoolToObject(root, "persisted", persisted) ||
        (receipt && (!cJSON_AddNumberToObject(root, "operation", receipt->operation) ||
         !cJSON_AddNumberToObject(root, "id", receipt->id) || !cJSON_AddNumberToObject(root, "revision", receipt->rule_revision)))) {
        cJSON_Delete(root); return D_ERR_NO_MEMORY;
    }
    if (receipt && receipt->id) {
        product_ui_alarm_record_t record;
        if (product_ui_alarm_get_record(receipt->id, &record) == D_OK) {
            cJSON *array = cJSON_AddArrayToObject(root, "current");
            if (!array || !add_record(array, &record)) { cJSON_Delete(root); return D_ERR_NO_MEMORY; }
            if (!describe_record(&record)) { cJSON_Delete(root); return D_ERR_FULL; }
        }
    }
    if (!strcmp(status, "already_in_requested_state") && !persisted &&
        !append_result_text("当前记录未保存。")) { cJSON_Delete(root); return D_ERR_FULL; }
    if (!cJSON_AddStringToObject(root, "result_text", s_result_text)) { cJSON_Delete(root); return D_ERR_NO_MEMORY; }
    {
        d_err_t result = emit(root);
        if (result == D_OK) LOGI("ALARM_TOOL", "result status=%s success=%u applied=%u persisted=%u",
            status, (unsigned)success, (unsigned)applied, (unsigned)persisted);
        return result;
    }
}

/**
 * @brief 向数组附加一条完整规则的固定字段对象。
 * @param[in,out] array 调用方拥有的 JSON 数组。
 * @param[in] record 已验证的借用规则记录。
 * @return 全部字段附加成功为非零，分配或附着失败为零。
 * @note 附着后节点由 array 拥有，失败可能留下部分字段供上层整体删除。
 */
static int add_record(cJSON *array, const product_ui_alarm_record_t *record)
{
    cJSON *entry = cJSON_CreateObject();
    const product_ui_alarm_t *rule = &record->rule;
    if (!entry) return 0;
    if (!cJSON_AddItemToArray(array, entry)) { cJSON_Delete(entry); return 0; }
    return cJSON_AddNumberToObject(entry, "id", rule->id) &&
        cJSON_AddNumberToObject(entry, "revision", record->rule_revision) &&
        cJSON_AddBoolToObject(entry, "enabled", rule->enabled) &&
        cJSON_AddNumberToObject(entry, "schedule", rule->schedule) &&
        cJSON_AddNumberToObject(entry, "year", rule->date.year) &&
        cJSON_AddNumberToObject(entry, "month", rule->date.month) &&
        cJSON_AddNumberToObject(entry, "day", rule->date.day) &&
        cJSON_AddNumberToObject(entry, "hour", rule->hour) && cJSON_AddNumberToObject(entry, "minute", rule->minute) &&
        cJSON_AddNumberToObject(entry, "weekday_mask", rule->repeat_mask) &&
        cJSON_AddNumberToObject(entry, "alternate_mask", rule->alternate_mask) &&
        cJSON_AddStringToObject(entry, "label", rule->label);
}

/**
 * @brief 一次拷贝全部规则集合，校验存储及快照版本后生成完整非分页结果。
 * @param[in] call 借用的查询调用，仅接受既定参数。
 * @return 结果完整生成为 D_OK；参数、旧版本、加载／数据／容量错误返回对应值。
 * @note 成功输出后才更新 worker 观察到的规则版本及可重试操作；墙钟另取快照，不宣称与规则集合跨服务原子。
 */
static d_err_t query(const qwen_tool_call_t *call)
{
    static const char *const keys[] = {"cursor", "snapshot_revision"};
    static const char *const columns[] = {"id", "revision", "time", "enabled", "label", "rule"};
    static const char legend[] =
        "以下是本机全部闹钟。rule: once=单次日期; weekly=星期串; biweekly=第一周周一起点、两周星期串。"
        "星期1至7为周一至周日，空串为该周不响铃。enabled为开关。仅依据数据回答，不朗读ID和版本。\n";
    product_ui_alarm_receipt_t receipt;
    product_ui_clock_snapshot_t clock;
    product_ui_datetime_t local;
    cJSON *root = NULL, *details, *array, *names;
    char now[24];
    int32_t cursor = 0, revision = 0;
    uint8_t slot, count = 0U, pending;
    d_err_t result;
    if (!allowed_arguments(call, keys, 2U)) return D_ERR_INVALID_ARG;
    if (qwen_tool_argument(call, "cursor") && !product_ui_alarm_tool_integer(call, "cursor", &cursor)) return D_ERR_INVALID_ARG;
    if (cursor) return ALARM_QUERY_PAGINATION;
    if (qwen_tool_argument(call, "snapshot_revision") &&
        !product_ui_alarm_tool_integer(call, "snapshot_revision", &revision)) return D_ERR_INVALID_ARG;
    product_ui_alarm_copy_set(&s_view, s_records);
    product_ui_clock_get_snapshot(&clock);
    if (!s_view.loaded) return D_ERR_NOT_READY;
    if (s_view.storage_protected) return PRODUCT_UI_ALARM_ERR_VERSION;
    if (s_view.load_error != D_OK && s_view.load_error != D_ERR_NOT_FOUND) return D_ERR_IO;
    if (revision && (uint32_t)revision != s_view.revision) return D_ERR_BUSY;
    product_ui_alarm_get_receipt(&receipt);
    pending = receipt.revision == s_view.revision &&
        (receipt.state == PRODUCT_UI_ALARM_RECEIPT_PENDING || receipt.state == PRODUCT_UI_ALARM_RECEIPT_FAILED);
    details = cJSON_CreateObject();
    if (!details) return D_ERR_NO_MEMORY;
    if (clock.valid && product_ui_time_utc_to_beijing(clock.utc_seconds, &local) != PRODUCT_UI_TIME_OK) clock.valid = 0U;
    if (clock.valid) {
        if (snprintf(now, sizeof(now), "%04u-%02u-%02u %02u:%02u", (unsigned)local.date.year,
            (unsigned)local.date.month, (unsigned)local.date.day, (unsigned)local.time.hour,
            (unsigned)local.time.minute) != 16) { cJSON_Delete(details); return D_ERR_IO; }
        if (!cJSON_AddStringToObject(details, "device_time", now)) goto memory;
    } else if (!cJSON_AddNullToObject(details, "device_time")) goto memory;
    if (!cJSON_AddStringToObject(details, "timezone", "Asia/Shanghai") ||
        !cJSON_AddBoolToObject(details, "clock_valid", clock.valid) ||
        !cJSON_AddBoolToObject(details, "dirty", s_view.dirty) ||
        !cJSON_AddNumberToObject(details, "storage_error", s_view.storage_error)) goto memory;
    if (pending && (!cJSON_AddNumberToObject(details, "operation", receipt.operation) ||
        !cJSON_AddStringToObject(details, "save_state",
            receipt.state == PRODUCT_UI_ALARM_RECEIPT_PENDING ? "pending" : "failed"))) goto memory;
    names = cJSON_CreateStringArray(columns, 6);
    if (!names) goto memory;
    if (!cJSON_AddItemToObject(details, "columns", names)) { cJSON_Delete(names); goto memory; }
    array = cJSON_AddArrayToObject(details, "alarms");
    if (!array) goto memory;
    for (slot = 0U; slot < PRODUCT_UI_ALARM_COUNT; slot++) {
        if (!s_records[slot].rule.id) continue;
        if (!product_ui_alarm_record_valid(&s_records[slot])) { cJSON_Delete(details); return D_ERR_IO; }
        if (!add_query_row(array, &s_records[slot])) goto memory;
        count++;
    }
    if (count != s_view.count) { cJSON_Delete(details); return D_ERR_IO; }
    memcpy(s_result_text, legend, sizeof(legend) - 1U);
    if (!cJSON_PrintPreallocated(details, s_result_text + sizeof(legend) - 1U,
        sizeof(s_result_text) - sizeof(legend) + 1U, 0)) {
        cJSON_Delete(details); return D_ERR_FULL;
    }
    cJSON_Delete(details); details = NULL;
    root = cJSON_CreateObject();
    if (!root || !cJSON_AddBoolToObject(root, "success", 1) || !cJSON_AddBoolToObject(root, "complete", 1) ||
        !cJSON_AddNumberToObject(root, "total_count", count) || !cJSON_AddNumberToObject(root, "returned_count", count) ||
        !cJSON_AddNumberToObject(root, "snapshot_revision", s_view.revision) ||
        !cJSON_AddBoolToObject(root, "mutations_enabled", D_AI_ALARM_MUTATIONS) ||
        !cJSON_AddStringToObject(root, "result_text", s_result_text)) goto memory;
    result = emit(root);
    if (result == D_OK) {
        for (slot = 0U; slot < PRODUCT_UI_ALARM_COUNT; slot++) {
            s_tool.id[slot] = s_records[slot].rule.id;
            s_tool.revision[slot] = s_records[slot].rule_revision;
        }
        s_tool.retry_seen = pending ? receipt.operation : 0U;
        LOGI("ALARM_TOOL", "query status=ok complete=1 total=%u returned=%u snapshot=%u dirty=%u storage=%d bytes=%u",
            (unsigned)count, (unsigned)count,
            (unsigned)s_view.revision, (unsigned)s_view.dirty, (int)s_view.storage_error,
            (unsigned)strlen(s_tool.result));
    }
    return result;
memory:
    cJSON_Delete(details); cJSON_Delete(root); return D_ERR_NO_MEMORY;
}

/**
 * @brief 要求目标编号及规则版本曾被本 worker 查询观察，并复核当前服务版本。
 * @param[in] call 含明确 id／revision 的借用调用。
 * @param[out] record 成功读取当前完整记录，后续版本不符时输出不可作为授权。
 * @return 匹配为 D_OK，参数无效 INVALID_ARG，未观察或已变为 BUSY，目标不存在 NOT_FOUND。
 */
static d_err_t target(const qwen_tool_call_t *call, product_ui_alarm_record_t *record)
{
    int32_t id, revision;
    uint8_t i;
    if (!product_ui_alarm_tool_integer(call, "id", &id) || id < 1 ||
        !product_ui_alarm_tool_integer(call, "revision", &revision) || revision < 1) return D_ERR_INVALID_ARG;
    for (i = 0U; i < PRODUCT_UI_ALARM_COUNT; i++)
        if (s_tool.id[i] == (uint32_t)id && s_tool.revision[i] == (uint32_t)revision) break;
    if (i == PRODUCT_UI_ALARM_COUNT) return D_ERR_BUSY;
    if (product_ui_alarm_get_record((uint32_t)id, record) != D_OK) return D_ERR_NOT_FOUND;
    return record->rule_revision == (uint32_t)revision ? D_OK : D_ERR_BUSY;
}

/**
 * @brief 在写操作开关允许时验证显式参数及查询身份，提交精确规则跟踪或重试。
 * @param[in] call 借用的新建、更新、删除、开关或重试调用。
 * @return 受理为 D_OK，其余返回参数、冲突、未就绪或服务错误；只读开关时生成 query_only 结果。
 * @note 编辑保持已保存单次日期；只有成功跟踪受理才设置 worker pending 操作，持久化仍须 poll。
 */
static d_err_t mutate(const qwen_tool_call_t *call)
{
    static const char *const rule_keys[] = {"id","revision","schedule","hour","minute","date_mode","date","day_offset","delay_minutes","weekdays","label"};
    static const char *const target_keys[] = {"id","revision","enabled"};
    product_ui_alarm_command_t command = {0};
    product_ui_alarm_record_t record;
    product_ui_alarm_receipt_t receipt;
    product_ui_clock_snapshot_t clock;
    d_err_t result;
    uint32_t expected_clock = 0U;
    uint8_t create = !strcmp(call->name, s_names[1]), enabled;
    if (!D_AI_ALARM_MUTATIONS) return outcome("query_only", 0U, 0U, 0U, NULL);
    if (!strcmp(call->name, s_names[5])) {
        int32_t operation;
        if (call->count != 1U || !product_ui_alarm_tool_integer(call, "operation", &operation) ||
            !operation || (uint32_t)operation != s_tool.retry_seen) return D_ERR_INVALID_ARG;
        result = product_ui_alarm_retry_tracked((uint32_t)operation);
        if (result == D_OK) s_tool.pending = (uint32_t)operation;
        return result;
    }
    if (!create && (result = target(call, &record)) != D_OK) return result;
    product_ui_clock_get_snapshot(&clock);
    if (create || !strcmp(call->name, s_names[2])) {
        if (!allowed_arguments(call, rule_keys, sizeof(rule_keys)/sizeof(rule_keys[0])) ||
            (create && (qwen_tool_argument(call, "id") || qwen_tool_argument(call, "revision")))) return D_ERR_INVALID_ARG;
        result = product_ui_alarm_tool_rule(call, &clock, create ? NULL : &record, &command);
        if (result != D_OK) return result;
        expected_clock = clock.revision;
    } else {
        if (!allowed_arguments(call, target_keys, !strcmp(call->name, s_names[3]) ? 2U : 3U)) return D_ERR_INVALID_ARG;
        command.id = record.rule.id; command.expected_revision = record.rule_revision;
        command.action = PRODUCT_UI_ALARM_DELETE;
        if (!strcmp(call->name, s_names[4])) {
            if (!product_ui_alarm_tool_boolean(call, "enabled", &enabled)) return D_ERR_INVALID_ARG;
            if (enabled && !clock.valid) return D_ERR_NOT_READY;
            if (enabled) {
                product_ui_alarm_t candidate = record.rule;
                candidate.enabled = 1U;
                if (!product_ui_alarm_next_occurrence(&candidate, clock.utc_seconds)) return D_ERR_INVALID_ARG;
            }
            if (enabled == record.rule.enabled) {
                product_ui_alarm_get_snapshot(&s_view);
                return outcome("already_in_requested_state", 1U, 0U, !s_view.dirty, NULL);
            }
            /* EDIT 保留已存日期；旧 UI 的 ENABLE 路径按其合同重新定位单次日期。 */
            command.action = PRODUCT_UI_ALARM_EDIT; command.rule = record.rule;
            command.rule.enabled = enabled; command.label_kind = record.label_kind;
            if (enabled) expected_clock = clock.revision;
            else { command.action = PRODUCT_UI_ALARM_ENABLE; command.enabled = 0U; }
        }
    }
    result = product_ui_alarm_submit_tracked(&command, expected_clock, &receipt);
    if (result == D_OK) { s_tool.pending = receipt.operation; s_tool.retry_seen = receipt.operation; }
    return result;
}

/**
 * @brief 清除本端口上次响应状态，执行查询或受理变更，错误转为结构化结果。
 * @param[in] call 调用方已通过 accepts 选择的非空工具调用。
 * @return 结果准备或变更受理成功为 D_OK，其余返回结果编码错误。
 */
static d_err_t tool_start(const qwen_tool_call_t *call)
{
    d_err_t result;
    s_tool.ready = 0U; s_tool.pending = 0U;
    result = !strcmp(call->name, s_names[0]) ? query(call) : mutate(call);
    if (result != D_OK) return outcome(error_name(result), 0U, 0U, 0U, NULL);
    return D_OK;
}

/**
 * @brief 观察匹配本地操作回执，分清保存、未保存及后续取代，再复制有限响应。
 * @param[out] result 非空调用方缓冲，成功时复制 NUL 结尾结果。
 * @param[in] capacity 输出总容量，必须大于完整结果字节长度。
 * @return 尚待完成为 BUSY，输出容量不足为 FULL，成功复制为 D_OK；结果生成失败映射 NO_MEMORY。
 * @note 已保存后规则又变化会单独报告 saved_then_changed；不把仅受理或 RAM 已应用说成已保存。
 */
static d_err_t tool_poll(char *result, uint32_t capacity)
{
    if (s_tool.pending) {
        product_ui_alarm_receipt_t receipt;
        product_ui_alarm_get_receipt(&receipt);
        if (receipt.operation != s_tool.pending) return D_ERR_BUSY;
        if (receipt.state == PRODUCT_UI_ALARM_RECEIPT_PENDING) return D_ERR_BUSY;
        if (receipt.state == PRODUCT_UI_ALARM_RECEIPT_SAVED && receipt.rule_revision) {
            product_ui_alarm_record_t current;
            if (product_ui_alarm_get_record(receipt.id, &current) != D_OK || current.rule_revision != receipt.rule_revision) {
                if (outcome("saved_then_changed", 0U, 1U, 1U, &receipt) != D_OK) return D_ERR_NO_MEMORY;
                s_tool.pending = 0U;
            }
        }
        if (s_tool.pending) {
            if (outcome(receipt.state == PRODUCT_UI_ALARM_RECEIPT_SAVED ? "saved" :
                receipt.state == PRODUCT_UI_ALARM_RECEIPT_FAILED ? "applied_not_saved" : "superseded",
                receipt.state == PRODUCT_UI_ALARM_RECEIPT_SAVED, 1U,
                receipt.state == PRODUCT_UI_ALARM_RECEIPT_SAVED, &receipt) != D_OK) return D_ERR_NO_MEMORY;
            s_tool.pending = 0U;
        }
    }
    if (!s_tool.ready) return D_ERR_BUSY;
    if (strlen(s_tool.result) >= capacity) return D_ERR_FULL;
    strcpy(result, s_tool.result); return D_OK;
}

/**
 * @brief 清空本端口 worker 的结果和观察身份，不撤销服务已受理的 RAM／保存变更。
 */
static void tool_cancel(void) { memset(&s_tool, 0, sizeof(s_tool)); }
static const qwen_local_tool_port_t s_port = {
    .prefix = "local_alarm_", .accepts = tool_accepts, .start = tool_start, .poll = tool_poll, .cancel = tool_cancel,
};
/**
 * @brief 借用 AI 闹钟功能下的静态本地工具端口描述。
 */
const qwen_local_tool_port_t *product_ui_alarm_tool_port(void) { return &s_port; }
#endif
