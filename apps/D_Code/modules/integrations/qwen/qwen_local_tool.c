/** @file qwen_local_tool.c @brief 单工作任务维护工具历史，发送与接收按有界所有权交接。 */
#include "config/app_config.h"
#if D_FEATURE_AI_ALARM
#include "qwen_local_tool.h"
#include "system/spinlock.h"
#include "framework/d_log.h"
#if D_QWEN_WSS_DIAG_ENABLE
#include "qwen_wire_diag.h"
#endif
#include <string.h>
#define TOOL_TIMEOUT_MS 15000U
typedef enum { TOOL_IDLE, TOOL_RUNNING, TOOL_READY, TOOL_CLAIMED,
    TOOL_RESERVED, TOOL_GENERATED, TOOL_FAILED } tool_phase_e;
typedef struct {
    qwen_tool_envelope_t request;
    char result[QWEN_TOOL_RESULT];
    uint32_t length;
    uint8_t done;
} tool_history_t;
static DEFINE_SPINLOCK(s_tool_lock);
static struct {
    uint32_t generation, epoch, started, wait_started, output_floor;
    uint8_t allowed, cancel, count, slot, inbox, waiting, early_output, tx_copying;
    tool_phase_e phase;
    qwen_tool_snapshot_t snapshot;
} s_state;
/* 历史项发布后不再改变，仅工作任务写入；ASR 取消不回退条目计数。
 * 只有运输线程全部结束后才允许复用。 */
static tool_history_t s_history[QWEN_TOOL_HISTORY + 1U];
static qwen_tool_envelope_t s_rx, s_inbox, s_work, s_tx;
static qwen_tool_observation_t s_observation;
static char s_work_result[QWEN_TOOL_RESULT], s_tx_result[QWEN_TOOL_RESULT];
static uint8_t s_wire[QWEN_TOOL_WIRE_SIZE];
static uint32_t s_tx_epoch;
static uint8_t s_tx_armed;
static char s_task[QWEN_TOOL_CONTEXT], s_dialog[QWEN_TOOL_CONTEXT];
static char s_output_round[QWEN_TOOL_CONTEXT];
static char s_output_llm[QWEN_TOOL_CONTEXT], s_active_round[QWEN_TOOL_CONTEXT];
static char s_active_llm[QWEN_TOOL_CONTEXT], s_inbox_llm[QWEN_TOOL_CONTEXT];
/* 仅接收线程使用的近期输出身份，不作为完成确认。 */
static struct {
    char round[QWEN_TOOL_CONTEXT], llm[QWEN_TOOL_CONTEXT];
    uint32_t first_sequence;
} s_seen[16];
static uint8_t s_seen_count, s_seen_next;
static uint32_t s_receive_sequence;
static const qwen_local_tool_port_t *s_port;
static void (*s_wake)(void);
/**
 * @brief 调用可选的非阻塞工作任务唤醒回调。
 */
static void wake(void) { if (s_wake) s_wake(); }

/**
 * @brief 产品启动时绑定有静态生命周期的工具端口；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_bind(const qwen_local_tool_port_t *port)
{
    s_port = port && port->prefix && *port->prefix && strlen(port->prefix) < QWEN_TOOL_NAME &&
        port->accepts && port->start && port->poll && port->cancel ? port : NULL;
}
/**
 * @brief 保存持续有效的工作任务唤醒回调；合同见 qwen_local_tool.h。
 */
void qwen_local_tool_waker(void (*callback)(void)) { s_wake = callback; }

/**
 * @brief 推进取消代次并撤销活动工具状态，保留已发布历史与固定缓冲。
 * @note 调用者持有短状态锁；不移动历史项或调用产品回调。
 */
static void cancel_locked(void)
{
    s_state.epoch++;
    s_state.cancel = 1U;
    s_state.phase = TOOL_IDLE;
    s_state.waiting = s_state.early_output = 0U;
    if (s_state.inbox == 1U) s_state.inbox = 0U;
    s_state.snapshot.notice = QWEN_TOOL_NOTICE_NONE;
}

/**
 * @brief 更新会话代次与准入，变化时撤销当前工具状态；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_session(uint32_t generation, uint8_t allowed)
{
    spin_lock(&s_tool_lock);
    if (generation != s_state.generation || (!allowed && s_state.allowed)) cancel_locked();
    s_state.generation = generation;
    s_state.allowed = allowed;
    spin_unlock(&s_tool_lock);
}

/**
 * @brief 撤销活动工具状态并请求工作任务取消，保留已发布历史；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_interrupt(void)
{
    uint8_t changed = 0U;
    spin_lock(&s_tool_lock);
    if (s_state.phase != TOOL_IDLE || s_state.waiting || s_state.inbox) {
        cancel_locked(); changed = 1U;
    } else s_state.snapshot.notice = QWEN_TOOL_NOTICE_NONE;
    spin_unlock(&s_tool_lock);
    if (changed) wake();
}

/**
 * @brief 检查工具阶段、等待云输出或收件槽是否活动；完整合同见 qwen_local_tool.h。
 */
uint8_t qwen_local_tool_busy(void)
{
    uint8_t busy;
    spin_lock(&s_tool_lock);
    busy = s_state.phase != TOOL_IDLE || s_state.waiting || s_state.inbox;
    spin_unlock(&s_tool_lock);
    return busy;
}
/**
 * @brief 读取工具阶段是否为 TOOL_FAILED；完整合同见 qwen_local_tool.h。
 */
uint8_t qwen_local_tool_failed(void)
{
    uint8_t failed;
    spin_lock(&s_tool_lock); failed = s_state.phase == TOOL_FAILED; spin_unlock(&s_tool_lock);
    return failed;
}
/**
 * @brief 复制工具通知与事务值快照；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_get_snapshot(qwen_tool_snapshot_t *snapshot)
{
    if (!snapshot) return;
    spin_lock(&s_tool_lock); *snapshot = s_state.snapshot; spin_unlock(&s_tool_lock);
}
/**
 * @brief 更新 SDK 是否允许重试的展示标记；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_sdk_ready(uint8_t ready)
{
    spin_lock(&s_tool_lock); s_state.snapshot.retry_ready = ready; spin_unlock(&s_tool_lock);
}
/**
 * @brief 普通请求到达且工具完全空闲时清除旧工具通知；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_normal_request(void)
{
    spin_lock(&s_tool_lock);
    if (s_state.phase == TOOL_IDLE && !s_state.inbox && !s_state.waiting)
        s_state.snapshot.notice = QWEN_TOOL_NOTICE_NONE;
    spin_unlock(&s_tool_lock);
}

/**
 * @brief 在接收线程有界历史中查询或登记当前云输出身份首次序号。
 * @param[in] sequence 当前接收序号。
 * @return 已见身份的首次序号，未见时为传入 sequence。
 * @note 只作后续文本观察，不充当业务完成确认。
 */
static uint32_t output_first_sequence(uint32_t sequence)
{
    uint8_t i;
    for (i = 0U; i < s_seen_count; i++) {
        if (s_observation.llm[0] && !strcmp(s_seen[i].llm, s_observation.llm)) return s_seen[i].first_sequence;
        if (!s_observation.llm[0] && s_observation.round[0] &&
            !strcmp(s_seen[i].round, s_observation.round)) return s_seen[i].first_sequence;
    }
    if (s_observation.llm[0] || s_observation.round[0]) {
        strcpy(s_seen[s_seen_next].llm, s_observation.llm);
        strcpy(s_seen[s_seen_next].round, s_observation.round);
        s_seen[s_seen_next].first_sequence = sequence;
        s_seen_next = (uint8_t)((s_seen_next + 1U) % 16U);
        if (s_seen_count < 16U) s_seen_count++;
    }
    return sequence;
}

/**
 * @brief 由唯一接收线程观察云输出并解码路由到当前任务与对话的工具命令；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_receive(const uint8_t *data, uint32_t length, uint32_t now)
{
    uint32_t epoch;
    uint8_t identity, observed = 0U;
    char task[QWEN_TOOL_CONTEXT] = {0}, dialog[QWEN_TOOL_CONTEXT] = {0};
    d_err_t decoded;
    (void)now;
    if (!s_port) return;
    spin_lock(&s_tool_lock);
    epoch = s_state.epoch;
    identity = s_task[0] && !s_dialog[0];
    spin_unlock(&s_tool_lock);
    if (identity && qwen_tool_identity(data, length, 1U, task, dialog)) {
        spin_lock(&s_tool_lock);
        if (epoch == s_state.epoch && !strcmp(task, s_task) && !s_dialog[0]) strcpy(s_dialog, dialog);
        spin_unlock(&s_tool_lock);
    }
    if (qwen_tool_observe(data, length, &s_observation) && s_observation.responding &&
        !strcmp(s_task, s_observation.task) && !strcmp(s_dialog, s_observation.dialog)) {
        uint32_t sequence, first;
        spin_lock(&s_tool_lock);
        sequence = ++s_receive_sequence;
        spin_unlock(&s_tool_lock);
        first = output_first_sequence(sequence);
        /* 空片段不能让后续正文被判旧；命令所在轮次也可恢复，
         * 协议不保证总会产生新的 round 或 LLM 编号。 */
        if (s_observation.text && !s_observation.commands) {
            spin_lock(&s_tool_lock);
            if (epoch == s_state.epoch && s_state.allowed &&
                ((int32_t)(first - s_state.output_floor) > 0 ||
                 (s_observation.round[0] && !strcmp(s_observation.round, s_output_round) &&
                  !strcmp(s_observation.llm, s_output_llm)))) {
                if (s_state.phase == TOOL_GENERATED) s_state.early_output = 1U;
                if (s_state.waiting) {
                    s_state.waiting = 0U;
                    s_state.snapshot.notice = QWEN_TOOL_NOTICE_OUTPUT;
                    observed = 1U;
                }
            }
            spin_unlock(&s_tool_lock);
        }
    }
    if (observed) { LOGI("QWEN_TOOL", "cloud output observed; no business ACK"); wake(); }
    decoded = qwen_tool_decode(data, length, s_port->prefix, &s_rx);
    /* 普通或超长消息不可解析不等于工具故障；不扫描任意文本找前缀，
     * 仅实际 commands 字段可路由工具。 */
    if (decoded != D_OK) return;
    if (!s_port->accepts(s_rx.call.name) || !s_task[0] || !s_dialog[0] ||
        strcmp(s_rx.task, s_task) || strcmp(s_rx.dialog, s_dialog)) return;
    spin_lock(&s_tool_lock);
    if (!s_state.allowed || s_state.cancel || epoch != s_state.epoch || s_state.inbox) {
        spin_unlock(&s_tool_lock); return;
    }
    s_state.inbox = 3U; /* RX 正在写入，工作任务及取消不能复用此槽。 */
    spin_unlock(&s_tool_lock);
    s_inbox = s_rx;
    strcpy(s_inbox_llm, s_observation.llm);
    spin_lock(&s_tool_lock);
    s_state.inbox = epoch == s_state.epoch && s_state.allowed ? 1U : 0U;
    spin_unlock(&s_tool_lock);
    LOGI("QWEN_TOOL", "command received name=%s args=%u", s_rx.call.name, (unsigned)s_rx.call.count);
#if D_QWEN_WSS_DIAG_ENABLE
    qwen_wire_diag_tool_request(s_rx.request);
#endif
    wake();
}

/**
 * @brief 在工作任务中检查请求去重、冲突与历史容量，再认领或重放结果。
 * @param[in] epoch 捕获的工具取消代次。
 * @param[in] now 本次准入毫秒值。
 * @note 锁外比较和复制期间收件槽固定；相同编号参数变化不重新执行。
 */
static void admit(uint32_t epoch, uint32_t now)
{
    uint8_t slot, count;
    const char *rejection = NULL;
    const char *status = "request_interrupted_query_current_state";
    spin_lock(&s_tool_lock); count = s_state.count; spin_unlock(&s_tool_lock);
    for (slot = 0U; slot < count; slot++)
        if (!strcmp(s_history[slot].request.request, s_inbox.request)) break;
    if (slot < count) {
        if (memcmp(&s_history[slot].request.call, &s_inbox.call, sizeof(s_inbox.call))) {
            rejection = "同一请求的参数已改变，本次未执行，请重新查询。";
            status = "request_id_conflict";
        } else if (!s_history[slot].done)
            rejection = "原请求已被中断，不会重新执行，请查询当前状态。";
    } else if (count == QWEN_TOOL_HISTORY) {
        rejection = "本次会话工具请求已满，请重新开启对话。";
        status = "command_history_full_start_new_conversation";
    }
    if (rejection) slot = QWEN_TOOL_HISTORY;
    if (slot == count || rejection) {
        s_history[slot].request = s_inbox;
        s_history[slot].done = 0U;
    }
    if (rejection) {
        /* 仅拼接固定可信字面量，不插入请求文本。 */
        strcpy(s_history[slot].result, "{\"success\":false,\"applied\":false,\"status\":\"");
        strcat(s_history[slot].result, status);
        strcat(s_history[slot].result, "\",\"result_text\":\"");
        strcat(s_history[slot].result, rejection);
        strcat(s_history[slot].result, "\"}");
        s_history[slot].length = (uint32_t)strlen(s_history[slot].result);
        s_history[slot].done = 1U;
    }
    s_work = s_history[slot].request;
    spin_lock(&s_tool_lock);
    s_state.inbox = 0U;
    if (epoch == s_state.epoch && s_state.allowed) {
        if (slot == count && !rejection) s_state.count++;
        s_state.slot = slot;
        /* 重放保留结果，同时观察本次调用上下文；
         * 这些有界身份字符串只用于元数据。 */
        strcpy(s_active_round, s_inbox.round);
        strcpy(s_active_llm, s_inbox_llm);
        s_state.started = now;
        s_state.waiting = 0U;
        s_state.snapshot.transaction++;
        s_state.snapshot.notice = QWEN_TOOL_NOTICE_EXECUTING;
        s_state.phase = s_history[slot].done ? TOOL_READY : TOOL_RUNNING;
    }
    spin_unlock(&s_tool_lock);
    LOGI("QWEN_TOOL", "admitted slot=%u cached=%u", (unsigned)slot, (unsigned)s_history[slot].done);
}

/**
 * @brief 由唯一工作任务推进工具准入、执行、轮询、取消与云输出期限；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_tick(uint32_t now)
{
    uint32_t epoch;
    uint8_t cancel, incoming = 0U, running, slot, start = 0U, expired = 0U;
    d_err_t result;
    spin_lock(&s_tool_lock);
    epoch = s_state.epoch;
    cancel = s_state.cancel; s_state.cancel = 0U;
    if (s_state.waiting && (uint32_t)(now - s_state.wait_started) >= TOOL_TIMEOUT_MS) {
        s_state.waiting = 0U;
        s_state.snapshot.notice = QWEN_TOOL_NOTICE_NO_ANSWER;
        expired = 1U;
    }
    if (s_state.phase != TOOL_IDLE && (uint32_t)(now - s_state.started) >= TOOL_TIMEOUT_MS)
        s_state.phase = TOOL_FAILED;
    if (s_state.phase == TOOL_IDLE && s_state.inbox == 1U && !s_state.tx_copying) {
        s_state.inbox = 2U; incoming = 1U;
    }
    spin_unlock(&s_tool_lock);
    if (expired) LOGW("QWEN_TOOL", "cloud output timeout; keep transport and cached result");
    if (cancel && s_port) s_port->cancel();
    if (incoming) { admit(epoch, now); start = 1U; }
    spin_lock(&s_tool_lock);
    running = epoch == s_state.epoch && s_state.phase == TOOL_RUNNING;
    slot = s_state.slot;
    spin_unlock(&s_tool_lock);
    if (!running) return;
    result = start ? s_port->start(&s_work.call) : D_OK;
    if (result == D_OK) result = s_port->poll(s_work_result, sizeof(s_work_result));
    if (result == D_OK) {
        uint32_t length = (uint32_t)strlen(s_work_result);
        /* 即使复制中 ASR 取消请求，该历史槽仍保持固定。 */
        memcpy(s_history[slot].result, s_work_result, length + 1U);
        s_history[slot].length = length;
    }
    spin_lock(&s_tool_lock);
    if (epoch == s_state.epoch && s_state.phase == TOOL_RUNNING) {
        if (result == D_OK) {
            s_history[slot].done = 1U;
            s_state.phase = TOOL_READY;
        } else if (result != D_ERR_BUSY) s_state.phase = TOOL_FAILED;
    }
    spin_unlock(&s_tool_lock);
}

/**
 * @brief 由唯一发送者领取已就绪结果，并复制到固定发送暂存；完整合同见 qwen_local_tool.h。
 */
uint8_t qwen_local_tool_tx_ready(void)
{
    uint8_t ready, slot;
    uint32_t epoch;
    spin_lock(&s_tool_lock);
    ready = s_state.allowed && s_state.phase == TOOL_READY;
    slot = s_state.slot; epoch = s_state.epoch;
    if (ready) {
        s_state.phase = TOOL_CLAIMED;
        s_state.tx_copying = 1U;
    }
    spin_unlock(&s_tool_lock);
    if (!ready) return 0U;
    /* 只有当前发送者写发送暂存，历史槽保持不变。 */
    s_tx = s_history[slot].request;
    memcpy(s_tx_result, s_history[slot].result, s_history[slot].length + 1U);
    s_tx_epoch = epoch;
    spin_lock(&s_tool_lock);
    ready = epoch == s_state.epoch && s_state.phase == TOOL_CLAIMED;
    s_state.tx_copying = 0U;
    spin_unlock(&s_tool_lock);
    if (!ready) wake();
    return ready;
}

/**
 * @brief 发布 SDK 生成回复请求的立即受理结果；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_tx_reserved(int sdk_result)
{
    spin_lock(&s_tool_lock);
    s_tx_armed = sdk_result == 0;
    if (s_tx_epoch == s_state.epoch && s_state.phase == TOOL_CLAIMED)
        s_state.phase = sdk_result == 0 ? TOOL_RESERVED : TOOL_READY;
    else if (!sdk_result) s_state.phase = TOOL_FAILED;
    spin_unlock(&s_tool_lock);
    if (!sdk_result) LOGI("QWEN_TOOL", "SDK request accepted=1");
}

/**
 * @brief 核对发送身份，并为工具拥有的文本回复帧填入固定结果；完整合同见 qwen_local_tool.h。
 */
d_err_t qwen_local_tool_tx_frame(uint8_t opcode, qwen_tool_frame_t *frame, uint8_t tool_owned)
{
    d_err_t result;
    uint8_t valid;
    char task[QWEN_TOOL_CONTEXT] = {0}, dialog[QWEN_TOOL_CONTEXT] = {0};
    if (!frame || !frame->data || frame->length > frame->capacity) return D_ERR_INVALID_ARG;
    if (opcode == 1U && qwen_tool_identity(frame->data, frame->length, 0U, task, dialog)) {
        spin_lock(&s_tool_lock);
        valid = !s_task[0] || !strcmp(s_task, task);
        if (valid) strcpy(s_task, task);
        else s_state.phase = TOOL_FAILED;
        spin_unlock(&s_tool_lock);
        if (!valid) return D_ERR_NOT_READY;
    }
    if (!tool_owned) return D_OK;
    spin_lock(&s_tool_lock);
    valid = s_tx_armed && s_tx_epoch == s_state.epoch && s_state.allowed && s_state.phase == TOOL_RESERVED;
    spin_unlock(&s_tool_lock);
    if (!valid || opcode != 1U || frame->length >= sizeof(s_wire)) return D_ERR_NOT_READY;
    memcpy(s_wire, frame->data, frame->length);
    result = qwen_tool_reply(&s_tx, s_tx_result, s_wire, &frame->length, sizeof(s_wire));
    spin_lock(&s_tool_lock);
    if (s_tx_epoch != s_state.epoch || !s_state.allowed || s_state.phase != TOOL_RESERVED) result = D_ERR_NOT_READY;
    s_state.early_output = 0U;
    s_state.output_floor = s_receive_sequence;
    strcpy(s_output_round, s_active_round);
    strcpy(s_output_llm, s_active_llm);
    s_state.phase = result == D_OK ? TOOL_GENERATED : TOOL_FAILED;
    spin_unlock(&s_tool_lock);
    if (result == D_OK) { frame->data = s_wire; frame->capacity = sizeof(s_wire); }
    LOGI("QWEN_TOOL", "result frame generated=%u bytes=%u", result == D_OK, (unsigned)frame->length);
    return result;
}

/**
 * @brief SDK 已受理却没有生成待发帧时标记工具失败；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_tx_empty(void)
{
    spin_lock(&s_tool_lock);
    if (s_tx_armed) s_state.phase = TOOL_FAILED;
    spin_unlock(&s_tool_lock);
}

#if D_QWEN_WSS_DIAG_ENABLE
/**
 * @brief 在套接字返回后只读比较发送帧与固定请求和结果；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_tx_diagnose(const qwen_tool_frame_t *frame)
{
    qwen_tool_reply_check_t check;
    /* 此处唯一发送者仍持有 s_tx、s_tx_result 及已发送视图；
     * 观察发生在套接字返回后，不改变发送结论。 */
    if (qwen_tool_check_reply(&s_tx, s_tx_result, frame, &check) != D_OK) {
        LOGW("QWEN_TOOL", "result check unavailable; send outcome unchanged");
        return;
    }
    LOGI("QWEN_TOOL", "result check available=1 context_match=%u request_match=%u text_match=%u structured_match=%u results=%u text_bytes=%u expected_text_bytes=%u",
        (unsigned)check.context_match, (unsigned)check.request_match,
        (unsigned)check.text_match, (unsigned)check.structured_match,
        (unsigned)check.results, (unsigned)check.text_bytes, (unsigned)check.expected_text_bytes);
}
#endif

/**
 * @brief 由唯一发送者在套接字返回后发布完整发送结果；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_tx_sent(int complete, uint32_t now)
{
    uint8_t generated;
    spin_lock(&s_tool_lock);
    generated = s_state.phase == TOOL_GENERATED && s_tx_epoch == s_state.epoch;
    if (generated) {
        if (!complete) s_state.phase = TOOL_FAILED;
        else {
            s_state.phase = TOOL_IDLE;
            s_state.wait_started = now;
            s_state.waiting = !s_state.early_output;
            s_state.snapshot.notice = s_state.waiting ? QWEN_TOOL_NOTICE_WAITING : QWEN_TOOL_NOTICE_OUTPUT;
        }
    }
    s_tx_armed = 0U;
    s_state.early_output = 0U;
    spin_unlock(&s_tool_lock);
    if (generated) LOGI("QWEN_TOOL", "result frame sent=%u", complete != 0);
    wake();
}

/**
 * @brief 在旧收发线程和 SDK 使用者全部退出后清空工具运输代次与历史引用；完整合同见 qwen_local_tool.h。
 */
void qwen_local_tool_transport_reset(void)
{
    /* 仅连接工作任务调用，前提是旧收发线程和 SDK 使用者已经退出。 */
    spin_lock(&s_tool_lock);
    cancel_locked();
    s_state.count = s_state.inbox = s_state.cancel = s_state.tx_copying = 0U;
    s_tx_armed = 0U;
    s_task[0] = s_dialog[0] = s_output_round[0] = '\0';
    s_output_llm[0] = s_active_round[0] = s_active_llm[0] = s_inbox_llm[0] = '\0';
    s_receive_sequence = s_state.output_floor = 0U;
    spin_unlock(&s_tool_lock);
    s_seen_count = s_seen_next = 0U;
    if (s_port) s_port->cancel();
}
#endif
