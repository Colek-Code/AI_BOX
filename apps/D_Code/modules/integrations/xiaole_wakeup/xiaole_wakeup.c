/** @file xiaole_wakeup.c @brief 小乐监听、双通道采样、事件代次与库任务退出编排。 */
#include "config/app_config.h"

#if D_FEATURE_XIAOLE_WAKEUP

#include "xiaole_wakeup.h"
#include "xiaole_sdk_release.h"
#include "modules/services/audio/d_audio_stop_ticket.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "os/os_api.h"
#include "system/includes.h"
#include "sdk/lx_aec_limit.h"
#include <stdint.h>
#include <string.h>

#define TAG "XIAOLE"

#ifndef D_XIAOLE_MIC_ADC_CHANNEL
#define D_XIAOLE_MIC_ADC_CHANNEL CONFIG_AISP_MIC0_ADC_CHANNEL
#endif

#ifndef D_XIAOLE_REF_ADC_CHANNEL
#define D_XIAOLE_REF_ADC_CHANNEL 0U
#endif

#ifndef D_XIAOLE_MIC_GAIN
#define D_XIAOLE_MIC_GAIN CONFIG_AISP_MIC_ADC_GAIN
#endif

#ifndef D_XIAOLE_SAMPLE_RATE
#define D_XIAOLE_SAMPLE_RATE 16000U
#endif

#ifndef D_XIAOLE_MPO_DP
#define D_XIAOLE_MPO_DP 6
#endif

#ifndef D_XIAOLE_ENC_BUF_SIZE
#define D_XIAOLE_ENC_BUF_SIZE (8U * 1024U)
#endif

#ifndef D_XIAOLE_TASK_PRIO
#define D_XIAOLE_TASK_PRIO 11
#endif

#ifndef D_XIAOLE_TASK_STACK
#define D_XIAOLE_TASK_STACK (4U * 1024U)
#endif

#ifndef D_XIAOLE_TASK_Q_SIZE
#define D_XIAOLE_TASK_Q_SIZE 64
#endif

#ifndef D_XIAOLE_START_DELAY_TICKS
#define D_XIAOLE_START_DELAY_TICKS 20
#endif

#define XIAOLE_CMD_START 1U
#define XIAOLE_CMD_STOP 2U
#define XIAOLE_CMD_WAKEUP 3U
#define XIAOLE_CMD_EXIT 4U
#define XIAOLE_CMD_STOP_ACK 5U
#define XIAOLE_MAX_FRAME_SAMPLES 160U

typedef enum {
    XIAOLE_STATE_STOPPED = 0,
    XIAOLE_STATE_STARTING,
    XIAOLE_STATE_LISTENING,
    XIAOLE_STATE_STOPPING,
} xiaole_wakeup_state_e;

typedef struct {
    uint8_t is_initialized;
    uint8_t exit_req;
    uint8_t wakeup_pending;
    volatile xiaole_wakeup_state_e state;
    volatile d_err_t stop_result;
    uint8_t algorithm_started;
    volatile uint8_t algorithm_stop_failed;
    volatile uint8_t shutting_down;
    volatile uint32_t listening_generation;
    int task_pid;
    struct server *enc_server;
} xiaole_wakeup_ctx_t;

static xiaole_wakeup_ctx_t s_xiaole_ctx;
static d_audio_stop_ticket_t s_xiaole_stop;

static d_err_t xiaole_wakeup_post_cmd(uint32_t cmd, uint32_t generation);
static d_err_t xiaole_enc_stop_inner(void);

/**
 * @brief 算法检测有效且仍在监听时投递带代次的唤醒命令。
 * @param[in] data 算法数据，本实现不使用。
 * @param[in] len 算法数据长度，本实现不使用。
 * @param[in] type 检测类型，小于 1 时忽略。
 */
static void xiaole_wakeup_callback(void *data, int len, int type)
{
    uint32_t generation = s_xiaole_ctx.listening_generation;
    (void)data;
    (void)len;
    if (type < 1 || s_xiaole_ctx.state != XIAOLE_STATE_LISTENING || s_xiaole_ctx.shutting_down) {
        return;
    }

    s_xiaole_ctx.wakeup_pending = 1U;
    (void)xiaole_wakeup_post_cmd(XIAOLE_CMD_WAKEUP, generation);
}

/**
 * @brief 拆分交错参考与麦克风 PCM，并向算法提交有界采样。
 * @param[in] file VFS 兼容参数，本实现不使用。
 * @param[in] data 交错双通道 16 位 PCM，调用期间借用。
 * @param[in] len 输入字节数；本次最多处理 XIAOLE_MAX_FRAME_SAMPLES 个双通道样本。
 * @return 空数据或零长度返回 0；其余按 VFS 合同返回原 len。
 * @note 超过本地帧上限的尾部不送算法，返回值仍是原输入长度。
 */
static int xiaole_enc_vfs_fwrite(void *file, void *data, u32 len)
{
    short *pcm_data;
    short mic_buf[XIAOLE_MAX_FRAME_SAMPLES];
    short ref_buf[XIAOLE_MAX_FRAME_SAMPLES];
    uint32_t sample_count;
    uint32_t index;

    (void)file;
    if ((data == NULL) || (len == 0U)) {
        return 0;
    }

    pcm_data = (short *)data;
    sample_count = len / (2U * sizeof(short));
    if (sample_count > XIAOLE_MAX_FRAME_SAMPLES) {
        sample_count = XIAOLE_MAX_FRAME_SAMPLES;
    }

    for (index = 0U; index < sample_count; index++) {
        ref_buf[index] = pcm_data[index * 2U];
        mic_buf[index] = pcm_data[(index * 2U) + 1U];
    }

    (void)lx_aec_mic_data((char *)mic_buf, (int)(sample_count * sizeof(short)));
    (void)lx_aec_ref_data((char *)ref_buf, (int)(sample_count * sizeof(short)));
    return (int)len;
}

/**
 * @brief 响应静态 VFS 关闭，无独立文件资源。
 * @param[in] file 兼容参数，本实现不使用。
 * @return 固定返回 0。
 */
static int xiaole_enc_vfs_fclose(void *file)
{
    (void)file;
    return 0;
}

/**
 * @brief 为实时采样 VFS 返回无固定文件长度。
 * @param[in] file 兼容参数，本实现不使用。
 * @return 固定返回 0。
 */
static int xiaole_enc_vfs_flen(void *file)
{
    (void)file;
    return 0;
}

static const struct audio_vfs_ops s_xiaole_enc_vfs_ops = {
    .fwrite = xiaole_enc_vfs_fwrite,
    .fclose = xiaole_enc_vfs_fclose,
    .flen = xiaole_enc_vfs_flen,
};

/**
 * @brief 将编码器错误转换为小乐错误事件。
 * @param[in] priv 兼容上下文，本实现不使用。
 * @param[in] argc SDK 参数数量，当前未用于长度校验。
 * @param[in] argv 事件数组；为空时忽略。
 */
static void xiaole_enc_event_handler(void *priv, int argc, int *argv)
{
    (void)priv;
    (void)argc;
    if (argv == NULL) {
        return;
    }

    if (argv[0] == AUDIO_SERVER_EVENT_ERR) {
        LOGE(TAG, "enc event err");
        d_event_post(EVT_XIAOLE_WAKEUP_ERROR, NULL, 0);
    }
}

/**
 * @brief 打开可复用编码服务并注册错误回调。
 * @return 已有或打开成功返回 D_OK；打开失败返回 D_ERR_IO。
 */
static d_err_t xiaole_enc_open(void)
{
    if (s_xiaole_ctx.enc_server != NULL) {
        return D_OK;
    }

    s_xiaole_ctx.enc_server = server_open("audio_server", "enc");
    if (s_xiaole_ctx.enc_server == NULL) {
        LOGE(TAG, "open enc server failed");
        return D_ERR_IO;
    }

    server_register_event_handler(s_xiaole_ctx.enc_server, NULL, xiaole_enc_event_handler);
    return D_OK;
}

/**
 * @brief 由小乐任务初始化算法并打开双通道麦克风采样。
 * @param[in] generation 本次监听会话代次。
 * @return 监听已活动或启动成功返回 D_OK；状态冲突返回 D_ERR_BUSY；算法、编码器失败返回 D_ERR_IO。
 * @note 闭源库初始化失败可能留下部分资源，进入保留故障状态，不宣称已释放。
 */
static d_err_t xiaole_enc_start_inner(uint32_t generation)
{
    union audio_req req = {0};
    int err;

    if (s_xiaole_ctx.state == XIAOLE_STATE_LISTENING) {
        return D_OK;
    }
    if (s_xiaole_ctx.state != XIAOLE_STATE_STOPPED || s_xiaole_ctx.algorithm_stop_failed ||
        s_xiaole_ctx.shutting_down) return D_ERR_BUSY;

    /* 重复 START 不能改写正在运行的监听会话代次。 */
    s_xiaole_ctx.listening_generation = generation;
    s_xiaole_ctx.state = XIAOLE_STATE_STARTING;
    err = lx_aec_init(D_XIAOLE_MPO_DP, (void *)xiaole_wakeup_callback);
    if (err != 0) {
        LOGE(TAG, "lx init failed: %d", err);
        /* 闭源库在初始化出错时可能保留部分资源。 */
        s_xiaole_ctx.algorithm_stop_failed = 1U;
        s_xiaole_ctx.stop_result = D_ERR_IO;
        s_xiaole_ctx.state = XIAOLE_STATE_STOPPING;
        return D_ERR_IO;
    }
    s_xiaole_ctx.algorithm_started = 1U;

    if (xiaole_enc_open() != D_OK) {
        (void)xiaole_enc_stop_inner();
        return D_ERR_IO;
    }

    req.enc.cmd = AUDIO_ENC_OPEN;
    req.enc.channel = 2;
    req.enc.channel_bit_map = BIT(D_XIAOLE_REF_ADC_CHANNEL) | BIT(D_XIAOLE_MIC_ADC_CHANNEL);
    req.enc.volume = D_XIAOLE_MIC_GAIN;
    req.enc.output_buf = NULL;
    req.enc.output_buf_len = D_XIAOLE_ENC_BUF_SIZE;
    req.enc.sample_rate = D_XIAOLE_SAMPLE_RATE;
    req.enc.format = "pcm";
    req.enc.frame_size = D_XIAOLE_SAMPLE_RATE / 100U * 2U * 2U;
    req.enc.sample_source = "mic";
    req.enc.vfs_ops = &s_xiaole_enc_vfs_ops;
    req.enc.msec = 0;

    err = server_request(s_xiaole_ctx.enc_server, AUDIO_REQ_ENC, &req);
    if (err != 0) {
        LOGE(TAG, "enc start failed: %d", err);
        xiaole_enc_stop_inner();
        return D_ERR_IO;
    }

    s_xiaole_ctx.wakeup_pending = 0U;
    s_xiaole_ctx.state = XIAOLE_STATE_LISTENING;
    LOGI(TAG, "started mic=%u ref=%u sr=%u", (unsigned)D_XIAOLE_MIC_ADC_CHANNEL, (unsigned)D_XIAOLE_REF_ADC_CHANNEL, (unsigned)D_XIAOLE_SAMPLE_RATE);
    d_event_post(EVT_XIAOLE_WAKEUP_STARTED, NULL, 0);
    return D_OK;
}

/**
 * @brief 先关闭编码采样，再等待算法内部任务退出并释放延迟缓冲。
 * @return 已停止或完整收尾返回 D_OK；关闭或算法释放失败返回实际错误。
 * @note 失败保留 STOPPING 及相关资源；算法停止故障需保留至重启。
 */
static d_err_t xiaole_enc_stop_inner(void)
{
    union audio_req req = {0};
    d_err_t result;
    uint16_t released = 0U;

    if (s_xiaole_ctx.state == XIAOLE_STATE_STOPPED) {
        return D_OK;
    }

    s_xiaole_ctx.state = XIAOLE_STATE_STOPPING;
    s_xiaole_ctx.wakeup_pending = 0U;
    if (s_xiaole_ctx.algorithm_stop_failed) return D_ERR_IO;
    if (s_xiaole_ctx.enc_server != NULL) {
        req.enc.cmd = AUDIO_ENC_CLOSE;
        if (server_request(s_xiaole_ctx.enc_server, AUDIO_REQ_ENC, &req) != 0) {
            LOGE(TAG, "enc close failed; keeping audio resources");
            return D_ERR_IO;
        }
        server_close(s_xiaole_ctx.enc_server);
        s_xiaole_ctx.enc_server = NULL;
    }

    if (s_xiaole_ctx.algorithm_started) {
        result = xiaole_sdk_release_begin();
        if (result == D_OK && lx_aec_free() != 0) result = D_ERR_IO;
        if (result == D_OK) result = xiaole_sdk_release_complete(&released);
        if (result != D_OK) {
            s_xiaole_ctx.algorithm_stop_failed = 1U;
            LOGE(TAG, "lx close failed: %d; retaining memory until reboot", result);
            return result;
        }
        s_xiaole_ctx.algorithm_started = 0U;
    }
    s_xiaole_ctx.state = XIAOLE_STATE_STOPPED;
    d_event_post(EVT_XIAOLE_WAKEUP_STOPPED, NULL, 0);
    LOGI(TAG, "stopped; library buffers released after worker exit: %u", (unsigned)released);
    return D_OK;
}

/**
 * @brief 顺序执行启动、停止、唤醒与退出命令。
 * @param[in] cmd XIAOLE_CMD_* 命令。
 * @param[in] generation 命令绑定的会话代次。
 * @note 唤醒先成功停止监听才发布 DETECTED；STOP_ACK 在执行后发布票据完成确认。
 */
static void xiaole_handle_cmd(uint32_t cmd, uint32_t generation)
{
    switch (cmd) {
    case XIAOLE_CMD_START:
        os_time_dly(D_XIAOLE_START_DELAY_TICKS);
        (void)xiaole_enc_start_inner(generation);
        break;
    case XIAOLE_CMD_STOP:
        s_xiaole_ctx.stop_result = xiaole_enc_stop_inner();
        break;
    case XIAOLE_CMD_STOP_ACK:
        s_xiaole_ctx.stop_result = xiaole_enc_stop_inner();
        d_audio_stop_ticket_complete(&s_xiaole_stop, s_xiaole_stop.requested);
        break;
    case XIAOLE_CMD_WAKEUP:
        if (s_xiaole_ctx.wakeup_pending != 0U && s_xiaole_ctx.state == XIAOLE_STATE_LISTENING &&
            !s_xiaole_ctx.shutting_down && generation == s_xiaole_ctx.listening_generation) {
            LOGI(TAG, "wakeup detected");
            s_xiaole_ctx.stop_result = xiaole_enc_stop_inner();
            if (s_xiaole_ctx.stop_result == D_OK)
                d_event_post(EVT_XIAOLE_WAKEUP_DETECTED, &generation, sizeof(generation));
            else d_event_post(EVT_XIAOLE_WAKEUP_ERROR, NULL, 0);
        }
        break;
    case XIAOLE_CMD_EXIT:
        s_xiaole_ctx.stop_result = xiaole_enc_stop_inner();
        if (s_xiaole_ctx.stop_result == D_OK) s_xiaole_ctx.exit_req = 1U;
        break;
    default:
        break;
    }
}

/**
 * @brief 持续消费小乐命令，退出前执行最后一次停止。
 * @param[in] priv 任务兼容参数，本实现不使用。
 */
static void xiaole_wakeup_task(void *priv)
{
    int msg[8];
    uint32_t cmd;
    int err;

    (void)priv;
    LOGI(TAG, "task start");
    while (s_xiaole_ctx.exit_req == 0U) {
        err = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));
        if ((err == OS_TASKQ) && (msg[0] == Q_USER)) {
            cmd = (uint32_t)msg[1];
            xiaole_handle_cmd(cmd, (uint32_t)msg[2]);
        }
    }

    xiaole_enc_stop_inner();
    s_xiaole_ctx.task_pid = 0;
    LOGI(TAG, "task exit");
}

/**
 * @brief 向小乐工作队列提交命令与代次。
 * @param[in] cmd 目标命令。
 * @param[in] generation 命令绑定代次。
 * @return 投递成功返回 D_OK；未初始化返回 D_ERR_NOT_READY；队列满返回 D_ERR_FULL。
 */
static d_err_t xiaole_wakeup_post_cmd(uint32_t cmd, uint32_t generation)
{
    if (s_xiaole_ctx.is_initialized == 0U) {
        return D_ERR_NOT_READY;
    }

    if (os_taskq_post("xiaole_wakeup", 2, (int)cmd, (int)generation) != 0) {
        LOGW(TAG, "post cmd failed: %u", (unsigned)cmd);
        return D_ERR_FULL;
    }
    return D_OK;
}

/**
 * @brief 创建小乐唤醒工作任务，初始不占用麦克风；完整合同见 xiaole_wakeup.h。
 */
d_err_t xiaole_wakeup_init(void)
{
    if (s_xiaole_ctx.is_initialized != 0U) {
        return D_OK;
    }

    memset(&s_xiaole_ctx, 0, sizeof(s_xiaole_ctx));
    s_xiaole_ctx.state = XIAOLE_STATE_STOPPED;

    if (thread_fork("xiaole_wakeup", D_XIAOLE_TASK_PRIO, D_XIAOLE_TASK_STACK, D_XIAOLE_TASK_Q_SIZE, &s_xiaole_ctx.task_pid, xiaole_wakeup_task, NULL) != OS_NO_ERR) {
        memset(&s_xiaole_ctx, 0, sizeof(s_xiaole_ctx));
        return D_ERR_IO;
    }

    s_xiaole_ctx.is_initialized = 1U;
    LOGI(TAG, "init done");
    return D_OK;
}

/**
 * @brief 禁止新启动，受检停止后退出并等待工作任务；完整合同见 xiaole_wakeup.h。
 */
void xiaole_wakeup_deinit(void)
{
    if (s_xiaole_ctx.is_initialized == 0U) {
        return;
    }

    s_xiaole_ctx.shutting_down = 1U;
    if (xiaole_wakeup_stop_wait(2000U) != D_OK ||
        xiaole_wakeup_post_cmd(XIAOLE_CMD_EXIT, 0U) != D_OK) {
        LOGE(TAG, "deinit incomplete; keeping worker and resources");
        return;
    }
    if (s_xiaole_ctx.task_pid != 0) {
        thread_kill(&s_xiaole_ctx.task_pid, KILL_WAIT);
        s_xiaole_ctx.task_pid = 0;
    }
    memset(&s_xiaole_stop, 0, sizeof(s_xiaole_stop));
    memset(&s_xiaole_ctx, 0, sizeof(s_xiaole_ctx));
}

/**
 * @brief 受理指定会话代次的监听启动命令；完整合同见 xiaole_wakeup.h。
 */
d_err_t xiaole_wakeup_start(uint32_t generation)
{
    if (s_xiaole_ctx.shutting_down || s_xiaole_ctx.algorithm_stop_failed) return D_ERR_BUSY;
    return xiaole_wakeup_post_cmd(XIAOLE_CMD_START, generation);
}

/**
 * @brief 非阻塞投递停止监听命令；完整合同见 xiaole_wakeup.h。
 */
d_err_t xiaole_wakeup_stop(void)
{
    return xiaole_wakeup_post_cmd(XIAOLE_CMD_STOP, 0U);
}

/**
 * @brief 后台提交或加入唯一停止请求，并等待完成确认；完整合同见 xiaole_wakeup.h。
 */
d_err_t xiaole_wakeup_stop_wait(uint32_t timeout_ms)
{
    uint32_t start_ms;
    d_err_t result;

    if (!s_xiaole_ctx.is_initialized) return D_OK;
    if (!timeout_ms) return D_ERR_INVALID_ARG;
    (void)d_audio_stop_ticket_begin(&s_xiaole_stop);
    if (!s_xiaole_stop.submitted) {
        result = xiaole_wakeup_post_cmd(XIAOLE_CMD_STOP_ACK, 0U);
        if (result != D_OK) return result;
        d_audio_stop_ticket_submitted(&s_xiaole_stop);
    }
    start_ms = timer_get_ms();
    do {
        if (d_audio_stop_ticket_ready(&s_xiaole_stop)) {
            d_audio_stop_ticket_finish(&s_xiaole_stop);
            return s_xiaole_ctx.stop_result;
        }
        os_time_dly(1);
    } while ((uint32_t)(timer_get_ms() - start_ms) < timeout_ms);
    return D_ERR_TIMEOUT;
}

/**
 * @brief 读取监听是否处于 STARTING 或 LISTENING；完整合同见 xiaole_wakeup.h。
 */
int xiaole_wakeup_is_running(void)
{
    if ((s_xiaole_ctx.state == XIAOLE_STATE_STARTING) ||
        (s_xiaole_ctx.state == XIAOLE_STATE_LISTENING)) {
        return 1;
    }
    return 0;
}

#endif /* D_FEATURE_XIAOLE_WAKEUP */
