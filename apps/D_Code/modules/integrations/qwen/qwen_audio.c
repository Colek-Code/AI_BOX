#include "config/app_config.h"

#if D_FEATURE_QWEN

#include "qwen_audio.h"
#include "modules/services/audio/d_audio_stop_ticket.h"
#include "modules/services/audio/d_audio_shared_stop.h"
#include "audio_input.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "os/os_api.h"
#include "sys_common.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "generic/circular_buf.h"

#include <stdint.h>
#include <string.h>

#define TAG "QWEN_AUD"
#define QWEN_AUDIO_TASK_PRIO 11
#define QWEN_AUDIO_TASK_STACK (4U * 1024U)
#define QWEN_AUDIO_PLAYER_TASK_PRIO 11
#define QWEN_AUDIO_PLAYER_TASK_STACK (4U * 1024U)
#define QWEN_AUDIO_CMD_START_RECORDER 1U
#define QWEN_AUDIO_CMD_STOP_RECORDER 2U
#define QWEN_AUDIO_CMD_START_PLAYER 4U
#define QWEN_AUDIO_CMD_STOP_PLAYER 5U
#define QWEN_AUDIO_CMD_FINISH_PLAYER 6U
#define QWEN_AUDIO_CMD_EXIT 7U
#define QWEN_AUDIO_CMD_PREPARE_RECORDER 8U
#define QWEN_AUDIO_CMD_STOP_RECORDER_ACK 9U
#define QWEN_AUDIO_CMD_STOP_PLAYER_ACK 10U
#define QWEN_AUDIO_REC_CHUNK 1280U
#define QWEN_AUDIO_REC_PREBUF_SIZE (32U * 1024U)
#define QWEN_AUDIO_PLAYER_OUTPUT_BUF_SIZE (2U * 1024U)
#define QWEN_AUDIO_PLAYER_PUMP_CHUNK (2U * 1024U)
#define QWEN_AUDIO_PLAYER_CBUF_SIZE (1024U * 1024U)
#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
#define QWEN_AUDIO_PLAYER_START_THRESHOLD (D_QWEN_DS_OPUS_PKT_LEN * 3U)
#define QWEN_AUDIO_PLAYER_FEED_CHUNK D_QWEN_DS_OPUS_PKT_LEN
#define QWEN_AUDIO_PLAYER_FEED_INTERVAL_MS D_QWEN_DS_OPUS_FRAME_MS
#define QWEN_AUDIO_PLAYER_TAIL_DRAIN_MS 600U
#define QWEN_AUDIO_PLAYER_FORMAT_NAME "raw_opus"
#else
#define QWEN_AUDIO_PLAYER_START_THRESHOLD 7200U
#define QWEN_AUDIO_PLAYER_FEED_CHUNK 320U
#define QWEN_AUDIO_PLAYER_FEED_INTERVAL_MS 20U
#define QWEN_AUDIO_PLAYER_TAIL_DRAIN_MS 300U
#define QWEN_AUDIO_PLAYER_FORMAT_NAME "pcm"
#endif
#define QWEN_AUDIO_PLAYER_WAIT_MS 10U
#define QWEN_AUDIO_SWITCH_WAIT_TICKS 30U
#define QWEN_AUDIO_READY_CUE_WARMUP_TICKS 180U
#define QWEN_AUDIO_PLAYER_COLD_WARMUP_MS 120U
#define QWEN_AUDIO_PLAYER_CHANNEL 1U
#define QWEN_AUDIO_STATS_INTERVAL_MS 1000U
#define QWEN_AUDIO_RECORDER_READY_WAIT_TICKS 300U
#ifdef D_QWEN_PLAY_VOLUME
#define QWEN_AUDIO_PLAY_VOLUME D_QWEN_PLAY_VOLUME
#else
#define QWEN_AUDIO_PLAY_VOLUME 80U
#endif
#define QWEN_AUDIO_VOLUME_MIN 0U
#define QWEN_AUDIO_VOLUME_MAX 100U

uint32_t c_mmi_put_recorder_data(uint8_t *data, uint32_t size);
uint32_t c_mmi_get_player_data(uint8_t *data, uint32_t size);
uint8_t c_mmi_audio_recv_all(void);
bool is_audio_play_open(void);
bool is_audio_play_stop_pending(void);

#ifdef D_QWEN_TRACE_ENABLE
#define QWEN_TRACE(fmt, ...) LOGD(TAG, fmt, ##__VA_ARGS__)
#else
#define QWEN_TRACE(fmt, ...) do { } while (0)
#endif

typedef enum {
    QWEN_AUDIO_RECORDER_IDLE = 0,
    QWEN_AUDIO_RECORDER_PREPARING,
    QWEN_AUDIO_RECORDER_PREBUFFERING,
    QWEN_AUDIO_RECORDER_FEEDING,
    QWEN_AUDIO_RECORDER_STOPPING,
} qwen_audio_recorder_state_e;

typedef struct {
    uint8_t is_initialized;
    uint8_t exit_req;
    qwen_audio_recorder_state_e recorder_state;
    volatile uint8_t recorder_stop_requested;
    uint8_t player_running;
    uint8_t player_feed_enabled;
    uint8_t player_decoder_started;
    uint8_t player_stream_ended;
    uint8_t recorder_first_packet_logged;
    uint8_t recorder_capture_first_packet_logged;
    uint8_t player_first_packet_logged;
    uint8_t player_cold_warmup_done;
    int task_pid;
    int player_task_pid;
    OS_QUEUE msg_q;
    OS_QUEUE player_msg_q;
    struct server *dec_server;
    cbuffer_t recorder_pre_cbuf;
    cbuffer_t player_cbuf;
    uint8_t recorder_buf[QWEN_AUDIO_REC_CHUNK];
    uint8_t recorder_pre_cbuf_mem[QWEN_AUDIO_REC_PREBUF_SIZE];
    uint8_t player_buf[QWEN_AUDIO_PLAYER_PUMP_CHUNK];
    uint8_t player_cbuf_mem[QWEN_AUDIO_PLAYER_CBUF_SIZE];
    uint32_t recorder_bytes;
    uint32_t recorder_pre_bytes;
    uint32_t recorder_drop_bytes;
    uint32_t player_bytes;
    uint32_t recorder_empty_reads;
    uint32_t player_empty_reads;
    uint32_t player_full_writes;
    uint32_t last_stats_ms;
    uint32_t player_last_pump_ms;
    uint32_t player_last_progress_ms;
    uint32_t player_tail_deadline_ms;
    uint32_t player_warmup_deadline_ms;
    uint8_t player_volume_percent;
    uint8_t player_restore_volume_percent;
    uint8_t is_player_muted;
} qwen_audio_context_t;

static qwen_audio_context_t s_qwen_audio;
static d_audio_stop_ticket_t s_recorder_stop;
static d_audio_stop_ticket_t s_player_stop;
static volatile uint32_t s_player_generation;
static void qwen_audio_finish_player_inner(void);
static void qwen_audio_stop_player_inner(void);
static d_err_t qwen_audio_start_player_inner(void);
static d_err_t qwen_audio_prepare_recorder_inner(void);
static void qwen_audio_stop_recorder_inner(void);
static void qwen_audio_post_recorder_cmd(uint32_t cmd);
static void qwen_audio_post_player_cmd(uint32_t cmd);
static void qwen_audio_wait_player_idle(uint32_t wait_ticks);
static void qwen_audio_wait_recorder_idle(uint32_t wait_ticks);
static void qwen_audio_wait_player_ready(uint32_t wait_ticks);
static d_err_t qwen_audio_wait_recorder_ready(uint32_t wait_ticks);
static d_err_t qwen_audio_wait_recorder_hw_ready(uint32_t wait_ticks);

static const char *qwen_audio_recorder_state_name(qwen_audio_recorder_state_e recorder_state)
{
    switch (recorder_state) {
    case QWEN_AUDIO_RECORDER_IDLE:
        return "IDLE";
    case QWEN_AUDIO_RECORDER_PREPARING:
        return "PREPARING";
    case QWEN_AUDIO_RECORDER_PREBUFFERING:
        return "PREBUFFERING";
    case QWEN_AUDIO_RECORDER_FEEDING:
        return "FEEDING";
    case QWEN_AUDIO_RECORDER_STOPPING:
        return "STOPPING";
    default:
        return "UNKNOWN";
    }
}

static void qwen_audio_set_recorder_state(qwen_audio_recorder_state_e recorder_state, const char *reason)
{
    qwen_audio_recorder_state_e old_state;

    if (s_qwen_audio.recorder_state == recorder_state) {
        return;
    }

    old_state = s_qwen_audio.recorder_state;
    s_qwen_audio.recorder_state = recorder_state;
    LOGI(TAG, "recorder state %s -> %s reason=%s",
         qwen_audio_recorder_state_name(old_state),
         qwen_audio_recorder_state_name(recorder_state),
         (reason != NULL) ? reason : "unknown");
}

static uint8_t qwen_audio_recorder_is_active(void)
{
    return (s_qwen_audio.recorder_state != QWEN_AUDIO_RECORDER_IDLE) &&
           (s_qwen_audio.recorder_state != QWEN_AUDIO_RECORDER_STOPPING);
}

static uint8_t qwen_audio_recorder_can_feed_sdk(void)
{
    if (s_qwen_audio.recorder_stop_requested != 0U) {
        return 0U;
    }

    return (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_FEEDING) ? 1U : 0U;
}

static void qwen_audio_mark_player_progress(void)
{
    s_qwen_audio.player_last_progress_ms = (uint32_t)timer_get_ms();
}

static uint8_t qwen_audio_recorder_is_ready(void)
{
    return (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_PREBUFFERING) ||
           (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_FEEDING);
}

static uint8_t qwen_audio_clamp_volume(uint8_t volume_percent)
{
    if (volume_percent > QWEN_AUDIO_VOLUME_MAX) {
        return QWEN_AUDIO_VOLUME_MAX;
    }

    return volume_percent;
}

static uint8_t qwen_audio_player_hw_open(void)
{
    if (is_audio_play_stop_pending() != 0) {
        return 0U;
    }

    return (is_audio_play_open() != 0) ? 1U : 0U;
}

static uint32_t qwen_audio_calc_player_ms(uint32_t player_bytes)
{
#if (D_QWEN_DS_AUDIO_FORMAT == D_QWEN_AUDIO_FORMAT_RAW_OPUS)
    return (uint32_t)(((uint64_t)player_bytes * D_QWEN_DS_OPUS_FRAME_MS) / D_QWEN_DS_OPUS_PKT_LEN);
#else
    return (uint32_t)(((uint64_t)player_bytes * 1000U) / (D_QWEN_DS_SAMPLE_RATE * 2U * QWEN_AUDIO_PLAYER_CHANNEL));
#endif
}

static void qwen_audio_write_recorder_sdk(uint8_t *recorder_data, uint32_t data_len)
{
    uint32_t write_len;

    if ((recorder_data == NULL) || (data_len == 0U)) {
        return;
    }

    write_len = c_mmi_put_recorder_data(recorder_data, data_len);
    s_qwen_audio.recorder_bytes += write_len;
    if (s_qwen_audio.recorder_first_packet_logged == 0U) {
        s_qwen_audio.recorder_first_packet_logged = 1U;
        LOGI(TAG, "recorder first packet=%u", (unsigned)write_len);
    }
    if (write_len != data_len) {
        LOGW(TAG, "recorder write short: in=%u out=%u", (unsigned)data_len, (unsigned)write_len);
    }
}

static void qwen_audio_write_recorder_prebuffer(uint8_t *recorder_data, uint32_t data_len)
{
    uint32_t cache_len;
    uint32_t write_len;
    uint8_t *write_data;

    if ((recorder_data == NULL) || (data_len == 0U)) {
        return;
    }

    write_len = data_len;
    write_data = recorder_data;
    if (write_len > QWEN_AUDIO_REC_PREBUF_SIZE) {
        s_qwen_audio.recorder_drop_bytes += write_len - QWEN_AUDIO_REC_PREBUF_SIZE;
        write_data = &recorder_data[write_len - QWEN_AUDIO_REC_PREBUF_SIZE];
        write_len = QWEN_AUDIO_REC_PREBUF_SIZE;
    }

    cache_len = cbuf_get_data_size(&s_qwen_audio.recorder_pre_cbuf);
    if ((cache_len + write_len) > QWEN_AUDIO_REC_PREBUF_SIZE) {
        s_qwen_audio.recorder_drop_bytes += cache_len;
        cbuf_clear(&s_qwen_audio.recorder_pre_cbuf);
    }

    if (cbuf_write(&s_qwen_audio.recorder_pre_cbuf, write_data, write_len) == write_len) {
        s_qwen_audio.recorder_pre_bytes += write_len;
    } else {
        s_qwen_audio.recorder_drop_bytes += write_len;
        cbuf_clear(&s_qwen_audio.recorder_pre_cbuf);
    }
}

static void qwen_audio_flush_recorder_prebuffer(void)
{
    uint32_t cache_len;
    uint32_t read_len;

    cache_len = cbuf_get_data_size(&s_qwen_audio.recorder_pre_cbuf);
    if (cache_len == 0U) {
        return;
    }

    LOGI(TAG, "recorder feed cache=%u drop=%u",
         (unsigned)cache_len,
         (unsigned)s_qwen_audio.recorder_drop_bytes);
    while (cache_len > 0U) {
        read_len = cache_len;
        if (read_len > sizeof(s_qwen_audio.recorder_buf)) {
            read_len = sizeof(s_qwen_audio.recorder_buf);
        }
        read_len = cbuf_read(&s_qwen_audio.recorder_pre_cbuf, s_qwen_audio.recorder_buf, read_len);
        if (read_len == 0U) {
            break;
        }
        qwen_audio_write_recorder_sdk(s_qwen_audio.recorder_buf, read_len);
        cache_len = cbuf_get_data_size(&s_qwen_audio.recorder_pre_cbuf);
    }
}

static void qwen_audio_log_stats_if_needed(void)
{
    uint32_t now_ms;

    if ((s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_IDLE) && (s_qwen_audio.player_running == 0U)) {
        return;
    }

    now_ms = timer_get_ms();
    if ((s_qwen_audio.last_stats_ms != 0U) &&
        ((uint32_t)(now_ms - s_qwen_audio.last_stats_ms) < QWEN_AUDIO_STATS_INTERVAL_MS)) {
        return;
    }

    s_qwen_audio.last_stats_ms = now_ms;
    QWEN_TRACE("stats rec_state=%s play_run=%u rec_bytes=%u rec_pre=%u rec_drop=%u play_bytes=%u rec_empty=%u play_empty=%u play_full=%u",
               qwen_audio_recorder_state_name(s_qwen_audio.recorder_state),
               (unsigned)s_qwen_audio.player_running,
               (unsigned)s_qwen_audio.recorder_bytes,
               (unsigned)cbuf_get_data_size(&s_qwen_audio.recorder_pre_cbuf),
               (unsigned)s_qwen_audio.recorder_drop_bytes,
               (unsigned)s_qwen_audio.player_bytes,
               (unsigned)s_qwen_audio.recorder_empty_reads,
               (unsigned)s_qwen_audio.player_empty_reads,
               (unsigned)s_qwen_audio.player_full_writes);
}

static void qwen_audio_finish_player_inner(void)
{
    if (s_qwen_audio.player_running == 0U) {
        return;
    }

    s_qwen_audio.player_stream_ended = 1U;
    s_qwen_audio.player_tail_deadline_ms = 0U;
    qwen_audio_mark_player_progress();
    LOGI(TAG, "player stream end");
}

static void qwen_audio_stop_player_inner(void)
{
    uint8_t player_open;

    s_player_generation++;
    player_open = qwen_audio_player_hw_open();
    if ((s_qwen_audio.player_running == 0U) && (player_open == 0U)) {
        return;
    }

    s_qwen_audio.player_running = 0U;
    s_qwen_audio.player_feed_enabled = 0U;
    s_qwen_audio.player_decoder_started = 0U;
    s_qwen_audio.player_stream_ended = 0U;
    s_qwen_audio.player_last_pump_ms = 0U;
    s_qwen_audio.player_last_progress_ms = 0U;
    s_qwen_audio.player_tail_deadline_ms = 0U;
    s_qwen_audio.player_warmup_deadline_ms = 0U;
    cbuf_clear(&s_qwen_audio.player_cbuf);
    if (player_open != 0U) {
        _device_rbuf_clear();
        _device_net_audio_play(false);
    }
    LOGI(TAG, "player stopped");
}

static d_err_t qwen_audio_start_player_inner(void)
{
    uint8_t player_open;

    if (s_qwen_audio.player_running != 0U) {
        return D_OK;
    }
    s_player_generation++;

#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK)
    if (s_qwen_audio.recorder_state != QWEN_AUDIO_RECORDER_IDLE) {
        s_qwen_audio.recorder_stop_requested = 1U;
        qwen_audio_post_recorder_cmd(QWEN_AUDIO_CMD_STOP_RECORDER);
        qwen_audio_wait_recorder_idle(QWEN_AUDIO_SWITCH_WAIT_TICKS);
    }
#endif

#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_DUPLEX)
    if (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_IDLE) {
        audio_stream_init(D_QWEN_DS_SAMPLE_RATE, 16, 1);
    }
#else
    audio_stream_init(D_QWEN_DS_SAMPLE_RATE, 16, 1);
#endif
    player_open = qwen_audio_player_hw_open();
    if (player_open == 0U) {
        _device_net_audio_play(true);
    } else {
        _device_rbuf_clear();
    }

    cbuf_clear(&s_qwen_audio.player_cbuf);
    s_qwen_audio.player_running = 1U;
    s_qwen_audio.player_feed_enabled = 1U;
    s_qwen_audio.player_decoder_started = 0U;
    s_qwen_audio.player_stream_ended = 0U;
    s_qwen_audio.player_first_packet_logged = 0U;
    s_qwen_audio.player_bytes = 0U;
    s_qwen_audio.player_empty_reads = 0U;
    s_qwen_audio.player_full_writes = 0U;
    s_qwen_audio.player_last_pump_ms = 0U;
    qwen_audio_mark_player_progress();
    s_qwen_audio.player_tail_deadline_ms = 0U;
    s_qwen_audio.player_warmup_deadline_ms = 0U;
    if ((player_open == 0U) && (s_qwen_audio.player_cold_warmup_done == 0U)) {
        s_qwen_audio.player_warmup_deadline_ms = timer_get_ms() + QWEN_AUDIO_PLAYER_COLD_WARMUP_MS;
    }
    LOGI(TAG, "player start req open=%u fmt=%s sr=%u cbuf=%u start=%u feed=%u/%u",
         (unsigned)player_open,
         QWEN_AUDIO_PLAYER_FORMAT_NAME,
         (unsigned)D_QWEN_DS_SAMPLE_RATE,
         (unsigned)QWEN_AUDIO_PLAYER_CBUF_SIZE,
         (unsigned)QWEN_AUDIO_PLAYER_START_THRESHOLD,
         (unsigned)QWEN_AUDIO_PLAYER_FEED_CHUNK,
         (unsigned)QWEN_AUDIO_PLAYER_FEED_INTERVAL_MS);
    return D_OK;
}

static d_err_t qwen_audio_prepare_recorder_inner(void)
{
    uint8_t should_stop_player;

    if (s_qwen_audio.recorder_state != QWEN_AUDIO_RECORDER_IDLE) {
        return D_OK;
    }

    s_qwen_audio.recorder_stop_requested = 0U;
    qwen_audio_set_recorder_state(QWEN_AUDIO_RECORDER_PREPARING, "prepare");
    should_stop_player = 0U;
#if (D_QWEN_DIALOG_MODE == D_QWEN_DIALOG_MODE_PUSH2TALK)
    if ((s_qwen_audio.player_running != 0U) ||
        (s_qwen_audio.player_stream_ended != 0U) ||
        (qwen_audio_player_hw_open() != 0U)) {
        should_stop_player = 1U;
        s_qwen_audio.player_running = 0U;
        s_qwen_audio.player_feed_enabled = 0U;
        s_qwen_audio.player_decoder_started = 0U;
        s_qwen_audio.player_stream_ended = 0U;
        s_qwen_audio.player_last_pump_ms = 0U;
        s_qwen_audio.player_last_progress_ms = 0U;
        s_qwen_audio.player_tail_deadline_ms = 0U;
        cbuf_clear(&s_qwen_audio.player_cbuf);
        _device_rbuf_clear();
    }
#endif

    audio_stream_init(D_QWEN_US_SAMPLE_RATE, 16, 1);
    _device_wbuf_clear();
    cbuf_clear(&s_qwen_audio.recorder_pre_cbuf);
    s_qwen_audio.recorder_first_packet_logged = 0U;
    s_qwen_audio.recorder_capture_first_packet_logged = 0U;
    s_qwen_audio.recorder_bytes = 0U;
    s_qwen_audio.recorder_pre_bytes = 0U;
    s_qwen_audio.recorder_drop_bytes = 0U;
    s_qwen_audio.recorder_empty_reads = 0U;
    _device_net_audio_recorder(true);
    if (qwen_audio_wait_recorder_hw_ready(QWEN_AUDIO_RECORDER_READY_WAIT_TICKS) != D_OK) {
        _device_net_audio_recorder(false);
        qwen_audio_set_recorder_state(QWEN_AUDIO_RECORDER_IDLE, "hw_ready_timeout");
        LOGW(TAG, "recorder hw ready timeout");
        return D_ERR_TIMEOUT;
    }
    qwen_audio_set_recorder_state(QWEN_AUDIO_RECORDER_PREBUFFERING, "hw_ready");
    LOGI(TAG, "recorder prestart");
    if (should_stop_player != 0U) {
        _device_net_audio_play(false);
        LOGI(TAG, "player interrupted for recorder");
    }
    return D_OK;
}

static void qwen_audio_start_recorder_inner(void)
{
    if (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_IDLE) {
        if (qwen_audio_prepare_recorder_inner() != D_OK) {
            return;
        }
    }
    if (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_IDLE) {
        return;
    }

    if (s_qwen_audio.recorder_stop_requested != 0U) {
        return;
    }

    qwen_audio_set_recorder_state(QWEN_AUDIO_RECORDER_FEEDING, "start");
    LOGI(TAG, "recorder started cache=%u",
         (unsigned)cbuf_get_data_size(&s_qwen_audio.recorder_pre_cbuf));
}

static void qwen_audio_stop_recorder_inner(void)
{
    if (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_IDLE) {
        s_qwen_audio.recorder_stop_requested = 0U;
        return;
    }

    qwen_audio_set_recorder_state(QWEN_AUDIO_RECORDER_STOPPING, "stop");
    s_qwen_audio.recorder_stop_requested = 0U;
    _device_net_audio_recorder(false);
    _device_wbuf_clear();
    cbuf_clear(&s_qwen_audio.recorder_pre_cbuf);
    qwen_audio_set_recorder_state(QWEN_AUDIO_RECORDER_IDLE, "stopped");
    LOGI(TAG, "recorder stopped bytes=%u pre=%u drop=%u empty=%u",
         (unsigned)s_qwen_audio.recorder_bytes,
         (unsigned)s_qwen_audio.recorder_pre_bytes,
         (unsigned)s_qwen_audio.recorder_drop_bytes,
         (unsigned)s_qwen_audio.recorder_empty_reads);
}

static void qwen_audio_wait_player_idle(uint32_t wait_ticks)
{
    uint32_t tick;

    for (tick = 0U; tick < wait_ticks; tick++) {
        if ((s_qwen_audio.player_running == 0U) && (qwen_audio_player_hw_open() == 0U)) {
            return;
        }
        os_time_dly(1);
    }

    LOGW(TAG, "wait player idle timeout: run=%u open=%u",
         (unsigned)s_qwen_audio.player_running,
         (unsigned)qwen_audio_player_hw_open());
}

static void qwen_audio_wait_recorder_idle(uint32_t wait_ticks)
{
    uint32_t tick;

    for (tick = 0U; tick < wait_ticks; tick++) {
        if (s_qwen_audio.recorder_state == QWEN_AUDIO_RECORDER_IDLE) {
            return;
        }
        os_time_dly(1);
    }

    LOGW(TAG, "wait recorder idle timeout: state=%s",
         qwen_audio_recorder_state_name(s_qwen_audio.recorder_state));
}

static void qwen_audio_wait_player_ready(uint32_t wait_ticks)
{
    uint32_t tick;

    for (tick = 0U; tick < wait_ticks; tick++) {
        if (qwen_audio_player_hw_open() != 0U) {
            return;
        }
        os_time_dly(1);
    }

    LOGW(TAG, "wait player ready timeout: run=%u open=%u",
         (unsigned)s_qwen_audio.player_running,
         (unsigned)qwen_audio_player_hw_open());
}

static d_err_t qwen_audio_wait_recorder_ready(uint32_t wait_ticks)
{
    uint32_t tick;

    for (tick = 0U; tick < wait_ticks; tick++) {
        if (qwen_audio_recorder_is_ready() != 0U) {
            return D_OK;
        }
        os_time_dly(1);
    }

    LOGW(TAG, "wait recorder ready timeout: state=%s hw=%u",
         qwen_audio_recorder_state_name(s_qwen_audio.recorder_state),
         (unsigned)is_audio_record_open());
    return D_ERR_TIMEOUT;
}

static d_err_t qwen_audio_wait_recorder_hw_ready(uint32_t wait_ticks)
{
    uint32_t tick;

    for (tick = 0U; tick < wait_ticks; tick++) {
        if (is_audio_record_open() != 0) {
            return D_OK;
        }
        os_time_dly(1);
    }

    return D_ERR_TIMEOUT;
}

static void qwen_audio_handle_recorder_cmd(uint32_t cmd)
{
    switch (cmd) {
    case QWEN_AUDIO_CMD_PREPARE_RECORDER:
        (void)qwen_audio_prepare_recorder_inner();
        break;
    case QWEN_AUDIO_CMD_START_RECORDER:
        qwen_audio_start_recorder_inner();
        break;
    case QWEN_AUDIO_CMD_STOP_RECORDER:
        qwen_audio_stop_recorder_inner();
        break;
    case QWEN_AUDIO_CMD_STOP_RECORDER_ACK:
        qwen_audio_stop_recorder_inner();
        d_audio_stop_ticket_complete(&s_recorder_stop, s_recorder_stop.requested);
        break;
    case QWEN_AUDIO_CMD_EXIT:
        s_qwen_audio.exit_req = 1U;
        break;
    default:
        break;
    }
}

static void qwen_audio_handle_player_cmd(uint32_t cmd)
{
    switch (cmd) {
    case QWEN_AUDIO_CMD_START_PLAYER:
        (void)qwen_audio_start_player_inner();
        break;
    case QWEN_AUDIO_CMD_STOP_PLAYER:
        qwen_audio_stop_player_inner();
        break;
    case QWEN_AUDIO_CMD_STOP_PLAYER_ACK:
        qwen_audio_stop_player_inner();
        d_audio_stop_ticket_complete(&s_player_stop, s_player_stop.requested);
        break;
    case QWEN_AUDIO_CMD_FINISH_PLAYER:
        qwen_audio_finish_player_inner();
        break;
    case QWEN_AUDIO_CMD_EXIT:
        s_qwen_audio.exit_req = 1U;
        break;
    default:
        break;
    }
}

static void qwen_audio_pump_recorder(void)
{
    uint32_t read_len;

    if (qwen_audio_recorder_is_active() == 0U) {
        return;
    }

    if (qwen_audio_recorder_can_feed_sdk() != 0U) {
        qwen_audio_flush_recorder_prebuffer();
    }

    read_len = (uint32_t)_device_get_voice_data(s_qwen_audio.recorder_buf, sizeof(s_qwen_audio.recorder_buf));
    if (read_len == 0U) {
        s_qwen_audio.recorder_empty_reads++;
        return;
    }

    if (s_qwen_audio.recorder_capture_first_packet_logged == 0U) {
        s_qwen_audio.recorder_capture_first_packet_logged = 1U;
        LOGI(TAG, "recorder first capture=%u state=%s",
             (unsigned)read_len,
             qwen_audio_recorder_state_name(s_qwen_audio.recorder_state));
    }

    if (qwen_audio_recorder_can_feed_sdk() == 0U) {
        qwen_audio_write_recorder_prebuffer(s_qwen_audio.recorder_buf, read_len);
        return;
    }

    qwen_audio_write_recorder_sdk(s_qwen_audio.recorder_buf, read_len);
}

static void qwen_audio_pump_player(void)
{
    uint32_t now_ms;
    uint32_t cache_len;
    uint32_t space_len;
    uint32_t read_len;
    uint32_t feed_len;
    int write_len;

    if ((s_qwen_audio.player_running == 0U) || (s_qwen_audio.player_feed_enabled == 0U)) {
        return;
    }

    while (1) {
        cache_len = cbuf_get_data_size(&s_qwen_audio.player_cbuf);
        if (cache_len >= QWEN_AUDIO_PLAYER_CBUF_SIZE) {
            s_qwen_audio.player_full_writes++;
            break;
        }

        space_len = QWEN_AUDIO_PLAYER_CBUF_SIZE - cache_len;
        if (space_len > sizeof(s_qwen_audio.player_buf)) {
            space_len = sizeof(s_qwen_audio.player_buf);
        }

        read_len = c_mmi_get_player_data(s_qwen_audio.player_buf, space_len);
        if (read_len == 0U) {
            break;
        }

        if (cbuf_write(&s_qwen_audio.player_cbuf, s_qwen_audio.player_buf, read_len) != read_len) {
            s_qwen_audio.player_full_writes++;
            break;
        }
        qwen_audio_mark_player_progress();
    }

    cache_len = cbuf_get_data_size(&s_qwen_audio.player_cbuf);

    if (s_qwen_audio.player_decoder_started == 0U) {
        if ((cache_len >= QWEN_AUDIO_PLAYER_START_THRESHOLD) ||
            ((s_qwen_audio.player_stream_ended != 0U) && (cache_len > 0U))) {
            s_qwen_audio.player_decoder_started = 1U;
            LOGI(TAG, "player open cache=%u end=%u",
                 (unsigned)cache_len,
                 (unsigned)s_qwen_audio.player_stream_ended);
        } else {
            return;
        }
    }

    if (qwen_audio_player_hw_open() == 0U) {
        return;
    }

    now_ms = timer_get_ms();
    if (s_qwen_audio.player_warmup_deadline_ms != 0U) {
        if ((int32_t)(now_ms - s_qwen_audio.player_warmup_deadline_ms) < 0) {
            return;
        }
        s_qwen_audio.player_warmup_deadline_ms = 0U;
        s_qwen_audio.player_cold_warmup_done = 1U;
        LOGI(TAG, "player cold warmup done cache=%u", (unsigned)cbuf_get_data_size(&s_qwen_audio.player_cbuf));
    }

    if ((s_qwen_audio.player_last_pump_ms != 0U) &&
        ((uint32_t)(now_ms - s_qwen_audio.player_last_pump_ms) < QWEN_AUDIO_PLAYER_FEED_INTERVAL_MS)) {
        return;
    }

    s_qwen_audio.player_last_pump_ms = now_ms;
    cache_len = cbuf_get_data_size(&s_qwen_audio.player_cbuf);
    if (cache_len == 0U) {
        if (s_qwen_audio.player_stream_ended != 0U) {
            if (s_qwen_audio.player_tail_deadline_ms == 0U) {
                s_qwen_audio.player_tail_deadline_ms = now_ms + QWEN_AUDIO_PLAYER_TAIL_DRAIN_MS;
            } else if ((int32_t)(now_ms - s_qwen_audio.player_tail_deadline_ms) >= 0) {
                s_qwen_audio.player_running = 0U;
                s_qwen_audio.player_feed_enabled = 0U;
                s_qwen_audio.player_decoder_started = 0U;
                s_qwen_audio.player_stream_ended = 0U;
                s_qwen_audio.player_last_pump_ms = 0U;
                s_qwen_audio.player_last_progress_ms = 0U;
                s_qwen_audio.player_tail_deadline_ms = 0U;
                s_qwen_audio.player_warmup_deadline_ms = 0U;
                cbuf_clear(&s_qwen_audio.player_cbuf);
                _device_rbuf_clear();
                _device_net_audio_play(false);
                LOGI(TAG, "player drained fmt=%s bytes=%u play_ms=%u empty=%u full=%u",
                     QWEN_AUDIO_PLAYER_FORMAT_NAME,
                     (unsigned)s_qwen_audio.player_bytes,
                     (unsigned)qwen_audio_calc_player_ms(s_qwen_audio.player_bytes),
                     (unsigned)s_qwen_audio.player_empty_reads,
                     (unsigned)s_qwen_audio.player_full_writes);
                {
                    uint32_t generation = s_player_generation;
                    d_event_post(EVT_QWEN_TTS_LOCAL_END, &generation, sizeof(generation));
                }
            }
        } else {
            s_qwen_audio.player_empty_reads++;
        }
        return;
    }

    s_qwen_audio.player_tail_deadline_ms = 0U;
    feed_len = cache_len;
    if (feed_len > QWEN_AUDIO_PLAYER_FEED_CHUNK) {
        feed_len = QWEN_AUDIO_PLAYER_FEED_CHUNK;
    }

    read_len = cbuf_read(&s_qwen_audio.player_cbuf, s_qwen_audio.player_buf, feed_len);
    if (read_len == 0U) {
        s_qwen_audio.player_empty_reads++;
        return;
    }

    write_len = _device_write_voice_data(s_qwen_audio.player_buf, read_len);
    if ((write_len <= 0) || ((uint32_t)write_len != read_len)) {
        s_qwen_audio.player_full_writes++;
        LOGW(TAG, "player device write short: req=%u write=%d",
             (unsigned)read_len,
             write_len);
    }
    if (write_len > 0) {
        s_qwen_audio.player_bytes += (uint32_t)write_len;
        qwen_audio_mark_player_progress();
    }
    if (s_qwen_audio.player_first_packet_logged == 0U) {
        s_qwen_audio.player_first_packet_logged = 1U;
        LOGI(TAG, "player first packet=%d", write_len);
    }
}

static void qwen_audio_player_task(void *priv)
{
    void *msg;
    int ret;
    uint32_t cmd;
    int timeout;

    (void)priv;
    LOGI(TAG, "player task start");
    while (s_qwen_audio.exit_req == 0U) {
        timeout = (s_qwen_audio.player_running != 0U) ? 1 : 0;
        msg = NULL;
        ret = os_q_pend(&s_qwen_audio.player_msg_q, timeout, &msg);
        if (ret == 0) {
            cmd = (uint32_t)(uintptr_t)msg;
            QWEN_TRACE("player cmd=%u", (unsigned)cmd);
            qwen_audio_handle_player_cmd(cmd);
        }
        qwen_audio_pump_player();
    }

    qwen_audio_stop_player_inner();
    LOGI(TAG, "player task exit");
    s_qwen_audio.player_task_pid = 0;
}

static void qwen_audio_task(void *priv)
{
    void *msg;
    int ret;
    uint32_t cmd;
    int timeout;

    (void)priv;
    LOGI(TAG, "audio task start");
    while (s_qwen_audio.exit_req == 0U) {
        timeout = ((qwen_audio_recorder_is_active() != 0U) || (s_qwen_audio.player_running != 0U)) ? 1 : 0;
        msg = NULL;
        ret = os_q_pend(&s_qwen_audio.msg_q, timeout, &msg);
        if (ret == 0) {
            cmd = (uint32_t)(uintptr_t)msg;
            QWEN_TRACE("cmd=%u", (unsigned)cmd);
            qwen_audio_handle_recorder_cmd(cmd);
        }
        qwen_audio_pump_recorder();
        qwen_audio_log_stats_if_needed();
    }

    qwen_audio_stop_recorder_inner();
    LOGI(TAG, "audio task exit");
    s_qwen_audio.task_pid = 0;
}

static void qwen_audio_post_recorder_cmd(uint32_t cmd)
{
    if (s_qwen_audio.is_initialized == 0U) {
        return;
    }

    if (os_q_post(&s_qwen_audio.msg_q, (void *)(uintptr_t)cmd) != 0) {
        LOGW(TAG, "post cmd failed: %u", (unsigned)cmd);
    }
}

static void qwen_audio_post_player_cmd(uint32_t cmd)
{
    if (s_qwen_audio.is_initialized == 0U) {
        return;
    }

    if (os_q_post(&s_qwen_audio.player_msg_q, (void *)(uintptr_t)cmd) != 0) {
        LOGW(TAG, "post player cmd failed: %u", (unsigned)cmd);
    }
}

d_err_t qwen_audio_init(void)
{
    if (s_qwen_audio.is_initialized != 0U) {
        return D_OK;
    }

    memset(&s_qwen_audio, 0, sizeof(s_qwen_audio));
    s_qwen_audio.player_volume_percent = QWEN_AUDIO_PLAY_VOLUME;
    s_qwen_audio.player_restore_volume_percent = QWEN_AUDIO_PLAY_VOLUME;
    if (audio_stream_volume_init() != 0) {
        return D_ERR_IO;
    }
    audio_stream_init(D_QWEN_DS_SAMPLE_RATE, 16, 1);
    if (audio_stream_set_play_volume(QWEN_AUDIO_PLAY_VOLUME) != 0) {
        return D_ERR_NOT_READY;
    }
    cbuf_init(&s_qwen_audio.recorder_pre_cbuf, s_qwen_audio.recorder_pre_cbuf_mem, sizeof(s_qwen_audio.recorder_pre_cbuf_mem));
    cbuf_init(&s_qwen_audio.player_cbuf, s_qwen_audio.player_cbuf_mem, sizeof(s_qwen_audio.player_cbuf_mem));
    if (os_q_create(&s_qwen_audio.msg_q, 8) != OS_NO_ERR) {
        return D_ERR_IO;
    }
    if (os_q_create(&s_qwen_audio.player_msg_q, 8) != OS_NO_ERR) {
        os_q_del(&s_qwen_audio.msg_q, OS_DEL_ALWAYS);
        return D_ERR_IO;
    }
    if (thread_fork("qwen_audio", QWEN_AUDIO_TASK_PRIO, QWEN_AUDIO_TASK_STACK, 0,
                    &s_qwen_audio.task_pid, qwen_audio_task, NULL) != OS_NO_ERR) {
        os_q_del(&s_qwen_audio.player_msg_q, OS_DEL_ALWAYS);
        os_q_del(&s_qwen_audio.msg_q, OS_DEL_ALWAYS);
        memset(&s_qwen_audio, 0, sizeof(s_qwen_audio));
        return D_ERR_IO;
    }
    if (thread_fork("qwen_play", QWEN_AUDIO_PLAYER_TASK_PRIO, QWEN_AUDIO_PLAYER_TASK_STACK, 0,
                    &s_qwen_audio.player_task_pid, qwen_audio_player_task, NULL) != OS_NO_ERR) {
        thread_kill(&s_qwen_audio.task_pid, KILL_WAIT);
        s_qwen_audio.task_pid = 0;
        os_q_del(&s_qwen_audio.player_msg_q, OS_DEL_ALWAYS);
        os_q_del(&s_qwen_audio.msg_q, OS_DEL_ALWAYS);
        memset(&s_qwen_audio, 0, sizeof(s_qwen_audio));
        return D_ERR_IO;
    }
    s_qwen_audio.is_initialized = 1U;
    LOGI(TAG, "audio init done");
    return D_OK;
}

uint8_t qwen_audio_is_initialized(void)
{
    return s_qwen_audio.is_initialized;
}

uint8_t qwen_audio_is_player_active(void)
{
    if (s_qwen_audio.is_initialized == 0U) {
        return 0U;
    }

    if ((s_qwen_audio.player_running != 0U) ||
        (s_qwen_audio.player_stream_ended != 0U) ||
        ((s_qwen_audio.player_last_progress_ms != 0U) &&
         (qwen_audio_player_hw_open() != 0U)) ||
        (cbuf_get_data_size(&s_qwen_audio.player_cbuf) != 0U)) {
        return 1U;
    }

    return 0U;
}

uint8_t qwen_audio_is_player_stalled(uint32_t timeout_ms)
{
    uint32_t idle_ms;

    if ((s_qwen_audio.is_initialized == 0U) ||
        (s_qwen_audio.player_running == 0U) ||
        (timeout_ms == 0U) ||
        (s_qwen_audio.player_last_progress_ms == 0U)) {
        return 0U;
    }

    idle_ms = (uint32_t)((uint32_t)timer_get_ms() -
                         s_qwen_audio.player_last_progress_ms);
    return (idle_ms >= timeout_ms) ? 1U : 0U;
}

void qwen_audio_deinit(void)
{
    if (s_qwen_audio.is_initialized == 0U) {
        return;
    }

    qwen_audio_post_recorder_cmd(QWEN_AUDIO_CMD_EXIT);
    qwen_audio_post_player_cmd(QWEN_AUDIO_CMD_EXIT);
    if (s_qwen_audio.player_task_pid != 0) {
        thread_kill(&s_qwen_audio.player_task_pid, KILL_WAIT);
        s_qwen_audio.player_task_pid = 0;
    }
    if (s_qwen_audio.task_pid != 0) {
        thread_kill(&s_qwen_audio.task_pid, KILL_WAIT);
        s_qwen_audio.task_pid = 0;
    }
    if (qwen_audio_player_hw_open() != 0U) {
        _device_rbuf_clear();
        _device_net_audio_play(false);
    }
    os_q_del(&s_qwen_audio.player_msg_q, OS_DEL_ALWAYS);
    os_q_del(&s_qwen_audio.msg_q, OS_DEL_ALWAYS);
    memset(&s_recorder_stop, 0, sizeof(s_recorder_stop));
    memset(&s_player_stop, 0, sizeof(s_player_stop));
    memset(&s_qwen_audio, 0, sizeof(s_qwen_audio));
}

void qwen_audio_prepare_player(void)
{
    qwen_audio_post_player_cmd(QWEN_AUDIO_CMD_START_PLAYER);
    qwen_audio_wait_player_ready(QWEN_AUDIO_SWITCH_WAIT_TICKS);
    if (s_qwen_audio.player_running != 0U) {
        os_time_dly(QWEN_AUDIO_READY_CUE_WARMUP_TICKS);
    }
}

d_err_t qwen_audio_prepare_recorder(void)
{
    qwen_audio_post_recorder_cmd(QWEN_AUDIO_CMD_PREPARE_RECORDER);
    return qwen_audio_wait_recorder_ready(QWEN_AUDIO_RECORDER_READY_WAIT_TICKS);
}

void qwen_audio_start_recorder(void)
{
    qwen_audio_post_recorder_cmd(QWEN_AUDIO_CMD_START_RECORDER);
}

void qwen_audio_stop_recorder(void)
{
    s_qwen_audio.recorder_stop_requested = 1U;
    qwen_audio_post_recorder_cmd(QWEN_AUDIO_CMD_STOP_RECORDER);
}

void qwen_audio_start_player(void)
{
    qwen_audio_post_player_cmd(QWEN_AUDIO_CMD_START_PLAYER);
}

void qwen_audio_finish_player(void)
{
    qwen_audio_post_player_cmd(QWEN_AUDIO_CMD_FINISH_PLAYER);
}

void qwen_audio_stop_player(void)
{
    qwen_audio_post_player_cmd(QWEN_AUDIO_CMD_STOP_PLAYER);
}

uint32_t qwen_audio_player_generation(void)
{
    return s_player_generation;
}

void qwen_audio_stop_all(void)
{
    qwen_audio_stop_recorder();
    qwen_audio_stop_player();
}

d_err_t qwen_audio_stop_all_wait(uint32_t timeout_ms)
{
    uint32_t start_ms;

    if (!s_qwen_audio.is_initialized) return D_OK;
    if (!timeout_ms) return D_ERR_INVALID_ARG;
    (void)d_audio_stop_ticket_begin(&s_recorder_stop);
    (void)d_audio_stop_ticket_begin(&s_player_stop);
    s_qwen_audio.recorder_stop_requested = 1U;
    if (!s_recorder_stop.submitted) {
        if (os_q_post(&s_qwen_audio.msg_q,
                      (void *)(uintptr_t)QWEN_AUDIO_CMD_STOP_RECORDER_ACK) != 0)
            return D_ERR_FULL;
        d_audio_stop_ticket_submitted(&s_recorder_stop);
    }
    if (!s_player_stop.submitted) {
        if (os_q_post(&s_qwen_audio.player_msg_q,
                      (void *)(uintptr_t)QWEN_AUDIO_CMD_STOP_PLAYER_ACK) != 0)
            return D_ERR_FULL;
        d_audio_stop_ticket_submitted(&s_player_stop);
    }
    start_ms = timer_get_ms();
    do {
        if (d_audio_stop_ticket_ready(&s_recorder_stop) &&
            d_audio_stop_ticket_ready(&s_player_stop)) {
            uint32_t elapsed = (uint32_t)(timer_get_ms() - start_ms);
            d_err_t result;
            if (elapsed >= timeout_ms) return D_ERR_TIMEOUT;
            result = d_audio_shared_stop_wait(timeout_ms - elapsed);
            if (result != D_OK) return result;
            d_audio_stop_ticket_finish(&s_recorder_stop);
            d_audio_stop_ticket_finish(&s_player_stop);
            return D_OK;
        }
        os_time_dly(1);
    } while ((uint32_t)(timer_get_ms() - start_ms) < timeout_ms);
    return D_ERR_TIMEOUT;
}

d_err_t qwen_audio_set_player_volume(uint8_t volume_percent)
{
    uint8_t new_volume_percent;
    uint8_t old_volume_percent;

    new_volume_percent = qwen_audio_clamp_volume(volume_percent);
    old_volume_percent = s_qwen_audio.player_volume_percent;
    if (audio_stream_set_play_volume((int)new_volume_percent) != 0) {
        LOGW(TAG, "player hw volume set failed: %u", (unsigned)new_volume_percent);
        return D_ERR_IO;
    }
    s_qwen_audio.player_volume_percent = new_volume_percent;
    if (new_volume_percent > QWEN_AUDIO_VOLUME_MIN) {
        s_qwen_audio.player_restore_volume_percent = new_volume_percent;
        s_qwen_audio.is_player_muted = 0U;
    } else {
        s_qwen_audio.is_player_muted = 1U;
    }

    LOGI(TAG, "player volume %u -> %u",
         (unsigned)old_volume_percent,
         (unsigned)new_volume_percent);
    return D_OK;
}

d_err_t qwen_audio_adjust_player_volume(int8_t step_percent)
{
    int16_t new_volume_percent;

    new_volume_percent = (int16_t)s_qwen_audio.player_volume_percent + (int16_t)step_percent;
    if (new_volume_percent < (int16_t)QWEN_AUDIO_VOLUME_MIN) {
        new_volume_percent = (int16_t)QWEN_AUDIO_VOLUME_MIN;
    } else if (new_volume_percent > (int16_t)QWEN_AUDIO_VOLUME_MAX) {
        new_volume_percent = (int16_t)QWEN_AUDIO_VOLUME_MAX;
    }

    return qwen_audio_set_player_volume((uint8_t)new_volume_percent);
}

d_err_t qwen_audio_set_player_mute(uint8_t is_mute)
{
    uint8_t restore_volume_percent;

    if (is_mute != 0U) {
        if (s_qwen_audio.player_volume_percent > QWEN_AUDIO_VOLUME_MIN) {
            s_qwen_audio.player_restore_volume_percent = s_qwen_audio.player_volume_percent;
        }
        return qwen_audio_set_player_volume(QWEN_AUDIO_VOLUME_MIN);
    }

    restore_volume_percent = s_qwen_audio.player_restore_volume_percent;
    if (restore_volume_percent == QWEN_AUDIO_VOLUME_MIN) {
        restore_volume_percent = QWEN_AUDIO_PLAY_VOLUME;
    }
    return qwen_audio_set_player_volume(restore_volume_percent);
}

uint8_t qwen_audio_get_player_volume(void)
{
    return s_qwen_audio.player_volume_percent;
}

#endif
