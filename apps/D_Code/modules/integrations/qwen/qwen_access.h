/** @file qwen_access.h @brief 定义私有接入静态库的稳定配置及平台回调端口，不公开鉴权凭据。
 * @author D_Code @date 2026-10-28 */
#ifndef QWEN_ACCESS_H
#define QWEN_ACCESS_H
#include "framework/d_error.h"
#include "qwen_request_gate.h"
#include "qwen_activation.h"
#include <stdint.h>
#include <stddef.h>

#define QWEN_ACCESS_ABI 1U
#define QWEN_ACCESS_NAME_SIZE 33U
#define QWEN_ACCESS_VOICE_SIZE 64U
#define QWEN_ACCESS_RECORD_MAX 127U
#define QWEN_ACCESS_HTTP_CAPACITY 4096U

/** @brief 按值复制的公开选项；configure 不保留调用方字符串指针，固定容量属于库 ABI。 */
typedef struct {
    uint32_t abi, size; /**< 接口版本及整个结构体字节数，须严格匹配当前库。 */
    uint8_t auth_mode, activation_source, factory_test; /**< 鉴权模式、激活码来源及工厂测试选择，沿用公开配置枚举。 */
    uint8_t factory_name, mac_name, dialog_mode; /**< 出厂设备名、MAC 名称策略及会话模式，彼此独立。 */
    uint8_t upstream_format, downstream_format, opus_cbr; /**< 上下行格式及下行 OPUS 固定码率选择。 */
    uint8_t tls_required, allow_unverified_diagnostic; /**< TLS 校验及明确允许非校验诊断；发布默认严格校验。 */
    uint16_t opus_frame_ms, opus_bitrate_kbps; /**< OPUS 帧长（毫秒）和码率（kbps），下行 OPUS 时受检。 */
    uint32_t upstream_rate, downstream_rate, recorder_bytes, http_timeout_ms; /**< 上下行采样率、录音环形缓冲字节数和 HTTP 超时毫秒数，必须非零。 */
    char fixed_name[QWEN_ACCESS_NAME_SIZE]; /**< 含 NUL 的固定名称值；不随激活码来源自动替换。 */
    char voice[QWEN_ACCESS_VOICE_SIZE]; /**< 含 NUL 且非空的音色标识。 */
} qwen_access_options_t;

/** @brief 借用的发送帧；工具所有者须保持缓冲区直到 tx_sent，即使期间发生取消。 */
typedef struct {
    uint8_t *data;             /**< 帧数据的借用视图，回调可替换为工具持有的发送缓冲。 */
    uint32_t length, capacity; /**< 有效字节数及该视图可用容量，必须 length<=capacity。 */
} qwen_access_frame_t;

typedef enum {
    QWEN_ACCESS_RECORD_OK, QWEN_ACCESS_RECORD_EMPTY,
    QWEN_ACCESS_RECORD_READ, QWEN_ACCESS_RECORD_CRC, QWEN_ACCESS_RECORD_LENGTH
} qwen_access_record_result_e;

/**
 * @brief 借用至固件结束的平台及工具回调表，结构体本身在 configure 时复制。
 * @note 工具回调必须成组登记或全部为 NULL；可选诊断为 NULL 表示关闭。回调不接收 HTTP 凭据。
 */
typedef struct {
    uint32_t abi, size;
    qwen_access_record_result_e (*read_record)(uint8_t *bytes, uint16_t *length); /**< 读取最多 RECORD_MAX 字节的原始载荷，length 为输出；只在身份工作流程调用。 */
    void (*receive)(const uint8_t *, uint32_t, uint32_t); /**< 接收线程交付借用的文本帧、长度及当前毫秒数；回调不得保存载荷指针。 */
    uint8_t (*tx_ready)(void); /**< 唯一发送线程尝试认领准备好的工具结果，非零表示可以预约 SDK 响应。 */
    void (*tx_reserved)(int); /**< 回传 SDK 预约结果，0 表示接受，其他结果由工具状态处理。 */
    void (*tx_empty)(void); /**< 标记已预约工具响应的提取失败，不把空帧当成发送成功。 */
    uint8_t (*failed)(void); /**< 非零表示工具事务失败，传输应结束而非继续发送。 */
    d_err_t (*tx_frame)(uint8_t, qwen_access_frame_t *, uint8_t); /**< 依次传入 opcode、可修改的帧视图、是否为工具所有；D_OK 才可发送。 */
    void (*tx_sent)(int, uint32_t); /**< 回传本地 socket 是否完整写入及当前毫秒数，不代表云端业务完成。 */
    void (*tx_diagnose)(const qwen_access_frame_t *); /**< 唯一发送线程在 socket 返回后只读检查发送视图；诊断不改变发送结果。 */
    void (*transport_reset)(void); /**< 旧传输和 SDK 使用者退出后、启动新线程前清理本地工具与请求槽。 */
    uint8_t (*request_begin)(qwen_request_owner_e); /**< 尝试取得响应提交占用；非零时必须配对 request_submitted。 */
    void (*request_submitted)(int); /**< 回传 SDK 是否已受理，非零为接受，不是响应完成。 */
    uint8_t (*generate_begin)(void); /**< 尝试取得唯一 SDK 提取占用；非零时必须配对 generate_end。 */
    qwen_request_owner_e (*generate_end)(int, uint8_t); /**< 依次接收响应分类和空标记，结束提取并返回帧归属；BROKEN 禁止发送。 */
    int (*is_response)(const uint8_t *, uint32_t); /**< 只读分类文本帧及长度：1 为响应、0 为其他有效对象、-1 为不可分类。 */
    void (*diag_rx)(const uint8_t *, uint32_t); /**< 只读观察收到的文本帧及长度，不能保留借用缓冲。 */
    void (*diag_tx)(const uint8_t *, uint32_t, int, const char *); /**< 只读观察发送帧、长度、socket 返回值及归属标签，不接收 HTTP 凭据。 */
} qwen_access_hooks_t;

/**
 * @brief 校验 ABI、配置及回调组合，并复制一次性的接入配置。
 * @param[in] options 完整配置值，abi 与 size 必须匹配；内嵌字符串随结构体复制。
 * @param[in] hooks 完整回调表，abi 与 size 必须匹配；回调代码借用至固件结束。
 * @return D_OK 为首次配置成功；重复成功配置后为 D_ERR_BUSY；参数或回调组合无效为 D_ERR_INVALID_ARG；所需出厂读取端口缺失为 D_ERR_NOT_READY。
 * @note 只在启动装配阶段、会话及身份工作开始前调用，不支持运行中重配。可选工具端口必须成组全有或全无，诊断端口可独立关闭。
 * @note 本函数不读取 Flash，不激活平台、不初始化 SDK 或发起网络请求。成功只证明配置已登记。
 */
d_err_t qwen_access_configure(const qwen_access_options_t *options,
                              const qwen_access_hooks_t *hooks);
/**
 * @brief 复制已经准备成功的设备名缓存，不触发身份准备。
 * @param[out] out 调用方名称缓冲区；非空且容量非零时先将首字节清零。
 * @param[in] capacity 输出字节容量，须容纳完整名称及结尾 NUL。
 * @return D_OK 为已复制含 NUL 的名称；未准备为 D_ERR_NOT_READY；空输出、零容量或容量不足为 D_ERR_INVALID_ARG。
 * @note 在库锁内读取值；不访问 Flash、SDK、网络或私有凭据，空间不足不截断名称。
 */
d_err_t qwen_access_get_device_name(char *out, size_t capacity);
/**
 * @brief 读取最近一次身份准备的缓存错误。
 * @return 缓存的 qwen_activation_error_e；初始 NONE 不证明身份已经准备。
 * @note 只读诊断，不执行 IO，不返回激活码或访问凭据。
 */
qwen_activation_error_e qwen_access_identity_error(void);
/**
 * @brief 由 Qwen 现有工作任务准备一次设备身份，并发布到缓存。
 * @return D_OK 为已准备或本次准备成功；未配置或半托管 MAC 不可用为 D_ERR_NOT_READY；出厂记录错误为 D_ERR_IO；固定名称无效为 D_ERR_INVALID_ARG。
 * @note 调用方必须持有现有准备占用并串行驱动；可能读取出厂 Flash 或 SDK VM，不执行网络 IO。
 * @note 出厂记录错误不静默换身份；合法旧单码没有名称时才沿用原 MAC/固定名策略。发布时受库锁保护，成功后名称不随页面重建。
 */
d_err_t qwen_access_prepare_identity(void);
/**
 * @brief 把当前固件公开菜单和平台回调装配到同一份接入库。
 * @return 首次返回库配置结果；该产品适配已成功装配后重复调用为 D_OK。
 * @note 仅在启动阶段调用；本适配保留自己的幂等标志，不改变库 configure 重复配置返回 BUSY 的合同。
 */
d_err_t qwen_access_configure_product(void);
/**
 * @brief 校验 SDK NTP 状态及秒级时间范围，并按需同步密码工具的时间基准。
 * @return 1 表示 SDK NTP 已就绪且时间合法；0 表示尚未就绪或时间超出范围。
 * @note 同步本地基准，不在此发起 NTP 网络请求；重复相同秒值不反复重置 tick 基准。
 */
int qwen_auth_sync_network_time(void);
#endif
