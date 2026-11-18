/** @file product_ui_alarm_preset_tests.c @brief 空存储证明、首次保存竞态及旧版记录工作者加载。 */
#include "modules/applications/product_ui/features/time/product_ui_alarm_service.h"
#include "modules/applications/product_ui/features/time/product_ui_alarm_port.h"
#include "modules/applications/product_ui/features/time/product_ui_clock.h"
#include "modules/applications/product_ui/features/time/product_ui_clock_port.h"
#include "product_ui_host_alarm.h"
#include "os/os_api.h"
#include "alarm_legacy_fixtures.h"
#include <assert.h>
#include <string.h>
#include "alarm_read_gates.h"
#include <windows.h>
extern int puts(const char *);
static product_ui_alarm_snapshot_t state;
static uint8_t bytes[PRODUCT_UI_ALARM_RECORD_SIZE];
static product_ui_alarm_saved_t decoded;
/**
 * @brief 转发主机时钟端口毫秒值。
 * @return 当前端口毫秒数。
 */
uint32_t timer_get_ms(void) { return product_ui_clock_port_ms(); }

/**
 * @brief 推进十毫秒并让主机工作者运行后刷新闹钟快照。
 */
static void pump(void)
{
    product_ui_host_alarm_advance(10); Sleep(2); product_ui_alarm_get_snapshot(&state);
}
/**
 * @brief 有界等待保存及脏标志清除，超过测试期限断言。
 */
static void await_save(void)
{
    unsigned retry;
    for (retry = 0; retry < 1000; retry++) { pump(); if (!state.dirty && !state.saving) return; }
    assert(!"save timeout");
}
/**
 * @brief 按字符类型配置单槽不存在、不可读、短读、IO、旧版、CRC或未来版本记录。
 * @param[in] slot 零或一的测试槽。
 * @param[in] kind 由命令行矩阵给出的记录／错误类别字符。
 */
static void slot_data(uint8_t slot, char kind)
{
    if (kind == 'n') return;
    if (kind == 'u') { product_ui_host_alarm_read_fault(slot, D_ERR_NOT_READY); return; }
    if (kind == 's') { product_ui_host_alarm_read_fault(slot, PRODUCT_UI_ALARM_RECORD_SIZE - 1); return; }
    if (kind == 'i') { product_ui_host_alarm_read_fault(slot, D_ERR_IO); return; }
    if (kind == 'd') { product_ui_host_alarm_read_fault(slot, -511); return; }
    if (kind == 'o') { product_ui_host_alarm_read_fault(slot, PRODUCT_UI_ALARM_RECORD_SIZE + 1); return; }
    if (kind == 'x') { product_ui_host_alarm_read_fault(slot, -999); return; }
    memcpy(bytes, kind == 'l' ? v2_weekly_stale_date : v2_empty, sizeof(bytes));
    if (kind == 'c') bytes[80] ^= 1;
    if (kind == 'v') bytes[4] = 4;
    assert(product_ui_alarm_storage_write(slot, bytes, sizeof(bytes)) == sizeof(bytes));
}
/**
 * @brief 逐项核验六个默认未启用预置的时间、编号、重复掩码及作息模板。
 */
static void preset_fields(void)
{
    const uint8_t hours[] = {7, 8, 12, 14, 18, 22}, minutes[] = {0, 30, 30, 0, 30, 30};
    unsigned index;
    assert(state.count == 6 && !state.ring_mask && !state.stop_mask);
    for (index = 0; index < 6; index++) {
        product_ui_alarm_record_t record;
        assert(product_ui_alarm_get_at(index, &record) == D_OK);
        assert(record.rule.id == index + 1 && !record.rule.enabled && record.state == PRODUCT_UI_ALARM_IDLE);
        assert(record.rule.hour == hours[index] && record.rule.minute == minutes[index]);
        assert(record.rule.repeat_mask == 62 && !record.rule.alternate_mask);
        assert(record.rule.routine_template == PRODUCT_UI_ALARM_ROUTINE_DOUBLE_REST);
    }
}
/**
 * @brief 在首次保存期间编辑或删除首项，clear 模式循环删完六项。
 * @param[in] mode 借用的 edit、delete 或 clear 场景名。
 */
static void mutate(const char *mode)
{
    product_ui_alarm_record_t record;
    product_ui_alarm_command_t command = {0};
    unsigned count = !strcmp(mode, "clear") ? 6 : 1, index;
    for (index = 0; index < count; index++) {
        assert(product_ui_alarm_get_at(0, &record) == D_OK);
        command.action = !strcmp(mode, "edit") ? PRODUCT_UI_ALARM_EDIT : PRODUCT_UI_ALARM_DELETE;
        command.id = record.rule.id; command.rule = record.rule; command.rule.hour = 9;
        command.expected_revision = record.rule_revision; command.label_kind = record.label_kind;
        assert(product_ui_alarm_submit(&command, NULL, NULL) == D_OK);
    }
}

/**
 * @brief 验证空存储证明、两槽分类、旧版读取、首次保存竞态、失败重试及跨进程恢复。
 * @param[in] argc 必须为三。
 * @param[in] argv 程序名、临时存储路径、已知模式名；slots 模式另在字符串中编码两槽类别。
 * @return 全部断言通过返回零。
 * @note 冻结 V1／V2 输入只送入生产解码器，测试不更改历史载荷或预期。
 */
int main(int argc, char **argv)
{
    const char *mode;
    unsigned retry, initial_writes;
    uint8_t matrix, restore;
    assert(argc == 3); mode = argv[2];
    matrix = !strncmp(mode, "slots-", 6); restore = !strncmp(mode, "restore-", 8);
    assert(product_ui_host_alarm_path(argv[1]) == D_OK);
    if (matrix) { slot_data(0, mode[6]); slot_data(1, mode[7]); }
    else if (!restore) product_ui_host_alarm_fault(!strcmp(mode, "first-fail") ? 1 : 0, strcmp(mode, "first-fail") != 0);
    initial_writes = product_ui_host_alarm_writes();
    product_ui_time_domain_init(); assert(product_ui_alarm_init() == D_OK);
    for (retry = 0; retry < 1000; retry++) {
        pump(); if (state.loaded || (matrix && state.storage_error && retry > 20)) break;
    }
    if (matrix) {
        const alarm_read_gate_t *gate = NULL;
        unsigned row;
        for (row = 0U; row < sizeof(alarm_read_gates)/sizeof(alarm_read_gates[0]); row++)
            if (alarm_read_gates[row].left == mode[6] && alarm_read_gates[row].right == mode[7]) gate = &alarm_read_gates[row];
        assert(gate != NULL);
        assert(!!state.loaded == !gate->incomplete);
        if (gate->protected_record || gate->incomplete) {
            product_ui_alarm_command_t edit = {.action=PRODUCT_UI_ALARM_VOLUME, .volume=37U};
            assert(state.storage_error == (gate->protected_record ? PRODUCT_UI_ALARM_ERR_VERSION : D_ERR_IO));
            assert(state.load_error == state.storage_error);
            assert(product_ui_alarm_submit(&edit,NULL,NULL) == (gate->protected_record ? PRODUCT_UI_ALARM_ERR_VERSION : D_ERR_NOT_READY));
            assert(product_ui_host_alarm_writes() == initial_writes);
        }
        uint8_t fresh = mode[6] == 'n' && mode[7] == 'n';
        uint8_t protected = gate->protected_record;
        unsigned expected = protected ? 0 : mode[6] == 'l' || mode[7] == 'l' ? 1 : 0;
        if (fresh) { preset_fields(); await_save(); assert(state.has_record); }
        else {
            assert(state.count == expected);
            assert(!!state.storage_protected == !!protected);
            for (retry = 0; retry < 20; retry++) pump();
            assert(product_ui_host_alarm_writes() == initial_writes); /* 启动时不得自行迁移或修复写入。 */
        }
        if (gate->incomplete && !gate->protected_record &&
            (mode[6] == 'e' || mode[6] == 'l' || mode[7] == 'e' || mode[7] == 'l')) {
            product_ui_alarm_command_t edit = {.action=PRODUCT_UI_ALARM_VOLUME, .volume=37U};
            product_ui_host_alarm_read_fault(0U,0);
            product_ui_host_alarm_read_fault(1U,0);
            assert(product_ui_alarm_retry_save(NULL) == D_OK);
            for (retry = 0; retry < 1000; retry++) { pump(); if (state.loaded) break; }
            assert(state.loaded && state.load_error == D_OK && state.storage_error == D_OK && state.count == expected);
            assert(product_ui_host_alarm_writes() == initial_writes);
            assert(product_ui_alarm_submit(&edit,NULL,NULL) == D_OK);
            await_save(); assert(state.volume == 37U);
        }
    } else if (restore) {
        if (!strcmp(mode, "restore-clear")) assert(state.count == 0);
        else preset_fields();
        assert(state.has_record && !state.dirty && !product_ui_host_alarm_writes());
    } else {
        preset_fields(); assert(state.dirty && !state.has_record && state.request_id);
        if (!strcmp(mode, "first-fail")) {
            for (retry = 0; retry < 1000; retry++) { pump(); if (state.storage_error == D_ERR_IO) break; }
            assert(state.storage_error == D_ERR_IO && state.dirty && !state.has_record);
            initial_writes = product_ui_host_alarm_writes();
            for (retry = 0; retry < 20; retry++) pump();
            assert(product_ui_host_alarm_writes() == initial_writes);
            product_ui_host_alarm_fault(0, 0); assert(product_ui_alarm_retry_save(NULL) == D_OK);
        } else {
            for (retry = 0; retry < 1000 && !product_ui_host_alarm_entered(); retry++) pump();
            assert(product_ui_host_alarm_entered());
            if (strcmp(mode, "defaults")) mutate(mode);
            product_ui_host_alarm_fault(0, 0);
        }
        await_save();
        assert(state.count == (!strcmp(mode, "clear") ? 0 : !strcmp(mode, "delete") ? 5 : 6));
        if (!strcmp(mode, "edit")) {
            product_ui_alarm_record_t record;
            assert(product_ui_alarm_get_at(0, &record) == D_OK && record.rule.hour == 9);
        }
        assert(state.has_record && state.completed_id == state.request_id);
    }
    /* 冻结的稍后提醒记录也经本测试二进制的生产解码器读取。 */
    { uint32_t sequence;
      assert(product_ui_alarm_decode(v1_snoozed, &decoded, &sequence) == D_OK);
      assert(product_ui_alarm_decode(v2_snoozed, &decoded, &sequence) == D_OK); }
    product_ui_host_alarm_fault(0, 0);
    assert(product_ui_alarm_service_stop(2000) == D_OK);
    product_ui_clock_stop(); assert(product_ui_host_join_workers() == 0);
    puts("PASS preset absence/migration/first-save contract"); return 0;
}
