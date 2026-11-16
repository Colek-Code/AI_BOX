/** @file qwen_wire_diag.c @brief 有界且仅观察的 Qwen 收发字段诊断。 */
#include "config/app_config.h"
#if D_FEATURE_QWEN && D_QWEN_WSS_DIAG_ENABLE
#include "qwen_wire_diag.h"
#include "framework/d_log.h"
#include "cJSON.h"
#include <string.h>

#define DIAG_JSON_LIMIT 16384U
#define DIAG_TEXT_LIMIT 2048U
#define DIAG_PART_BYTES 96U
#define DIAG_ID_BYTES 128U

/* RX 与 TX 计数各由单个线程持有，不跨线程传递缓冲。 */
static uint32_t s_rx_sequence, s_tx_sequence;
static struct {
    char task[DIAG_ID_BYTES], dialog[DIAG_ID_BYTES];
    char round[DIAG_ID_BYTES], llm[DIAG_ID_BYTES];
    uint32_t chunks, last_text, last_spoken, max_text, max_spoken;
} s_rx_stream;

/**
 * @brief 借用大小写精确的 JSON 对象字段。
 * @param[in] parent 借用父对象。
 * @param[in] name 字段名。
 * @return 找到的借用项或 NULL。
 */
static const cJSON *item(const cJSON *parent, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(parent, name);
}

/**
 * @brief 把缺失或非字符串项视为空显示值。
 * @param[in] value 待读取项。
 * @return JSON 借用字符串或静态空串。
 */
static const char *string_value(const cJSON *value)
{
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

/**
 * @brief 区分 finished 缺失、类型错误与布尔真假。
 * @param[in] value finished 项。
 * @return 缺失返回 -1，非布尔返回 -2，布尔返回 0 或 1。
 */
static int finished_value(const cJSON *value)
{
    return !value ? -1 : !cJSON_IsBool(value) ? -2 : cJSON_IsTrue(value) != 0;
}

/**
 * @brief 在字节与嵌套上限内解析完整诊断 JSON 对象。
 * @param[in] data 原始字节，不要求末尾零。
 * @param[in] length 原始长度，不超过 DIAG_JSON_LIMIT。
 * @return 新建 JSON 根对象，失败返回 NULL；成功后由调用者 cJSON_Delete。
 * @note 拒绝内嵌零与过深 JSON，不改变原帧或 SDK 是否解析的决定。
 */
static cJSON *parse(const uint8_t *data, uint32_t length)
{
    uint32_t i, depth = 0U;
    uint8_t quoted = 0U;
    const char *end = NULL;
    cJSON *root;
    if (!data || !length || length > DIAG_JSON_LIMIT) return NULL;
    for (i = 0U; i < length; i++) {
        uint8_t c = data[i];
        if (!c) return NULL;
        if (quoted && c == '\\') { if (++i == length) return NULL; }
        else if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '{' || c == '[')) { if (++depth > 12U) return NULL; }
        else if (!quoted && (c == '}' || c == ']')) { if (!depth) return NULL; depth--; }
    }
    if (depth || quoted) return NULL;
    root = cJSON_ParseWithLengthOpts((const char *)data, length, &end, 0);
    if (!root) return NULL;
    while (end < (const char *)data + length && strchr(" \t\r\n", *end)) end++;
    if (end != (const char *)data + length || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/**
 * @brief 按字段上限分段转义输出，并显式记录类型、存在性与截断。
 * @param[in] direction RX 或 TX 诊断方向。
 * @param[in] sequence 当前收发序列号。
 * @param[in] name 字段诊断名称。
 * @param[in] value 借用 JSON 项，允许为空。
 * @note 分段保持 UTF-8 续字节与首字节相邻，控制字符转义。
 */
static void print_field(const char *direction, uint32_t sequence, const char *name, const cJSON *value)
{
    static const char hex[] = "0123456789abcdef";
    const char *text = string_value(value);
    uint32_t length = (uint32_t)strlen(text), offset = 0U;
    uint32_t cap = (!strcmp(name, "text") || !strcmp(name, "spoken")) ? DIAG_TEXT_LIMIT : DIAG_ID_BYTES;
    uint32_t limit = length < cap ? length : cap;
    LOGI("QWEN_WIRE", "%s n=%u %s present=%u string=%u bytes=%u truncated=%u",
        direction, (unsigned)sequence, name, value != NULL, cJSON_IsString(value) != 0,
        (unsigned)length, length > limit);
    while (offset < limit) {
        char part[DIAG_PART_BYTES * 6U + 1U];
        uint32_t end = offset + DIAG_PART_BYTES, cursor = 0U, i;
        if (end > limit) end = limit;
        while (end > offset && end < length && ((uint8_t)text[end] & 0xc0U) == 0x80U) end--;
        if (end == offset) break;
        for (i = offset; i < end; i++) {
            uint8_t c = (uint8_t)text[i];
            if (c < 32U || c == 127U) {
                part[cursor++] = '\\'; part[cursor++] = 'u';
                part[cursor++] = '0'; part[cursor++] = '0';
                part[cursor++] = hex[c >> 4]; part[cursor++] = hex[c & 15U];
            } else {
                if (c == '\\' || c == '"') part[cursor++] = '\\';
                part[cursor++] = (char)c;
            }
        }
        part[cursor] = '\0';
        LOGI("QWEN_WIRE", "%s n=%u %s offset=%u value=\"%s\"",
            direction, (unsigned)sequence, name, (unsigned)offset, part);
        offset = end;
    }
}

/**
 * @brief 记录 task、dialog、round 与 llm 身份字段。
 * @param[in] direction 收发方向。
 * @param[in] sequence 当前诊断序列号。
 * @param[in] root JSON 根对象。
 * @param[in] body 业务输入或输出对象。
 */
static void print_identity(const char *direction, uint32_t sequence, const cJSON *root, const cJSON *body)
{
    print_field(direction, sequence, "task", item(item(root, "header"), "task_id"));
    print_field(direction, sequence, "dialog", item(body, "dialog_id"));
    print_field(direction, sequence, "round", item(body, "round_id"));
    print_field(direction, sequence, "llm", item(body, "llm_request_id"));
}

/**
 * @brief 比较并更新固定容量身份缓存。
 * @param[in,out] saved 至少 DIAG_ID_BYTES 字节的缓存。
 * @param[in] value 新 JSON 身份项。
 * @return 新值可容纳且与旧值相同返回非零，否则为 0。
 * @note 超长身份清空缓存，不保留截断前缀。
 */
static int same_identity(char *saved, const cJSON *value)
{
    const char *text = string_value(value);
    uint32_t length = (uint32_t)strlen(text);
    int same = length < DIAG_ID_BYTES && !strcmp(saved, text);
    if (length < DIAG_ID_BYTES) memcpy(saved, text, length + 1U);
    else saved[0] = '\0';
    return same;
}

/**
 * @brief 按完整身份累计文本片段长度，仅首片及结束状态输出摘要。
 * @param[in] sequence 本次 RX 序列号。
 * @param[in] root 完整 JSON 对象。
 * @param[in] body 输出内容对象。
 * @note 避免每个 token 打印累计文本而延迟串口后的 SDK 音频处理。
 */
static void observe_content(uint32_t sequence, const cJSON *root, const cJSON *body)
{
    const cJSON *text = item(body, "text"), *spoken = item(body, "spoken");
    uint32_t text_length = (uint32_t)strlen(string_value(text));
    uint32_t spoken_length = (uint32_t)strlen(string_value(spoken));
    int same = same_identity(s_rx_stream.task, item(item(root, "header"), "task_id"));
    int finished = finished_value(item(body, "finished"));
    same &= same_identity(s_rx_stream.dialog, item(body, "dialog_id"));
    same &= same_identity(s_rx_stream.round, item(body, "round_id"));
    same &= same_identity(s_rx_stream.llm, item(body, "llm_request_id"));
    if (!same) {
        s_rx_stream.chunks = s_rx_stream.last_text = s_rx_stream.last_spoken = 0U;
        s_rx_stream.max_text = s_rx_stream.max_spoken = 0U;
    }
    s_rx_stream.chunks++;
    /* 仅记录首片与结束片，避免每个 token 打印不断增长的累计文本，
     * 经串口拖慢 SDK 音频处理。 */
    if (s_rx_stream.chunks == 1U || finished != 0) {
        LOGI("QWEN_WIRE", "RX n=%u event=RespondingContent finished=%d chunks=%u text_bytes=%u spoken_bytes=%u prev=%u/%u max=%u/%u",
            (unsigned)sequence, finished, (unsigned)s_rx_stream.chunks,
            (unsigned)text_length, (unsigned)spoken_length,
            (unsigned)s_rx_stream.last_text, (unsigned)s_rx_stream.last_spoken,
            (unsigned)s_rx_stream.max_text, (unsigned)s_rx_stream.max_spoken);
        print_identity("RX", sequence, root, body);
    }
    if (finished != 0) {
        /* 明确记录缺失或类型，不虚构默认结束原因。 */
        print_field("RX", sequence, "finish_reason", item(body, "finish_reason"));
        print_field("RX", sequence, "text", text);
        print_field("RX", sequence, "spoken", spoken);
    }
    s_rx_stream.last_text = text_length;
    s_rx_stream.last_spoken = spoken_length;
    if (text_length > s_rx_stream.max_text) s_rx_stream.max_text = text_length;
    if (spoken_length > s_rx_stream.max_spoken) s_rx_stream.max_spoken = spoken_length;
}

/**
 * @brief 在 SDK 解析前只读观察收到的 WSS 文本字段；完整合同见 qwen_wire_diag.h。
 */
void qwen_wire_diag_rx(const uint8_t *data, uint32_t length)
{
    cJSON *root = parse(data, length);
    const cJSON *body = item(item(root, "payload"), "output");
    const char *event = string_value(item(body, "event"));
    uint32_t sequence = ++s_rx_sequence;
    if (!root) {
        LOGW("QWEN_WIRE", "RX n=%u unavailable bytes=%u; SDK input unchanged", (unsigned)sequence, (unsigned)length);
        return;
    }
    if (!strcmp(event, "RespondingContent")) observe_content(sequence, root, body);
    else if (!strcmp(event, "RespondingStarted") || !strcmp(event, "RespondingEnded") ||
             !strcmp(event, "SpeechStarted") || !strcmp(event, "SpeechEnded") ||
             !strcmp(event, "DialogStateChanged") || !strcmp(event, "RequestAccepted")) {
        LOGI("QWEN_WIRE", "RX n=%u bytes=%u finished=%d", (unsigned)sequence, (unsigned)length,
            finished_value(item(body, "finished")));
        print_field("RX", sequence, "event", item(body, "event"));
        print_field("RX", sequence, "state", item(body, "state"));
        print_identity("RX", sequence, root, body);
    }
    cJSON_Delete(root);
}

/**
 * @brief 工具解码后把请求编号关联到紧邻的 RX 诊断序列；完整合同见 qwen_wire_diag.h。
 */
void qwen_wire_diag_tool_request(const char *request_id)
{
    /* 只在当前 RX 帧内借用已解码编号，不分配或共享该内容。 */
    cJSON value = {0};
    value.type = cJSON_String;
    value.valuestring = (char *)request_id;
    print_field("RX", s_rx_sequence, "command_request_id", request_id ? &value : NULL);
}

/**
 * @brief 套接字返回后记录发送的选定字段和结果请求编号；完整合同见 qwen_wire_diag.h。
 */
void qwen_wire_diag_tx(const uint8_t *data, uint32_t length, int sent, const char *owner)
{
    cJSON *root = parse(data, length);
    const cJSON *body = item(item(root, "payload"), "input");
    const cJSON *results = item(item(item(item(root, "payload"), "parameters"), "biz_params"), "command_results");
    uint32_t sequence = ++s_tx_sequence;
    if (!root) {
        LOGW("QWEN_WIRE", "TX n=%u unavailable bytes=%u sent=%d", (unsigned)sequence, (unsigned)length, sent);
        return;
    }
    LOGI("QWEN_WIRE", "TX n=%u owner=%s bytes=%u sent=%d text_bytes=%u results=%d",
        (unsigned)sequence, owner, (unsigned)length, sent,
        (unsigned)strlen(string_value(item(body, "text"))), cJSON_IsArray(results) ? cJSON_GetArraySize(results) : 0);
    print_field("TX", sequence, "directive", item(body, "directive"));
    print_field("TX", sequence, "type", item(body, "type"));
    print_identity("TX", sequence, root, body);
    if (cJSON_IsArray(results)) {
        const cJSON *entry = cJSON_GetArrayItem(results, 0);
        print_field("TX", sequence, "command_request_id", item(entry, "command_request_id"));
    }
    cJSON_Delete(root);
}
#endif
