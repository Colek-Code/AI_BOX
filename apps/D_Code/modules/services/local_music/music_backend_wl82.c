/** @file music_backend_wl82.c @brief 由 local_music 任务独占 TF 扫描、文件、解码器和断点。 */
#include "config/app_config.h"
#if D_FEATURE_LOCAL_MUSIC
#include "music_backend.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "fs/fs.h"
#include "system/includes.h"
#include "utils/character_coding.h"
#include <string.h>

static struct {
    struct vfscan *scan;
    struct server *decoder;
    FILE *file;
    struct audio_dec_breakpoint breakpoint;
    uint32_t generation, next_generation, position, duration;
    uint8_t open_attempted, ended;
    d_err_t terminal_result;
} s_backend;
static void music_backend_event(void *context, int argc, int *argv);

/**
 * @brief 释放 SDK 分配的断点载荷并清空断点。；完整合同见 music_backend.h。
 */
void music_backend_forget_position(void)
{
    /* GET_BREAKPOINT 分配的 SDK 载荷归调用者所有，需要显式释放。 */
    if (s_backend.breakpoint.data != NULL) free(s_backend.breakpoint.data);
    memset(&s_backend.breakpoint, 0, sizeof(s_backend.breakpoint));
}

/**
 * @brief 检查续播断点载荷是否存在且长度有效。；完整合同见 music_backend.h。
 */
uint8_t music_backend_can_resume(void)
{
    return s_backend.breakpoint.data != NULL && s_backend.breakpoint.len > 0;
}

/**
 * @brief 检查是否仍持有解码器或播放文件。；完整合同见 music_backend.h。
 */
uint8_t music_backend_owned(void)
{
    return s_backend.decoder != NULL || s_backend.file != NULL;
}

/**
 * @brief 释放当前扫描对象并清空指针。；完整合同见 music_backend.h。
 */
void music_backend_scan_release(void)
{
    if (s_backend.scan != NULL) fscan_release(s_backend.scan);
    s_backend.scan = NULL;
}

/**
 * @brief 扫描 TF 卡目录并保留工作任务拥有的扫描对象。；完整合同见 music_backend.h。
 */
d_err_t music_backend_scan(uint8_t formats, int (*cancel)(void), uint16_t *count)
{
    const char *filter = formats == MUSIC_FORMAT_MP3 ? "-r -tMP3 -sn" :
        formats == MUSIC_FORMAT_WAV ? "-r -tWAV -sn" : "-r -tMP3WAV -sn";
    if (count == NULL || music_backend_owned()) return D_ERR_BUSY;
    music_backend_scan_release();
    s_backend.scan = fscan_interrupt(CONFIG_MUSIC_PATH_SD0, filter, 9, cancel);
    *count = s_backend.scan != NULL ? s_backend.scan->file_number : 0U;
    if (cancel != NULL && cancel()) {
        music_backend_scan_release();
        return D_ERR_BUSY;
    }
    return s_backend.scan != NULL ? D_OK : D_ERR_IO;
}

/**
 * @brief 将 JLFAT 长文件名或 ASCII 短名转换到有界标题缓冲。
 * @param[in] file 已打开的曲目文件。
 * @param[out] title 标题输出缓冲。
 * @param[in] capacity 正数缓冲容量，包含末尾零字符。
 * @note UTF16LE 代理区码点和非 ASCII 短名字节以问号替代；不返回借用文件名指针。
 */
static void music_backend_name(FILE *file, char *title, uint16_t capacity)
{
    uint8_t name[512] = {0};
    int length = fget_name(file, name, sizeof(name) - 2U);
    uint16_t used = 0U;
    if (length < 0) length = 0;
    if (length > (int)sizeof(name) - 2) length = sizeof(name) - 2;
    /* JLFAT 以反斜线加 U 标识 UTF16LE 长文件名；短名按 ASCII 处理。 */
    if (length >= 2 && name[0] == '\\' && name[1] == 'U') {
        int offset;
        for (offset = 2; offset + 1 < length; offset += 2) {
            uint16_t codepoint = name[offset] | ((uint16_t)name[offset + 1] << 8);
            uint8_t encoded[4];
            int bytes;
            if (!codepoint) break;
            if (codepoint >= 0xd800U && codepoint <= 0xdfffU) codepoint = '?';
            bytes = unicode_2_utf8_one(codepoint, encoded);
            if (bytes <= 0 || used + bytes >= capacity) break;
            memcpy(title + used, encoded, bytes);
            used += bytes;
        }
    } else {
        while (used < length && used + 1U < capacity && name[used]) {
            title[used] = name[used] < 0x80U ? name[used] : '?';
            ++used;
        }
    }
    title[used] = '\0';
}

/**
 * @brief 按零起始索引读取曲目标题并关闭临时文件。；完整合同见 music_backend.h。
 */
d_err_t music_backend_item(uint16_t index, music_item_t *item)
{
    FILE *file;
    if (item == NULL || s_backend.scan == NULL || index >= s_backend.scan->file_number)
        return D_ERR_INVALID_ARG;
    file = fselect(s_backend.scan, FSEL_BY_NUMBER, index + 1U);
    if (file == NULL) return D_ERR_IO;
    memset(item, 0, sizeof(*item));
    item->index = index;
    music_backend_name(file, item->title, sizeof(item->title));
    fclose(file);
    return D_OK;
}

/**
 * @brief 打开指定曲目的解码器，在各阶段重新检查播放准入。；完整合同见 music_backend.h。
 */
d_err_t music_backend_start(uint16_t index, uint8_t volume, uint8_t resume, d_err_t (*permit)(void))
{
    union audio_req request = {0};
    d_err_t result;
    if (music_backend_owned() || s_backend.scan == NULL) return D_ERR_BUSY;
    if (resume && !music_backend_can_resume()) return D_ERR_NOT_READY;
    result = permit != NULL ? permit() : D_OK;
    if (result != D_OK) return result;
    s_backend.file = fselect(s_backend.scan, FSEL_BY_NUMBER, index + 1U);
    if (s_backend.file == NULL) return D_ERR_IO;
    s_backend.decoder = server_open("audio_server", "dec");
    if (s_backend.decoder == NULL) {
        (void)music_backend_stop(0U, NULL);
        return D_ERR_NO_MEMORY;
    }
    if (++s_backend.next_generation == 0U) ++s_backend.next_generation;
    s_backend.generation = s_backend.next_generation;
    server_register_event_handler_to_task(s_backend.decoder, (void *)(uintptr_t)s_backend.generation,
                                         music_backend_event, "local_music");
    request.dec.cmd = AUDIO_DEC_OPEN;
    request.dec.file = s_backend.file;
    request.dec.volume = volume;
    request.dec.output_buf_len = 6U * 1024U;
    request.dec.sample_source = "dac";
    request.dec.priority = 1;
    request.dec.bp = resume ? &s_backend.breakpoint : NULL;
    result = permit != NULL ? permit() : D_OK;
    if (result == D_OK) {
        s_backend.open_attempted = 1U;
        if (server_request(s_backend.decoder, AUDIO_REQ_DEC, &request) != 0) result = D_ERR_IO;
    }
    if (result == D_OK) {
        s_backend.position = request.dec.play_time;
        s_backend.duration = request.dec.total_time;
        result = permit != NULL ? permit() : D_OK;
    }
    if (result == D_OK) {
        request.dec.cmd = AUDIO_DEC_START;
        if (server_request(s_backend.decoder, AUDIO_REQ_DEC, &request) != 0) result = D_ERR_IO;
    }
    if (result != D_OK) (void)music_backend_stop(0U, NULL);
    else music_backend_forget_position();
    return result;
}

/**
 * @brief 向现有解码器提交音量。；完整合同见 music_backend.h。
 */
d_err_t music_backend_volume(uint8_t volume)
{
    union audio_req request = {0};
    if (s_backend.decoder == NULL) return D_OK;
    request.dec.cmd = AUDIO_DEC_SET_VOLUME;
    request.dec.volume = volume;
    return server_request(s_backend.decoder, AUDIO_REQ_DEC, &request) == 0 ? D_OK : D_ERR_IO;
}

/**
 * @brief 复制解码事件维护的播放进度。；完整合同见 music_backend.h。
 */
void music_backend_progress(uint32_t *position, uint32_t *duration)
{
    *position = s_backend.position;
    *duration = s_backend.duration;
}

/**
 * @brief 消费一次解码终止标记并读取终止结果。；完整合同见 music_backend.h。
 */
uint8_t music_backend_ended(d_err_t *result)
{
    uint8_t ended = s_backend.ended;
    *result = s_backend.terminal_result;
    s_backend.ended = 0U;
    return ended;
}

/**
 * @brief 过滤旧解码代次并记录进度或当前文件的终止事件。
 * @param[in] context 注册时编码的解码代次。
 * @param[in] argc 事件参数数量，至少为 2。
 * @param[in] argv SDK 事件参数数组。
 * @note 回调由 local_music 队列派发，不在回调内关闭文件。
 */
static void music_backend_event(void *context, int argc, int *argv)
{
    if (argv == NULL || argc < 2 || (uint32_t)(uintptr_t)context != s_backend.generation) return;
    if (argv[0] == AUDIO_SERVER_EVENT_CURR_TIME) {
        s_backend.position = argv[1] < 0 ? 0U : (uint32_t)argv[1];
    } else if ((argv[0] == AUDIO_SERVER_EVENT_END || argv[0] == AUDIO_SERVER_EVENT_ERR) &&
               (FILE *)(uintptr_t)argv[1] == s_backend.file) {
        s_backend.ended = 1U;
        s_backend.terminal_result = argv[0] == AUDIO_SERVER_EVENT_END ? D_OK : D_ERR_IO;
    }
}

/**
 * @brief 可选保存断点，确认 STOP 成功后关闭解码与文件。；完整合同见 music_backend.h。
 */
d_err_t music_backend_stop(uint8_t save_position, d_err_t *resume_error)
{
    union audio_req request = {0};
    int queued[2];
    if (resume_error != NULL) *resume_error = D_OK;
    if (s_backend.decoder != NULL && s_backend.open_attempted) {
        if (save_position) {
            music_backend_forget_position();
            request.dec.cmd = AUDIO_DEC_GET_BREAKPOINT;
            request.dec.bp = &s_backend.breakpoint;
            if (server_request(s_backend.decoder, AUDIO_REQ_DEC, &request) != 0 ||
                !music_backend_can_resume()) {
                music_backend_forget_position();
                if (resume_error != NULL) *resume_error = D_ERR_IO;
            }
        }
        memset(&request, 0, sizeof(request));
        request.dec.cmd = AUDIO_DEC_STOP;
        if (server_request(s_backend.decoder, AUDIO_REQ_DEC, &request) != 0) return D_ERR_IO;
        s_backend.open_attempted = 0U;
    }
    if (s_backend.decoder != NULL) {
        queued[0] = AUDIO_SERVER_EVENT_END;
        queued[1] = (int)(uintptr_t)s_backend.file;
        (void)server_event_handler_del(s_backend.decoder, 2, queued);
        queued[0] = AUDIO_SERVER_EVENT_ERR;
        (void)server_event_handler_del(s_backend.decoder, 2, queued);
        s_backend.generation = 0U;
        server_close(s_backend.decoder);
        s_backend.decoder = NULL;
    }
    if (s_backend.file != NULL) fclose(s_backend.file);
    s_backend.file = NULL;
    s_backend.ended = 0U;
    return D_OK;
}
#endif
