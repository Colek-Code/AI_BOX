/** @file qwen_local_tool.h @brief 不携 SDK 或产品类型的有界设备工具端口与协议合同。 */
#ifndef QWEN_LOCAL_TOOL_H
#define QWEN_LOCAL_TOOL_H
#include "framework/d_error.h"
#include "qwen_access.h"
#include <stdint.h>
#define QWEN_TOOL_ARGS 12U
#define QWEN_TOOL_NAME 40U
#define QWEN_TOOL_ID 128U
#define QWEN_TOOL_CONTEXT 65U
#define QWEN_TOOL_RESULT 4096U
#define QWEN_TOOL_WIRE_SIZE 6144U
#define QWEN_TOOL_JSON_MAX 16384U
#define QWEN_TOOL_HISTORY 16U

typedef enum { QWEN_TOOL_STRING = 1, QWEN_TOOL_INTEGER, QWEN_TOOL_BOOLEAN } qwen_tool_kind_e;
typedef struct { char name[24]; char value[64]; uint8_t kind; } qwen_tool_arg_t;
typedef struct {
    char name[QWEN_TOOL_NAME];
    qwen_tool_arg_t args[QWEN_TOOL_ARGS]; /* 按名称排序，未使用字节清零。 */
    uint8_t count;
} qwen_tool_call_t;
typedef struct {
    char request[QWEN_TOOL_ID];
    char task[QWEN_TOOL_CONTEXT];
    char dialog[QWEN_TOOL_CONTEXT];
    char round[QWEN_TOOL_CONTEXT];
    qwen_tool_call_t call;
} qwen_tool_envelope_t;
typedef enum {
    QWEN_TOOL_NOTICE_NONE = 0, QWEN_TOOL_NOTICE_EXECUTING,
    QWEN_TOOL_NOTICE_WAITING, QWEN_TOOL_NOTICE_OUTPUT,
    QWEN_TOOL_NOTICE_NO_ANSWER, QWEN_TOOL_NOTICE_REJECTED
} qwen_tool_notice_e;
typedef struct {
    qwen_tool_notice_e notice;
    uint32_t transaction;
    uint8_t retry_ready;
} qwen_tool_snapshot_t;
typedef qwen_access_frame_t qwen_tool_frame_t;

/* 仅 qwen_tool_check_reply 返回 D_OK 时比较位有效；
 * 不匹配或检查不可用不能驱动传输及业务决策。 */
typedef struct {
    uint8_t context_match, request_match, text_match, structured_match;
    uint32_t results, text_bytes, expected_text_bytes;
} qwen_tool_reply_check_t;
typedef struct {
    char task[QWEN_TOOL_CONTEXT], dialog[QWEN_TOOL_CONTEXT];
    char round[QWEN_TOOL_CONTEXT], llm[QWEN_TOOL_CONTEXT];
    uint8_t responding, finished, text, commands;
} qwen_tool_observation_t;
typedef struct {
    const char *prefix; /* 产品拥有且不可变的路由命名空间。 */
    int (*accepts)(const char *name);
    d_err_t (*start)(const qwen_tool_call_t *call);
    /* D_ERR_BUSY 表示继续等待；D_OK 输出有界 JSON 对象，含产品负责的非空 result_text。
     * 编解码器把它移到 content.text，其余字段转到 structuredContent；
     * 不跨该端口传递产品类型。 */
    d_err_t (*poll)(char *result, uint32_t capacity);
    void (*cancel)(void); /* 仅工作任务调用；不取消服务已经进入的 VM 写入。 */
} qwen_local_tool_port_t;

/**
 * @brief 产品启动时绑定有静态生命周期的工具端口。
 * @param[in] port 静态端口；前缀或回调不完整时清除绑定。
 * @note 仅启动装配时调用，端口需存活至 Qwen 服务结束。
 */
void qwen_local_tool_bind(const qwen_local_tool_port_t *port);
/**
 * @brief 绑定非阻塞工作任务唤醒回调。
 * @param[in] wake 可空、持续有效的唤醒回调。
 */
void qwen_local_tool_waker(void (*wake)(void));
/**
 * @brief 更新会话代次与准入，变化时撤销当前工具状态。
 * @param[in] generation 当前会话代次。
 * @param[in] allowed 当前是否允许本地工具。
 * @note 撤销只记录状态，产品 cancel 由工具工作任务随后执行。
 */
void qwen_local_tool_session(uint32_t generation, uint8_t allowed);
/**
 * @brief 由唯一接收线程观察云输出并解码路由到当前任务与对话的工具命令。
 * @param[in] data 本次接收的借用字节。
 * @param[in] length 帧字节数。
 * @param[in] now 兼容时间参数，当前不使用。
 * @note 仅真实 commands 字段路由工具；普通解析失败不当作工具故障。云输出观察不是业务完成确认。
 */
void qwen_local_tool_receive(const uint8_t *data, uint32_t length, uint32_t now);
/**
 * @brief 由唯一工作任务推进工具准入、执行、轮询、取消与云输出期限。
 * @param[in] now 设备单调毫秒值。
 * @note 产品回调在状态锁外执行；成功结果保留到运输线程全部结束后才可复用历史槽。
 */
void qwen_local_tool_tick(uint32_t now);
/**
 * @brief 检查工具阶段、等待云输出或收件槽是否活动。
 * @return 任一活动返回非零。
 */
uint8_t qwen_local_tool_busy(void);
/**
 * @brief 读取工具阶段是否为 TOOL_FAILED。
 * @return 失败返回非零。
 */
uint8_t qwen_local_tool_failed(void);
/**
 * @brief 撤销活动工具状态并请求工作任务取消，保留已发布历史。
 */
void qwen_local_tool_interrupt(void);
/**
 * @brief 复制工具通知与事务值快照。
 * @param[out] snapshot 输出快照；为空时不处理。
 */
void qwen_local_tool_get_snapshot(qwen_tool_snapshot_t *snapshot);
/**
 * @brief 更新 SDK 是否允许重试的展示标记。
 * @param[in] ready SDK 就绪值。
 */
void qwen_local_tool_sdk_ready(uint8_t ready);
/**
 * @brief 普通请求到达且工具完全空闲时清除旧工具通知。
 */
void qwen_local_tool_normal_request(void);
/**
 * @brief 由唯一发送者领取已就绪结果，并复制到固定发送暂存。
 * @return 领取成功且代次仍有效返回非零，否则为 0。
 * @note 本调用有领取副作用；SDK 调用仍由传输层负责。
 */
uint8_t qwen_local_tool_tx_ready(void);
/**
 * @brief 发布 SDK 生成回复请求的立即受理结果。
 * @param[in] sdk_result SDK 立即返回值，0 表示受理。
 * @note 受理后还须生成并发送真实帧，不能视作云输出完成。
 */
void qwen_local_tool_tx_reserved(int sdk_result);
/**
 * @brief 核对发送身份，并为工具拥有的文本回复帧填入固定结果。
 * @param[in] opcode WebSocket 操作码，工具回复要求 1。
 * @param[in,out] frame 输入 SDK 帧视图，成功后可能替换为静态工具缓冲视图。
 * @param[in] tool_owned 非零表示本次回复归工具所有。
 * @return 成功返回 D_OK；参数无效、代次或状态失效及编码失败返回对应错误。
 * @note 返回视图固定保留至 tx_sent，即使期间取消也不提前复用；失败后不得发送。
 */
d_err_t qwen_local_tool_tx_frame(uint8_t opcode, qwen_tool_frame_t *frame, uint8_t tool_owned);
/**
 * @brief 由唯一发送者在套接字返回后发布完整发送结果。
 * @param[in] complete 非零表示完整发送，0 表示失败或不完整。
 * @param[in] now 当前毫秒值，作为等待云输出的起点。
 * @note 发送完成独立于云输出；不完整发送进入失败，已缓存业务结果仍保留。
 */
void qwen_local_tool_tx_sent(int complete, uint32_t now);
/**
 * @brief SDK 已受理却没有生成待发帧时标记工具失败。
 */
void qwen_local_tool_tx_empty(void);
/**
 * @brief 在套接字返回后只读比较发送帧与固定请求和结果。
 * @param[in] frame 唯一发送者仍持有的实际帧视图。
 * @note 只做诊断；检查不可用或不匹配不改变发送结论和业务状态。
 */
void qwen_local_tool_tx_diagnose(const qwen_tool_frame_t *frame);
/**
 * @brief 在旧收发线程和 SDK 使用者全部退出后清空工具运输代次与历史引用。
 * @note 仅连接所有者在新传输启动前调用；不能以此提前复用仍被旧线程借用的缓冲。
 */
void qwen_local_tool_transport_reset(void);

/**
 * @brief 解码已结束 RespondingContent 中唯一匹配前缀的工具命令。
 * @param[in] data 有界输入帧。
 * @param[in] length 有效字节数。
 * @param[in] prefix 非空产品路由命名空间前缀。
 * @param[out] out 非空有界命令输出；解析失败可能已清空或部分写入。
 * @return 成功返回 D_OK；没有本地工具或尚未结束返回 D_ERR_NOT_FOUND；格式、容量或重复匹配返回 D_ERR_INVALID_ARG。
 * @note 参数名排序且重复拒绝；仅接受字符串、布尔与受限整数，不根据任意文本猜测命令。
 */
d_err_t qwen_tool_decode(const uint8_t *data, uint32_t length, const char *prefix, qwen_tool_envelope_t *out);
/**
 * @brief 把产品结果嵌入匹配的 SDK RequestToRespond 空文本帧。
 * @param[in] request 固定请求身份。
 * @param[in] result 含非空 result_text 的零结尾 JSON 对象。
 * @param[in,out] data 输入 SDK JSON 帧，成功时改为包含结果的输出。
 * @param[in,out] length 输入帧长，成功后更新；失败时 data 可能被部分写入。
 * @param[in] capacity 缓冲容量，最多 16384。
 * @return 成功返回 D_OK；格式或身份不符返回 D_ERR_INVALID_ARG；分配失败返回 D_ERR_NO_MEMORY；输出容量不足返回 D_ERR_FULL。
 * @note result_text 移到 content.text，其余通用字段置于 structuredContent，SDK 不拥有临时 JSON 树。
 */
d_err_t qwen_tool_reply(const qwen_tool_envelope_t *request, const char *result,
    uint8_t *data, uint32_t *length, uint32_t capacity);
/**
 * @brief 只读比较固定请求、产品结果与实际发送帧。
 * @param[in] request 固定请求。
 * @param[in] result 产品结果 JSON。
 * @param[in] frame 实际帧视图。
 * @param[out] check 非空诊断输出，先清零。
 * @return 可比较返回 D_OK；参数或 JSON 不可用返回 D_ERR_INVALID_ARG。
 * @note 只有 D_OK 时比较位有效；不匹配和检查失败均不得驱动业务或传输决策。
 */
d_err_t qwen_tool_check_reply(const qwen_tool_envelope_t *request, const char *result,
    const qwen_tool_frame_t *frame, qwen_tool_reply_check_t *check);
/**
 * @brief 按精确名称查找有界命令参数。
 * @param[in] call 命令值对象。
 * @param[in] name 非空参数名称。
 * @return 参数的借用指针；未找到或 call 为空时返回 NULL。
 * @note 返回值生命周期依附 call。
 */
const qwen_tool_arg_t *qwen_tool_argument(const qwen_tool_call_t *call, const char *name);
/**
 * @brief 只读提取输出身份及文本、命令和结束标记。
 * @param[in] data 原始帧字节。
 * @param[in] length 帧字节数。
 * @param[out] observation 非空观察输出，先清零。
 * @return 成功提取 task 与 dialog 返回非零，否则返回 0。
 */
int qwen_tool_observe(const uint8_t *data, uint32_t length, qwen_tool_observation_t *observation);
/**
 * @brief 把完整 JSON 对象分类为 RequestToRespond 或其他帧。
 * @param[in] data 帧输入。
 * @param[in] length 字节数。
 * @return 响应帧返回 1，其他有效对象返回 0，无法分类返回 -1。
 * @note -1 不是普通 SDK 帧的证据，调用方不得据此放行发送。
 */
int qwen_tool_is_response_frame(const uint8_t *data, uint32_t length);
/**
 * @brief 从 SDK Start 或服务端 Started、Listening 提取运输身份。
 * @param[in] data 帧输入。
 * @param[in] length 字节数。
 * @param[in] direction 0 检查 SDK Start，非零检查服务端 Started 或 Listening。
 * @param[out] task 至少 QWEN_TOOL_CONTEXT 字节的任务编号输出。
 * @param[out] dialog 服务端方向所需的对话编号输出；SDK Start 方向不改。
 * @return 识别并复制所需身份返回非零，否则为 0。
 */
int qwen_tool_identity(const uint8_t *data, uint32_t length, uint8_t direction,
    char task[QWEN_TOOL_CONTEXT], char dialog[QWEN_TOOL_CONTEXT]);
#endif
