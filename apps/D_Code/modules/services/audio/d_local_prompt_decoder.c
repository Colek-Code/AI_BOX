/** @file d_local_prompt_decoder.c @brief 稳定 VFS 与播放代次隔离的 PCM 提示音解码实现。 */
#include "config/app_config.h"
#if D_LOCAL_PROMPT_ENABLE
#include "d_local_prompt_decoder.h"
#include "server/server_core.h"
#include "server/audio_server.h"
#include "system/includes.h"
#include <string.h>

static const int16_t s_prompt_pcm[] = {
#include "d_local_prompt_pcm.inc"
};
static struct {
    struct server *server;
    uint32_t generation;
    uint32_t offset;
    uint32_t length;
    uint8_t repeating;
    uint8_t finished;
    uint8_t opened;
    d_err_t result;
} s_decoder;

/**
 * @brief 从提示 PCM 或循环静音区复制下一段虚拟文件数据。
 * @param[in] file VFS 兼容参数，本实现不使用。
 * @param[out] buffer 可容纳 size 字节的目标缓冲区。
 * @param[in] size 本次最多读取字节数。
 * @return 实际写入 buffer 的字节数。
 */
static int d_local_prompt_read(void *file, void *buffer, u32 size)
{
    uint32_t available = s_decoder.length - s_decoder.offset;
    uint32_t copied = 0U;
    (void)file;
    if (size > available) size = available;
    while (copied < size) {
        uint32_t phase = s_decoder.repeating ? s_decoder.offset % 32000U : s_decoder.offset;
        uint32_t span = phase < sizeof(s_prompt_pcm) ? sizeof(s_prompt_pcm) - phase : 32000U - phase;
        if (span > size - copied) span = size - copied;
        if (phase < sizeof(s_prompt_pcm)) memcpy((uint8_t *)buffer + copied, (const uint8_t *)s_prompt_pcm + phase, span);
        else memset((uint8_t *)buffer + copied, 0, span);
        copied += span;
        s_decoder.offset += span;
    }
    return (int)size;
}

/**
 * @brief 按起点、当前位置或文件末尾调整虚拟 PCM 读偏移。
 * @param[in] file VFS 兼容参数，本实现不使用。
 * @param[in] offset 按有符号 32 位解释的偏移字节数。
 * @param[in] origin 0 为文件起点，1 为当前位置，2 为文件末尾。
 * @return 定位成功返回 0；起点无效或位置越界返回 -1。
 */
static int d_local_prompt_seek(void *file, u32 offset, int origin)
{
    int64_t position;
    (void)file;
    position = origin == 0 ? (int32_t)offset : origin == 1 ?
        (int64_t)s_decoder.offset + (int32_t)offset : origin == 2 ?
        (int64_t)s_decoder.length + (int32_t)offset : -1;
    if (position < 0 || position > s_decoder.length) return -1;
    s_decoder.offset = (uint32_t)position;
    return 0;
}

/**
 * @brief 读取当前虚拟 PCM 文件长度。
 * @param[in] file VFS 兼容参数，本实现不使用。
 * @return 虚拟文件的字节数。
 */
static int d_local_prompt_length(void *file) { (void)file; return (int)s_decoder.length; }
/**
 * @brief 响应 VFS 关闭操作，静态 PCM 无需释放。
 * @param[in] file VFS 兼容参数，本实现不使用。
 * @return 固定返回 0。
 */
static int d_local_prompt_close_file(void *file) { (void)file; return 0; }
static const struct audio_vfs_ops s_prompt_vfs = {
    .fread = d_local_prompt_read, .fseek = d_local_prompt_seek,
    .flen = d_local_prompt_length, .fclose = d_local_prompt_close_file,
};

/**
 * @brief 仅接受当前播放代次的解码结束与错误通知。
 * @param[in] context 注册时编码到指针中的播放代次。
 * @param[in] argc 事件参数数量。
 * @param[in] argv 事件参数数组，首项为事件编号。
 * @note 仅置完成和错误状态，不在事件回调中关闭解码服务。
 */
static void d_local_prompt_event(void *context, int argc, int *argv)
{
    if (argc < 1 || argv == NULL || (uint32_t)(uintptr_t)context != s_decoder.generation) return;
    if (argv[0] == AUDIO_SERVER_EVENT_END || argv[0] == AUDIO_SERVER_EVENT_ERR) {
        s_decoder.finished = 1U;
        if (argv[0] == AUDIO_SERVER_EVENT_ERR) s_decoder.result = D_ERR_IO;
    }
}

/**
 * @brief STOP 成功后清理排队事件并关闭服务；完整合同见 d_local_prompt_decoder.h。
 */
d_err_t d_local_prompt_decoder_stop(void)
{
    union audio_req request = {0};
    int queued[2];
    if (s_decoder.server == NULL) return D_OK;
    if (s_decoder.opened) {
        request.dec.cmd = AUDIO_DEC_STOP;
        if (server_request(s_decoder.server, AUDIO_REQ_DEC, &request) != 0) return D_ERR_IO;
        s_decoder.opened = 0U;
    }
    queued[0] = AUDIO_SERVER_EVENT_END;
    queued[1] = (int)(uintptr_t)&s_decoder;
    (void)server_event_handler_del(s_decoder.server, 2, queued);
    queued[0] = AUDIO_SERVER_EVENT_ERR;
    (void)server_event_handler_del(s_decoder.server, 2, queued);
    s_decoder.generation = 0U;
    server_close(s_decoder.server);
    s_decoder.server = NULL;
    return D_OK;
}

/**
 * @brief 配置虚拟 PCM 并在启动前重新核实准入；完整合同见 d_local_prompt_decoder.h。
 */
d_err_t d_local_prompt_decoder_start_checked(uint32_t generation, uint8_t volume, uint8_t repeating,
    d_err_t (*permit_start)(uint32_t generation))
{
    union audio_req request = {0};
    d_err_t result;
    if (generation == 0U || volume > 100U || s_decoder.server != NULL) return D_ERR_BUSY;
    s_decoder.finished = 0U;
    s_decoder.result = D_OK;
    s_decoder.server = server_open("audio_server", "dec");
    if (s_decoder.server == NULL) return D_ERR_NO_MEMORY;
    s_decoder.generation = generation;
    s_decoder.offset = 0U;
    s_decoder.repeating = repeating;
    /* 这里只扩大虚拟 PCM 长度，不分配五分钟的采样缓冲区。 */
    s_decoder.length = repeating ? 8000U * 2U * 300U : sizeof(s_prompt_pcm);
    s_decoder.finished = 0U;
    s_decoder.result = D_OK;
    server_register_event_handler_to_task(s_decoder.server, (void *)(uintptr_t)generation,
                                          d_local_prompt_event, "local_prompt");
    request.dec.cmd = AUDIO_DEC_OPEN;
    request.dec.volume = volume;
    request.dec.channel = 1U;
    request.dec.sample_rate = 8000U;
    request.dec.output_buf_len = 2048U;
    request.dec.dec_type = "pcm";
    request.dec.sample_source = "dac";
    request.dec.vfs_ops = &s_prompt_vfs;
    request.dec.file = (FILE *)&s_decoder;
    request.dec.priority = 0U;
    if (server_request(s_decoder.server, AUDIO_REQ_DEC, &request) != 0) {
        (void)d_local_prompt_decoder_stop();
        return D_ERR_IO;
    }
    s_decoder.opened = 1U;
    if (permit_start != NULL) {
        result = permit_start(generation);
        if (result != D_OK) return result;
    }
    request.dec.cmd = AUDIO_DEC_START;
    if (server_request(s_decoder.server, AUDIO_REQ_DEC, &request) != 0) return D_ERR_IO;
    return D_OK;
}

/**
 * @brief 启动单次提示音；完整合同见 d_local_prompt_decoder.h。
 */
d_err_t d_local_prompt_decoder_start(uint32_t generation, uint8_t volume)
{ return d_local_prompt_decoder_start_checked(generation, volume, 0U, NULL); }
/**
 * @brief 启动循环提示音；完整合同见 d_local_prompt_decoder.h。
 */
d_err_t d_local_prompt_decoder_start_alarm(uint32_t generation, uint8_t volume)
{ return d_local_prompt_decoder_start_checked(generation, volume, 1U, NULL); }

/**
 * @brief 向已打开的解码服务提交音量；完整合同见 d_local_prompt_decoder.h。
 */
d_err_t d_local_prompt_decoder_volume(uint8_t volume)
{
    union audio_req request = {0};
    if (volume > 100U) return D_ERR_INVALID_ARG;
    if (!s_decoder.opened) return D_OK;
    request.dec.cmd = AUDIO_DEC_SET_VOLUME;
    request.dec.volume = volume;
    return server_request(s_decoder.server, AUDIO_REQ_DEC, &request) == 0 ? D_OK : D_ERR_IO;
}

/**
 * @brief 读取事件终态标记；完整合同见 d_local_prompt_decoder.h。
 */
uint8_t d_local_prompt_decoder_finished(void) { return s_decoder.finished; }
/**
 * @brief 读取当前解码结果；完整合同见 d_local_prompt_decoder.h。
 */
d_err_t d_local_prompt_decoder_result(void) { return s_decoder.result; }
#endif
