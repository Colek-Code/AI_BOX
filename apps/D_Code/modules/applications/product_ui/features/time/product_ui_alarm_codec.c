/**
 * @file product_ui_alarm_codec.c
 * @brief 固定 1536 字节小端 A/B 记录，不把结构体 ABI 填充写入 Flash。
 * @author D_Code
 * @date 2026-09-21
 */
#include "product_ui_alarm_model.h"
#include <string.h>

#define PRODUCT_UI_ALARM_HEADER_BYTES 124U
#define PRODUCT_UI_ALARM_ENTRY_BYTES 88U
#define PRODUCT_UI_ALARM_CRC_OFFSET (PRODUCT_UI_ALARM_RECORD_SIZE - 4U)
typedef char product_ui_alarm_layout_check[
    PRODUCT_UI_ALARM_HEADER_BYTES + PRODUCT_UI_ALARM_COUNT * PRODUCT_UI_ALARM_ENTRY_BYTES + 4U ==
    PRODUCT_UI_ALARM_RECORD_SIZE ? 1 : -1];

/**
 * @brief 从有限记录片段读取小端无符号值。
 * @param[in] bytes 至少 size 字节的输入片段。
 * @param[in] size 调用链保证不超过八的字节数。
 * @return 解码的 uint64_t 值。
 */
static uint64_t product_ui_alarm_read_le(const uint8_t *bytes, uint8_t size)
{
    uint64_t value = 0U;
    uint8_t index;
    for (index = 0U; index < size; index++) value |= (uint64_t)bytes[index] << (index * 8U);
    return value;
}

/**
 * @brief 把无符号值低字节按小端写入记录片段。
 * @param[out] bytes 至少 size 字节的输出片段。
 * @param[in] value 待编码无符号值。
 * @param[in] size 调用链保证不超过八的字节数。
 */
static void product_ui_alarm_write_le(uint8_t *bytes, uint64_t value, uint8_t size)
{
    uint8_t index;
    for (index = 0U; index < size; index++) bytes[index] = (uint8_t)(value >> (index * 8U));
}

/**
 * @brief 计算记录 CRC 字段之前的反射 CRC32。
 * @param[in] bytes 至少 RECORD_SIZE 字节的完整记录。
 * @return 多项式 0xedb88320、初始及最终异或 UINT32_MAX 的校验值。
 */
static uint32_t product_ui_alarm_crc(const uint8_t *bytes)
{
    uint32_t crc = UINT32_MAX;
    uint16_t index;
    uint8_t bit;
    for (index = 0U; index < PRODUCT_UI_ALARM_CRC_OFFSET; index++) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; bit++) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return crc ^ UINT32_MAX;
}

/**
 * @brief 以半周期差比较相邻 A/B 保存序号，支持回绕。
 */
uint8_t product_ui_alarm_sequence_after(uint32_t left, uint32_t right)
{
    return left != right && (uint32_t)(left - right) < 0x80000000U;
}

/**
 * @brief 显式编码一个规则及实例的 V3 字段到八十八字节槽。
 * @param[in] entry 已经验证的记录。
 * @param[out] bytes 调用方已清零且至少 ENTRY_BYTES 字节的输出槽。
 */
static void product_ui_alarm_encode_entry(const product_ui_alarm_record_t *entry, uint8_t *bytes)
{
    product_ui_alarm_write_le(bytes, entry->rule.id, 4U);
    product_ui_alarm_write_le(bytes + 4U, entry->rule_revision, 4U);
    bytes[8] = entry->rule.enabled; bytes[9] = (uint8_t)entry->rule.schedule;
    bytes[10] = entry->rule.hour; bytes[11] = entry->rule.minute;
    product_ui_alarm_write_le(bytes + 12U, entry->rule.date.year, 2U);
    bytes[14] = entry->rule.date.month; bytes[15] = entry->rule.date.day;
    bytes[16] = entry->rule.repeat_mask; bytes[17] = entry->label_kind;
    bytes[18] = entry->state; bytes[19] = entry->snooze_count;
    memcpy(bytes + 20U, entry->rule.label, sizeof(entry->rule.label));
    product_ui_alarm_write_le(bytes + 44U, (uint64_t)entry->occurrence_utc, 8U);
    product_ui_alarm_write_le(bytes + 52U, (uint64_t)entry->last_handled_utc, 8U);
    product_ui_alarm_write_le(bytes + 60U, (uint64_t)entry->snooze_utc, 8U);
    product_ui_alarm_write_le(bytes + 68U, (uint64_t)entry->next_regular_utc, 8U);
    product_ui_alarm_write_le(bytes + 76U, entry->instance_generation, 4U);
    bytes[80] = entry->stop_action; bytes[81] = entry->notice;
    bytes[82] = entry->rule.alternate_mask; bytes[83] = entry->rule.routine_template;
}

/**
 * @brief 把已验证保存值编码为 V3 固定小端记录并追加 CRC32。
 */
void product_ui_alarm_encode(const product_ui_alarm_saved_t *saved, uint32_t sequence, uint8_t *record)
{
    uint8_t index;
    if (saved == NULL || record == NULL) return;
    memset(record, 0, PRODUCT_UI_ALARM_RECORD_SIZE);
    memcpy(record, "DALM", 4U);
    record[4] = 3U;
    product_ui_alarm_write_le(record + 6U, PRODUCT_UI_ALARM_RECORD_SIZE, 2U);
    product_ui_alarm_write_le(record + 8U, sequence, 4U);
    product_ui_alarm_write_le(record + 12U, saved->next_id, 4U);
    record[16] = saved->volume;
    record[18] = PRODUCT_UI_ALARM_RING_MS / 60000U;
    record[19] = PRODUCT_UI_ALARM_SNOOZE_MINUTES;
    record[20] = PRODUCT_UI_ALARM_SNOOZE_LIMIT;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        if (!saved->records[index].rule.id) continue;
        record[17]++;
        product_ui_alarm_encode_entry(&saved->records[index],
            record + PRODUCT_UI_ALARM_HEADER_BYTES + index * PRODUCT_UI_ALARM_ENTRY_BYTES);
    }
    product_ui_alarm_write_le(record + PRODUCT_UI_ALARM_CRC_OFFSET, product_ui_alarm_crc(record), 4U);
}

/**
 * @brief 从完整槽解码字段，不在此校验记录或保留字节。
 * @param[in] bytes 至少 ENTRY_BYTES 可读字节。
 * @param[out] entry 接收字段值，上层校验成功后才可消费。
 */
static void product_ui_alarm_decode_entry(const uint8_t *bytes, product_ui_alarm_record_t *entry)
{
    entry->rule.id = (uint32_t)product_ui_alarm_read_le(bytes, 4U);
    entry->rule_revision = (uint32_t)product_ui_alarm_read_le(bytes + 4U, 4U);
    entry->rule.enabled = bytes[8]; entry->rule.schedule = (product_ui_alarm_schedule_e)bytes[9];
    entry->rule.hour = bytes[10]; entry->rule.minute = bytes[11];
    entry->rule.date.year = (uint16_t)product_ui_alarm_read_le(bytes + 12U, 2U);
    entry->rule.date.month = bytes[14]; entry->rule.date.day = bytes[15];
    entry->rule.repeat_mask = bytes[16]; entry->label_kind = bytes[17];
    entry->state = bytes[18]; entry->snooze_count = bytes[19];
    memcpy(entry->rule.label, bytes + 20U, sizeof(entry->rule.label));
    entry->occurrence_utc = (int64_t)product_ui_alarm_read_le(bytes + 44U, 8U);
    entry->last_handled_utc = (int64_t)product_ui_alarm_read_le(bytes + 52U, 8U);
    entry->snooze_utc = (int64_t)product_ui_alarm_read_le(bytes + 60U, 8U);
    entry->next_regular_utc = (int64_t)product_ui_alarm_read_le(bytes + 68U, 8U);
    entry->instance_generation = (uint32_t)product_ui_alarm_read_le(bytes + 76U, 4U);
    entry->stop_action = bytes[80]; entry->notice = bytes[81];
    entry->rule.alternate_mask = bytes[82]; entry->rule.routine_template = bytes[83];
}

/**
 * @brief 检查有限字节区间是否全部为零。
 * @param[in] bytes 至少 length 字节的可读片段。
 * @param[in] length 检查长度。
 * @return 全部为零返回 1，发现任一非零返回 0。
 */
static uint8_t product_ui_alarm_zero(const uint8_t *bytes, uint16_t length)
{
    uint16_t index;
    for (index = 0U; index < length; index++) if (bytes[index] != 0U) return 0U;
    return 1U;
}

/**
 * @brief 验证 V1／V2／V3 记录，并按旧版本合同迁移双周字段及贪睡期限。
 */
d_err_t product_ui_alarm_decode(const uint8_t *record, product_ui_alarm_saved_t *saved, uint32_t *sequence)
{
    uint8_t index, previous, count = 0U;
    uint16_t version;
    if (record == NULL || saved == NULL || sequence == NULL) return D_ERR_INVALID_ARG;
    if (memcmp(record, "DALM", 4U) != 0) return D_ERR_IO;
    version = (uint16_t)product_ui_alarm_read_le(record + 4U, 2U);
    if (version != 1U && version != 2U && version != 3U) return PRODUCT_UI_ALARM_ERR_VERSION;
    if (product_ui_alarm_read_le(record + 6U, 2U) != PRODUCT_UI_ALARM_RECORD_SIZE ||
        product_ui_alarm_read_le(record + PRODUCT_UI_ALARM_CRC_OFFSET, 4U) != product_ui_alarm_crc(record) ||
        record[16] < 1U || record[16] > 100U || record[17] > PRODUCT_UI_ALARM_COUNT ||
        record[18] != PRODUCT_UI_ALARM_RING_MS / 60000U ||
        record[19] != (version == 1U ? 10U : PRODUCT_UI_ALARM_SNOOZE_MINUTES) ||
        record[20] != PRODUCT_UI_ALARM_SNOOZE_LIMIT ||
        !product_ui_alarm_zero(record + 21U, PRODUCT_UI_ALARM_HEADER_BYTES - 21U)) return D_ERR_IO;
    memset(saved, 0, sizeof(*saved));
    saved->next_id = (uint32_t)product_ui_alarm_read_le(record + 12U, 4U);
    saved->volume = record[16];
    if (!saved->next_id) return D_ERR_IO;
    for (index = 0U; index < PRODUCT_UI_ALARM_COUNT; index++) {
        const uint8_t *bytes = record + PRODUCT_UI_ALARM_HEADER_BYTES + index * PRODUCT_UI_ALARM_ENTRY_BYTES;
        product_ui_alarm_record_t *entry = &saved->records[index];
        product_ui_alarm_decode_entry(bytes, entry);
        if (!entry->rule.id) {
            if (!product_ui_alarm_zero(bytes, PRODUCT_UI_ALARM_ENTRY_BYTES)) return D_ERR_IO;
            continue;
        }
        count++;
        if (version < 3U) {
            /* 使用扩展 V3 校验前，先核实旧版本自身格式。 */
            if (entry->rule.schedule > PRODUCT_UI_ALARM_SCHEDULE_WEEKLY ||
                !product_ui_alarm_zero(bytes + 82U, 6U)) return D_ERR_IO;
            entry->rule.alternate_mask = 0U;
            entry->rule.routine_template = entry->rule.schedule == PRODUCT_UI_ALARM_SCHEDULE_WEEKLY &&
                entry->rule.repeat_mask == 62U ? PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST : PRODUCT_UI_ALARM_ROUTINE_BASIC;
        }
        if (!product_ui_alarm_record_valid(entry) || !product_ui_alarm_zero(bytes + 84U, 4U)) return D_ERR_IO;
        if (version == 1U && entry->state == PRODUCT_UI_ALARM_SNOOZED) {
            /* V1 十分钟截止改为同一次已受理贪睡起算的五分钟，
             * 不能每次重启又新增五分钟。 */
            entry->snooze_utc -= (10 - (int64_t)PRODUCT_UI_ALARM_SNOOZE_MINUTES) * 60;
            if (entry->snooze_utc < PRODUCT_UI_CLOCK_MIN_UTC) {
                entry->snooze_utc = 0;
                entry->state = PRODUCT_UI_ALARM_TIME_CONFLICT;
                entry->notice = 1U;
            }
        }
        for (previous = 0U; previous < index; previous++)
            if (saved->records[previous].rule.id == entry->rule.id) return D_ERR_IO;
    }
    if (count != record[17]) return D_ERR_IO;
    *sequence = (uint32_t)product_ui_alarm_read_le(record + 8U, 4U);
    return D_OK;
}
