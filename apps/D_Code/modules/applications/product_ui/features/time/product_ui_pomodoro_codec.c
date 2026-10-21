/** @file product_ui_pomodoro_codec.c @brief Explicit A/B record encoding, independent of target ABI. */
#include "product_ui_pomodoro_model.h"
#include <string.h>

static uint32_t product_ui_pomodoro_read32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void product_ui_pomodoro_write32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t product_ui_pomodoro_crc(const uint8_t *bytes)
{
    uint32_t crc = UINT32_MAX;
    unsigned index;
    unsigned bit;
    for (index = 0U; index < PRODUCT_UI_POMODORO_RECORD_SIZE - 4U; index++) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; bit++) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return crc ^ UINT32_MAX;
}

uint8_t product_ui_pomodoro_sequence_after(uint32_t left, uint32_t right)
{
    return left != right && (uint32_t)(left - right) < 0x80000000U;
}

void product_ui_pomodoro_encode(const product_ui_pomodoro_saved_t *saved,
    uint32_t sequence, uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE])
{
    unsigned index;
    if (saved == NULL || record == NULL) return;
    memset(record, 0, PRODUCT_UI_POMODORO_RECORD_SIZE);
    memcpy(record, "POMO", 4U);
    record[4] = 1U;
    record[6] = PRODUCT_UI_POMODORO_RECORD_SIZE;
    product_ui_pomodoro_write32(record + 8U, sequence);
    record[12] = saved->settings.focus_minutes;
    record[13] = saved->settings.short_minutes;
    record[14] = saved->settings.long_minutes;
    record[15] = saved->settings.interval;
    record[16] = saved->settings.auto_focus;
    record[17] = saved->settings.auto_break;
    record[18] = saved->settings.sound;
    for (index = 0U; index < PRODUCT_UI_POMODORO_DAYS; index++) {
        uint8_t *entry = record + 20U + index * 12U;
        product_ui_pomodoro_write32(entry, saved->days[index].date);
        product_ui_pomodoro_write32(entry + 4U, saved->days[index].count);
        product_ui_pomodoro_write32(entry + 8U, saved->days[index].minutes);
    }
    product_ui_pomodoro_write32(record + 104U, saved->undated_count);
    product_ui_pomodoro_write32(record + 108U, saved->undated_minutes);
    product_ui_pomodoro_write32(record + 124U, product_ui_pomodoro_crc(record));
}

d_err_t product_ui_pomodoro_decode(const uint8_t record[PRODUCT_UI_POMODORO_RECORD_SIZE],
    product_ui_pomodoro_saved_t *saved, uint32_t *sequence)
{
    product_ui_pomodoro_saved_t decoded;
    unsigned index;
    unsigned previous;
    if (record == NULL || saved == NULL || sequence == NULL) return D_ERR_INVALID_ARG;
    if (memcmp(record, "POMO", 4U) != 0 || record[4] != 1U || record[5] != 0U ||
        record[6] != PRODUCT_UI_POMODORO_RECORD_SIZE || record[7] != 0U || record[19] != 0U ||
        product_ui_pomodoro_read32(record + 124U) != product_ui_pomodoro_crc(record)) return D_ERR_IO;
    for (index = 112U; index < 124U; index++) if (record[index] != 0U) return D_ERR_IO;
    memset(&decoded, 0, sizeof(decoded));
    decoded.settings = (product_ui_pomodoro_settings_t){record[12], record[13], record[14],
        record[15], record[16], record[17], record[18]};
    if (!product_ui_pomodoro_settings_valid(&decoded.settings)) return D_ERR_IO;
    for (index = 0U; index < PRODUCT_UI_POMODORO_DAYS; index++) {
        const uint8_t *entry = record + 20U + index * 12U;
        product_ui_pomodoro_day_t *day = &decoded.days[index];
        day->date = product_ui_pomodoro_read32(entry);
        day->count = product_ui_pomodoro_read32(entry + 4U);
        day->minutes = product_ui_pomodoro_read32(entry + 8U);
        if (day->date == 0U) {
            if (day->count != 0U || day->minutes != 0U) return D_ERR_IO;
        } else {
            if (!product_ui_pomodoro_date_valid(day->date) || day->count == 0U || day->minutes < day->count)
                return D_ERR_IO;
            for (previous = 0U; previous < index; previous++)
                if (decoded.days[previous].date == day->date) return D_ERR_IO;
        }
    }
    decoded.undated_count = product_ui_pomodoro_read32(record + 104U);
    decoded.undated_minutes = product_ui_pomodoro_read32(record + 108U);
    if (decoded.undated_minutes < decoded.undated_count) return D_ERR_IO;
    *saved = decoded;
    *sequence = product_ui_pomodoro_read32(record + 8U);
    return D_OK;
}
