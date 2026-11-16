/** @file qwen_tool_protocol.c @brief 按实际字节长度校验多模态工具命令并构建结果帧。 */
#include "qwen_local_tool.h"
#include "cJSON.h"
#include "framework/compat/d_stdio_compat.h"
#include <string.h>

/**
 * @brief 限制 JSON 长度与嵌套并拒绝转义或原始零字符。
 * @param[in] text 原始字符段。
 * @param[in] length 字节数。
 * @return 边界检查通过返回非零，不代表完整 JSON 语法有效。
 */
static int tool_json_valid(const char *text, uint32_t length)
{
    uint32_t i, depth = 0U;
    uint8_t quoted = 0U;
    if (!text || !length || length > QWEN_TOOL_JSON_MAX) return 0;
    for (i = 0U; i < length; i++) {
        char c = text[i];
        if (!c) return 0;
        if (quoted && c == '\\') {
            if (++i == length) return 0;
            if (text[i] == 'u' && i + 4U < length && !memcmp(text + i + 1U, "0000", 4U)) return 0;
        } else if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '{' || c == '[')) { if (++depth > 12U) return 0; }
        else if (!quoted && (c == '}' || c == ']')) { if (!depth) return 0; depth--; }
    }
    return !depth && !quoted;
}

/**
 * @brief 递归拒绝所有对象层级的重复字段，包括未使用字段。
 * @param[in] node 已受深度限制的 JSON 节点。
 * @return 全部字段唯一返回非零。
 */
static int tool_unique(const cJSON *node)
{
    const cJSON *a, *b;
    for (a = node ? node->child : NULL; a; a = a->next) {
        if (cJSON_IsObject(node)) {
            if (!a->string) return 0;
            for (b = a->next; b; b = b->next)
                if (b->string && !strcmp(a->string, b->string)) return 0;
        }
        if (!tool_unique(a)) return 0;
    }
    return 1;
}

/**
 * @brief 有界解析完整 JSON 并核对所有字段唯一。
 * @param[in] data 原始字节，不要求零结尾。
 * @param[in] length 有效长度。
 * @return 调用者拥有的新 JSON 树；失败返回 NULL。
 */
static cJSON *tool_parse(const uint8_t *data, uint32_t length)
{
    const char *end = NULL;
    cJSON *root;
    if (!tool_json_valid((const char *)data, length)) return NULL;
    root = cJSON_ParseWithLengthOpts((const char *)data, length, &end, 0);
    if (!root) return NULL;
    while (end < (const char *)data + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) end++;
    if (end != (const char *)data + length || !tool_unique(root)) { cJSON_Delete(root); return NULL; }
    return root;
}

/**
 * @brief 借用大小写精确的 JSON 字段。
 * @param[in] node 父节点。
 * @param[in] name 字段名称。
 * @return 字段借用指针或 NULL。
 */
static const cJSON *field(const cJSON *node, const char *name)
{ return cJSON_GetObjectItemCaseSensitive(node, name); }

/**
 * @brief 完整复制非空 JSON 字符串，不接受截断。
 * @param[in] node JSON 字符串项。
 * @param[out] target 目标缓冲。
 * @param[in] capacity 包含末尾零的容量。
 * @return 复制成功返回 1，否则为 0。
 */
static int copy_string(const cJSON *node, char *target, uint32_t capacity)
{
    uint32_t length;
    if (!cJSON_IsString(node) || !node->valuestring) return 0;
    length = (uint32_t)strlen(node->valuestring);
    if (!length || length >= capacity) return 0;
    memcpy(target, node->valuestring, length + 1U); return 1;
}

/**
 * @brief 把字符串、布尔或受限整数编码为参数标量。
 * @param[in] node 参数值节点。
 * @param[in,out] arg 参数种类及字符串值输出。
 * @return 支持且可完整存储返回 1，否则为 0。
 */
static int argument_value(const cJSON *node, qwen_tool_arg_t *arg)
{
    if (cJSON_IsString(node)) {
        arg->kind = QWEN_TOOL_STRING;
        if (!node->valuestring || strlen(node->valuestring) >= sizeof(arg->value)) return 0;
        strcpy(arg->value, node->valuestring); return 1;
    }
    if (cJSON_IsBool(node)) {
        arg->kind = QWEN_TOOL_BOOLEAN; strcpy(arg->value, cJSON_IsTrue(node) ? "1" : "0"); return 1;
    }
    if (cJSON_IsNumber(node) && node->valuedouble >= -2147483647.0 &&
        node->valuedouble <= 2147483647.0 && node->valuedouble == (double)node->valueint) {
        arg->kind = QWEN_TOOL_INTEGER;
        return snprintf(arg->value, sizeof(arg->value), "%d", node->valueint) > 0;
    }
    return 0;
}

/**
 * @brief 解码参数数组，优先使用一致的规范化标量，并排序参数名。
 * @param[in] params 有界参数数组。
 * @param[in,out] call 已清零的命令输出。
 * @return 全部有效且唯一返回 1，否则为 0。
 * @note 规范化字段无效不回退原始自然语言值；不接受重复参数。
 */
static int decode_args(const cJSON *params, qwen_tool_call_t *call)
{
    const cJSON *item;
    uint8_t i, j;
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) > (int)QWEN_TOOL_ARGS) return 0;
    for (item = params->child; item; item = item->next) {
        qwen_tool_arg_t *arg = &call->args[call->count];
        const cJSON *value = field(item, "value"), *a = field(item, "norm_value"), *b = field(item, "normValue");
        qwen_tool_arg_t other = {0};
        if (!copy_string(field(item, "name"), arg->name, sizeof(arg->name))) return 0;
        if (a && b && (!argument_value(a, arg) || !argument_value(b, &other) ||
            arg->kind != other.kind || strcmp(arg->value, other.value))) return 0;
        /* 云端可能在 value 保留自然语言文本；优先使用已声明的规范化标量，
         * 规范化无效时不回退原始文本。 */
        if (a || b) value = a ? a : b;
        if (!argument_value(value, arg)) return 0;
        for (i = 0U; i < call->count; i++) if (!strcmp(call->args[i].name, arg->name)) return 0;
        call->count++;
    }
    for (i = 1U; i < call->count; i++) for (j = i; j && strcmp(call->args[j-1U].name, call->args[j].name) > 0; j--) {
        qwen_tool_arg_t temporary = call->args[j]; call->args[j] = call->args[j-1U]; call->args[j-1U] = temporary;
    }
    return 1;
}

/**
 * @brief 按精确名称查找有界命令参数；完整合同见 qwen_local_tool.h。
 */
const qwen_tool_arg_t *qwen_tool_argument(const qwen_tool_call_t *call, const char *name)
{
    uint8_t i;
    for (i = 0U; call && i < call->count; i++) if (!strcmp(call->args[i].name, name)) return &call->args[i];
    return NULL;
}

/**
 * @brief 解码已结束 RespondingContent 中唯一匹配前缀的工具命令；完整合同见 qwen_local_tool.h。
 */
d_err_t qwen_tool_decode(const uint8_t *data, uint32_t length, const char *prefix, qwen_tool_envelope_t *out)
{
    cJSON *root = tool_parse(data, length), *commands = NULL;
    const cJSON *output, *list, *item, *selected = NULL;
    d_err_t result = D_ERR_NOT_FOUND;
    if (!out || !prefix || !*prefix || strlen(prefix) >= QWEN_TOOL_NAME) { cJSON_Delete(root); return D_ERR_INVALID_ARG; }
    memset(out, 0, sizeof(*out));
    if (!root) return D_ERR_INVALID_ARG;
    output = field(field(root, "payload"), "output");
    if (!cJSON_IsString(field(output, "event")) || strcmp(field(output, "event")->valuestring, "RespondingContent")) goto done;
    list = field(field(output, "extra_info"), "commands");
    if (!list) goto done;
    if (cJSON_IsString(list) && !list->valuestring[0]) goto done;
    result = D_ERR_INVALID_ARG;
    if (!cJSON_IsString(list)) goto done;
    commands = tool_parse((const uint8_t *)list->valuestring, (uint32_t)strlen(list->valuestring));
    if (!cJSON_IsArray(commands)) goto done;
    result = D_ERR_NOT_FOUND;
    for (item = commands->child; item; item = item->next) {
        const cJSON *name = field(item, "name");
        if (!cJSON_IsString(name) || strncmp(name->valuestring, prefix, strlen(prefix))) continue;
        if (selected) { result = D_ERR_INVALID_ARG; goto done; }
        selected = item;
    }
    if (!selected) goto done;
    result = D_ERR_INVALID_ARG;
    if (!cJSON_IsTrue(field(output, "finished"))) { result = D_ERR_NOT_FOUND; goto done; }
    if (!copy_string(field(selected, "command_request_id"), out->request, sizeof(out->request)) ||
        !copy_string(field(field(root, "header"), "task_id"), out->task, sizeof(out->task)) ||
        !copy_string(field(output, "dialog_id"), out->dialog, sizeof(out->dialog)) ||
        !copy_string(field(output, "round_id"), out->round, sizeof(out->round)) ||
        !copy_string(field(selected, "name"), out->call.name, sizeof(out->call.name)) ||
        !decode_args(field(selected, "params"), &out->call)) goto done;
    result = D_OK;
done:
    cJSON_Delete(commands); cJSON_Delete(root); return result;
}

/**
 * @brief 创建对象子节点并把所有权交给父对象。
 * @param[in,out] parent 有效父对象。
 * @param[in] name 子对象字段名。
 * @return 父对象持有的新子节点；分配或挂接失败返回 NULL。
 */
static cJSON *object_child(cJSON *parent, const char *name)
{
    cJSON *child = cJSON_CreateObject();
    if (!child) return NULL;
    if (!cJSON_AddItemToObject(parent, name, child)) { cJSON_Delete(child); return NULL; }
    return child;
}

/**
 * @brief 判断 JSON 字符串与预期值精确相同。
 * @param[in] node 待检查项。
 * @param[in] text 预期零结尾文本。
 * @return 相同返回非零。
 */
static int string_is(const cJSON *node, const char *text)
{ return cJSON_IsString(node) && node->valuestring && !strcmp(node->valuestring, text); }

/**
 * @brief 从 SDK Start 或服务端 Started、Listening 提取运输身份；完整合同见 qwen_local_tool.h。
 */
int qwen_tool_identity(const uint8_t *data, uint32_t length, uint8_t direction,
    char task[QWEN_TOOL_CONTEXT], char dialog[QWEN_TOOL_CONTEXT])
{
    cJSON *root = tool_parse(data, length);
    const cJSON *payload = field(root, "payload");
    const cJSON *body = field(payload, direction ? "output" : "input");
    int found;
    if (direction) found = string_is(field(body, "event"), "Started") ||
        (string_is(field(body, "event"), "DialogStateChanged") && string_is(field(body, "state"), "Listening"));
    else found = string_is(field(body, "directive"), "Start");
    if (found) found = copy_string(field(field(root, "header"), "task_id"), task, QWEN_TOOL_CONTEXT) &&
        (!direction || copy_string(field(body, "dialog_id"), dialog, QWEN_TOOL_CONTEXT));
    cJSON_Delete(root); return found;
}

/**
 * @brief 只读提取输出身份及文本、命令和结束标记；完整合同见 qwen_local_tool.h。
 */
int qwen_tool_observe(const uint8_t *data, uint32_t length, qwen_tool_observation_t *observation)
{
    cJSON *root;
    if (!observation) return 0;
    root = tool_parse(data, length);
    const cJSON *output = field(field(root, "payload"), "output");
    const cJSON *text = field(output, "text"), *spoken = field(output, "spoken");
    const cJSON *commands = field(field(output, "extra_info"), "commands");
    int found;
    memset(observation, 0, sizeof(*observation));
    found = copy_string(field(field(root, "header"), "task_id"), observation->task, sizeof(observation->task)) &&
        copy_string(field(output, "dialog_id"), observation->dialog, sizeof(observation->dialog));
    if (found) {
        observation->responding = string_is(field(output, "event"), "RespondingContent");
        observation->finished = cJSON_IsTrue(field(output, "finished"));
        observation->text = (cJSON_IsString(text) && text->valuestring[0]) ||
            (cJSON_IsString(spoken) && spoken->valuestring[0]);
        observation->commands = cJSON_IsString(commands) && commands->valuestring[0] &&
            strcmp(commands->valuestring, "[]");
        (void)copy_string(field(output, "round_id"), observation->round, sizeof(observation->round));
        (void)copy_string(field(output, "llm_request_id"), observation->llm, sizeof(observation->llm));
    }
    cJSON_Delete(root);
    return found;
}

/**
 * @brief 把完整 JSON 对象分类为 RequestToRespond 或其他帧；完整合同见 qwen_local_tool.h。
 */
int qwen_tool_is_response_frame(const uint8_t *data, uint32_t length)
{
    cJSON *root = tool_parse(data, length);
    int found;
    /* 解析失败不能证明这是与工具无关的普通 SDK 帧。 */
    if (!root) return -1;
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return -1; }
    found = string_is(field(field(field(root, "payload"), "input"), "directive"), "RequestToRespond");
    cJSON_Delete(root);
    return found;
}

/**
 * @brief 把产品结果嵌入匹配的 SDK RequestToRespond 空文本帧；完整合同见 qwen_local_tool.h。
 */
d_err_t qwen_tool_reply(const qwen_tool_envelope_t *request, const char *result,
    uint8_t *data, uint32_t *length, uint32_t capacity)
{
    cJSON *root, *structured, *payload, *parameters, *biz, *array, *entry, *invoke, *content;
    const cJSON *input, *result_text;
    d_err_t error = D_ERR_INVALID_ARG;
    if (!request || !result || !data || !length || capacity > 16384U ||
        *length >= capacity || strlen(result) >= QWEN_TOOL_RESULT) return error;
    root = tool_parse(data, *length);
    structured = tool_parse((const uint8_t *)result, (uint32_t)strlen(result));
    if (!root || !cJSON_IsObject(structured)) goto done;
    result_text = field(structured, "result_text");
    if (!cJSON_IsString(result_text) || !result_text->valuestring || !result_text->valuestring[0]) goto done;
    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    input = field(payload, "input");
    if (!string_is(field(field(root, "header"), "task_id"), request->task) ||
        !string_is(field(input, "dialog_id"), request->dialog) ||
        !string_is(field(input, "directive"), "RequestToRespond") ||
        !string_is(field(input, "type"), "prompt") || !string_is(field(input, "text"), "") ||
        field(payload, "parameters")) goto done;
    error = D_ERR_NO_MEMORY;
    parameters = object_child(payload, "parameters");
    biz = parameters ? object_child(parameters, "biz_params") : NULL;
    array = biz ? cJSON_AddArrayToObject(biz, "command_results") : NULL;
    entry = array ? cJSON_CreateObject() : NULL;
    if (!entry) goto done;
    if (!cJSON_AddItemToArray(array, entry)) { cJSON_Delete(entry); goto done; }
    if (!cJSON_AddStringToObject(entry, "command_request_id", request->request)) goto done;
    invoke = object_child(entry, "invoke_result");
    content = invoke ? object_child(invoke, "content") : NULL;
    if (!content || !cJSON_AddStringToObject(content, "type", "text") ||
        !cJSON_AddStringToObject(content, "text", result_text->valuestring)) goto done;
    cJSON_DeleteItemFromObjectCaseSensitive(structured, "result_text");
    if (!cJSON_AddItemToObject(invoke, "structuredContent", structured)) goto done;
    structured = NULL; /* SDK 不持有此 JSON 树。 */
    if (!cJSON_PrintPreallocated(root, (char *)data, (int)capacity, 0)) { error = D_ERR_FULL; goto done; }
    *length = (uint32_t)strlen((char *)data);
    error = D_OK;
done:
    cJSON_Delete(structured); cJSON_Delete(root); return error;
}

/**
 * @brief 只读比较固定请求、产品结果与实际发送帧；完整合同见 qwen_local_tool.h。
 */
d_err_t qwen_tool_check_reply(const qwen_tool_envelope_t *request, const char *result,
    const qwen_tool_frame_t *frame, qwen_tool_reply_check_t *check)
{
    cJSON *root = NULL, *expected = NULL;
    const cJSON *payload, *input, *results, *entry, *invoke, *content, *text, *expected_text;
    d_err_t status = D_ERR_INVALID_ARG;
    if (!check) return status;
    memset(check, 0, sizeof(*check));
    if (!request || !result || !frame || !frame->data || frame->length > frame->capacity ||
        strlen(result) >= QWEN_TOOL_RESULT) return status;
    root = tool_parse(frame->data, frame->length);
    expected = tool_parse((const uint8_t *)result, (uint32_t)strlen(result));
    if (!cJSON_IsObject(root) || !cJSON_IsObject(expected)) goto done;
    expected_text = field(expected, "result_text");
    if (!cJSON_IsString(expected_text) || !expected_text->valuestring) goto done;
    payload = field(root, "payload");
    input = field(payload, "input");
    results = field(field(field(payload, "parameters"), "biz_params"), "command_results");
    check->results = cJSON_IsArray(results) ? (uint32_t)cJSON_GetArraySize(results) : 0U;
    entry = cJSON_GetArrayItem(results, 0);
    invoke = field(entry, "invoke_result");
    content = field(invoke, "content");
    text = field(content, "text");
    check->context_match = string_is(field(field(root, "header"), "task_id"), request->task) &&
        string_is(field(input, "dialog_id"), request->dialog) &&
        string_is(field(input, "directive"), "RequestToRespond") &&
        string_is(field(input, "type"), "prompt") && string_is(field(input, "text"), "");
    check->request_match = check->results == 1U &&
        string_is(field(entry, "command_request_id"), request->request);
    check->expected_text_bytes = (uint32_t)strlen(expected_text->valuestring);
    if (cJSON_IsString(text) && text->valuestring) {
        check->text_bytes = (uint32_t)strlen(text->valuestring);
        check->text_match = string_is(field(content, "type"), "text") &&
            !strcmp(text->valuestring, expected_text->valuestring);
    }
    /* 比较全部通用业务字段，不在 Qwen 层引入闹钟业务语义。 */
    cJSON_DeleteItemFromObjectCaseSensitive(expected, "result_text");
    check->structured_match = cJSON_Compare(field(invoke, "structuredContent"), expected, 1) != 0;
    status = D_OK;
done:
    cJSON_Delete(expected); cJSON_Delete(root); return status;
}
