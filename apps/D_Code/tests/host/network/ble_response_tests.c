/** @file ble_response_tests.c @brief 展开BLE模型的容量、加密失败及身份格式化边界。 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
extern int printf(const char *, ...);
extern int snprintf(char *, size_t, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, va_list);
typedef uint8_t u8;
typedef uint16_t u16;
#define ATT_CHARACTERISTIC_ae82_01_VALUE_HANDLE 1
#define ATT_OP_AUTO_READ_CCC 0
#define log_info(...) ((void)0)
#include "ble_response_cases.inc"

static struct { uint32_t before; char bytes[192]; uint32_t after; } reply;
#define rsp_buf reply.bytes
static unsigned sends, encrypts, mode;
static uint16_t sent_length;
static unsigned char sent[192];
static char uuid_value[194], batch_value[194];
static int uuid_null, batch_null;
static unsigned format_call, fail_format_call;

/**
 * @brief 在指定调用注入格式化失败，其他情况使用真实C格式化器。
 * @param[out] out 至少capacity字节的输出，注入失败时不写。
 * @param[in] capacity 输出容量。
 * @param[in] format 零结尾格式；可变实参按该格式读取。
 * @return 注入失败为-1，其余为vsnprintf实际返回值。
 */
static int format_port(char *out, size_t capacity, const char *format, ...)
{
    va_list args;
    int result;
    if (++format_call == fail_format_call) return -1;
    va_start(args, format); result = vsnprintf(out, capacity, format, args); va_end(args);
    return result;
}

/** @brief 借出有限的合成身份字段。 @return 注入空身份时NULL，否则为本夹具静态字符串。 */
static const char *d_ble_get_product_uuid(void) { return uuid_null ? NULL : uuid_value; }
/** @brief 借出有限的合成批次字段。 @return 注入空批次时NULL，否则为本夹具静态字符串。 */
static const char *d_ble_get_batch_num(void) { return batch_null ? NULL : batch_value; }
/** @brief 返回固定公开测试MAC，供真实格式化器执行。 @param[out] mac 六字节输出。 */
void wifi_get_mac(u8 *mac) { unsigned i; for (i = 0; i < 6U; i++) mac[i] = (u8)i; }

/**
 * @brief 复制调用期负载并计数，不提供无线送达确认。
 * @param[in] handle 须为夹具AE82句柄。
 * @param[in] data 调用期间有效且至少length字节的借用负载。
 * @param[in] length 不得超过192字节。
 * @param[in] kind 须为夹具自动CCC模式。
 * @return 固定零，不代表真实无线写入。
 */
static int app_send_user_data(int handle, const unsigned char *data, uint16_t length, int kind)
{
    assert(handle == 1 && kind == 0 && length <= sizeof(sent));
    memcpy(sent, data, length); sent_length = length; sends++; return 0;
}

#include "ble_default_encrypt.inc"

/**
 * @brief 注入加密返回分区，正常分区调用生产弱实现。
 * @param[in] in 至少length字节的借用输入。
 * @param[in] length 输入字节数。
 * @param[out] out 仅正常或填充分支写入的测试输出。
 * @param[in,out] capacity 须先传入192；无效长度分支故意返回193但不越界写入。
 * @return 注入错误为-7或-1，其他分支为零；由生产调用者判定能否发送。
 */
static int d_ble_encrypt(const uint8_t *in, uint16_t length, uint8_t *out, uint16_t *capacity)
{
    encrypts++;
    assert(*capacity == 192U);
    if (mode == MODE_FAILURE) return -7;
    if (mode == MODE_INVALID_LENGTH) { *capacity = 193U; return 0; }
    if (mode == MODE_PADDED) {
        if (length > *capacity / 2U) return -1;
        memcpy(out, in, length); memcpy(out + length, in, length); *capacity = length * 2U; return 0;
    }
    return default_encrypt(in, length, out, capacity);
}

#define snprintf format_port
#include "ble_response_functions.inc"
#undef snprintf

/** @brief 穷举模型声明的整数域与失败分区，验证生产输出容量和无部分发送。 @return 所有断言通过返回零。 */
int main(void)
{
    static unsigned char input[65539], guarded[256];
    unsigned l, c, n, m, i, copied, expected, checks = 0U;
    uint16_t cap;
    int u, b, result;
    memset(input, 'x', sizeof(input));
    for (l = 0U; l < DIRECT_LENGTH_COUNT; l++) {
        for (c = 0U; c < DIRECT_CAPACITY_COUNT; c++) {
            memset(guarded, 0xa5, sizeof(guarded)); cap = direct_capacities[c];
            result = default_encrypt(input, direct_lengths[l], guarded, &cap);
            copied = direct_lengths[l] <= direct_capacities[c] ? direct_lengths[l] : 0U;
            assert((result == 0) == (direct_lengths[l] <= direct_capacities[c]));
            if (!result) assert(cap == direct_lengths[l]);
            for (i = copied; i < sizeof(guarded); i++) assert(guarded[i] == 0xa5U);
            checks++;
        }
    }
    cap = 192U;
    assert(default_encrypt(NULL, 1U, guarded, &cap) < 0);
    assert(default_encrypt(input, 1U, NULL, &cap) < 0);
    assert(default_encrypt(input, 1U, guarded, NULL) < 0);
    for (n = 0U; n < SEND_LENGTH_COUNT; n++) {
        l = send_lengths[n]; input[l] = 0U;
        for (m = 0U; m < ENCRYPTION_MODE_COUNT; m++) {
            sends = encrypts = 0U; mode = m;
            d_ble_send_json_response((const char *)input);
            expected = l <= 192U && m != MODE_FAILURE && m != MODE_INVALID_LENGTH && (m != MODE_PADDED || l <= 96U);
            assert(sends == expected && encrypts == (l <= 192U));
            if (expected) assert(sent_length == (m == MODE_PADDED ? l * 2U : l));
            checks++;
        }
        input[l] = 'x';
    }
    sends = encrypts = 0U; d_ble_send_json_response(NULL); assert(!sends && !encrypts);
    for (u = IDENTITY_MIN; u <= IDENTITY_MAX; u++) {
        for (b = IDENTITY_MIN; b <= IDENTITY_MAX; b++) {
            memset(uuid_value, 'u', sizeof(uuid_value)); memset(batch_value, 'b', sizeof(batch_value));
            uuid_null = u < 0; batch_null = b < 0;
            uuid_value[u < 0 ? 0 : u] = 0; batch_value[b < 0 ? 0 : b] = 0;
            reply.before = 0x12345678U; reply.after = 0xabcdef12U;
            sends = encrypts = 0U; mode = MODE_PLAIN;
            /* 字段的固定协议开销独立于生产snprintf返回值。 */
            expected = BASE_RESPONSE_LENGTH + (u > 0 ? (unsigned)u + UUID_FIELD_OVERHEAD : 0U) +
                (b > 0 ? (unsigned)b + BATCH_FIELD_OVERHEAD : 0U);
            result = ble_connected_net_notify();
            assert((result == 0) == (expected < 192U));
            assert(sends == (expected < 192U));
            assert(reply.before == 0x12345678U && reply.after == 0xabcdef12U);
            if (sends) assert(sent_length == expected);
            checks++;
        }
    }
    uuid_null = batch_null = 0; uuid_value[0] = 'u'; uuid_value[1] = 0; batch_value[0] = 'b'; batch_value[1] = 0;
    for (n = 0U; n < FORMAT_FAIL_COUNT; n++) {
        fail_format_call = format_fail_calls[n];
        format_call = sends = encrypts = 0U;
        assert(ble_connected_net_notify() == -1);
        assert(!sends && !encrypts);
        checks++;
    }
    printf("PASS BLE response finite contract: %u cases\n", checks);
    return 0;
}
