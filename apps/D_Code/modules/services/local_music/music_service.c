/**
 * @file    music_service.c
 * @brief   本地音乐服务
 *
 * ❌ 禁止：LVGL 代码（如需 UI，在 music_presenter/music_view 中实现）
 */

#include "config/app_config.h"

#if D_FEATURE_LOCAL_MUSIC

#include "music_service.h"
#include "system/includes.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "fs/fs.h"
#include "storage_device.h"
#include "syscfg/syscfg_id.h"
#include "framework/d_event_bus.h"
#include "framework/d_log.h"
#include "framework/d_event_payloads.h"

#define TAG "MUSIC_SVC"

//*********************************************************************************//
//                              配置                                                //
//*********************************************************************************//

#define VOLUME_STEP         5
#define MIN_VOLUME_VALUE    5
#define MAX_VOLUME_VALUE    100
#define INIT_VOLUME_VALUE   50

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

struct music_hdl {
    u8  local_play_all;
    char volume;
    int play_time;
    int total_time;
    FILE *file;
    struct vfscan *fscan;
    struct vfscan *dir_list;
    struct server *dec_server;
    const char *local_path;
    char cur_name[64];
    char cur_path[128];
    u32 cur_file_len;
};

static struct music_hdl s_music;
#define __this (&s_music)

//*********************************************************************************//
//                              核心播放逻辑（保留原有）                            //
//*********************************************************************************//

static int music_dec_stop(void)
{
    union audio_req req = {0};
    if (!__this->file) return 0;

    LOGD(TAG, "dec_stop");
    req.dec.cmd = AUDIO_DEC_STOP;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);

    int argv[2];
    argv[0] = AUDIO_SERVER_EVENT_END;
    argv[1] = (int)__this->file;
    server_event_handler_del(__this->dec_server, 2, argv);

    fclose(__this->file);
    __this->file = NULL;
    return 0;
}

static int music_dec_file(FILE *file)
{
    int err;
    union audio_req req = {0};

    if (!file) return -1;

    if (__this->fscan) {
        fget_path(file, __this->fscan, (u8 *)__this->cur_path, sizeof(__this->cur_path) - 1, 0);
    }

    int file_len = (int)flen(file);
    __this->cur_file_len = file_len > 0 ? (u32)file_len : 0;
    LOGI(TAG, "play: %s (%u bytes)", __this->cur_path[0] ? __this->cur_path : __this->cur_name, __this->cur_file_len);

    music_dec_stop();

    req.dec.cmd             = AUDIO_DEC_OPEN;
    req.dec.volume          = __this->volume;
    req.dec.output_buf_len  = 6 * 1024;
    req.dec.file            = file;
    req.dec.channel         = 0;
    req.dec.sample_rate     = 0;
    req.dec.priority        = 1;
    req.dec.sample_source   = CONFIG_AUDIO_DEC_PLAY_SOURCE;

    err = server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    if (err) {
        LOGE(TAG, "dec_open err=%d", err);
        fclose(file);
        return err;
    }

    __this->play_time  = req.dec.play_time;
    __this->total_time = req.dec.total_time;

    req.dec.cmd = AUDIO_DEC_START;
    err = server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    if (err) {
        LOGE(TAG, "dec_start err=%d", err);
        fclose(file);
        return err;
    }

    __this->file = file;
    LOGI(TAG, "playing, total=%ds", __this->total_time);
    return 0;
}

static int music_dec_switch_file(int fsel_mode)
{
    FILE *file = NULL;
    char name[64] = {0};
    int i = 0;

    if (!__this->fscan || !__this->fscan->file_number) return -1;

__retry:
    do {
        file = fselect(__this->fscan, fsel_mode,
                       fsel_mode == FSEL_BY_NUMBER ? (CPU_RAND() % __this->fscan->file_number) + 1 : 0);
        if (file) {
            int len = fget_name(file, (u8 *)name, sizeof(name) - 1);
            if (len > 0) {
                memset(__this->cur_name, 0, sizeof(__this->cur_name));
                if (len >= (int)sizeof(__this->cur_name)) len = sizeof(__this->cur_name) - 1;
                memcpy(__this->cur_name, name, len);
            }
            i++;
            break;
        }
        if (fsel_mode == FSEL_NEXT_FILE) fsel_mode = FSEL_FIRST_FILE;
        else if (fsel_mode == FSEL_PREV_FILE) fsel_mode = FSEL_LAST_FILE;
        else break;
    } while (i++ < __this->fscan->file_number);

    if (!file) return -1;

    if (0 != music_dec_file(file)) {
        if (fsel_mode == FSEL_FIRST_FILE) fsel_mode = FSEL_NEXT_FILE;
        else if (fsel_mode == FSEL_LAST_FILE) fsel_mode = FSEL_PREV_FILE;
        goto __retry;
    }
    return 0;
}

static int music_switch_device(const char *path)
{
    LOGI(TAG, "switch device: %s", path ? path : "(null)");

    if (__this->dir_list) { fscan_release(__this->dir_list); __this->dir_list = NULL; }
    if (__this->fscan) { fscan_release(__this->fscan); __this->fscan = NULL; }
    music_dec_stop();

    if (!path) return -1;
    __this->local_path = path;

    /* 全盘搜索音频文件 */
    __this->fscan = fscan(path, "-r -tMP3WMAWAVM4AAMRAPEFLAAACSPXOPUDTSADP -sn", 2);
    if (!__this->fscan || !__this->fscan->file_number) {
        LOGW(TAG, "No audio files found");
        return -1;
    }
    LOGI(TAG, "Found %d audio files", __this->fscan->file_number);
    return music_dec_switch_file(FSEL_FIRST_FILE);
}

//*********************************************************************************//
//                              音频服务器事件回调                                  //
//*********************************************************************************//

static void dec_server_event_handler(void *priv, int argc, int *argv)
{
    switch (argv[0]) {
    case AUDIO_SERVER_EVENT_ERR:
    case AUDIO_SERVER_EVENT_END:
        if (!__this->file || argv[1] != (int)__this->file) break;
        music_dec_stop();
        music_dec_switch_file(FSEL_NEXT_FILE);
        break;
    case AUDIO_SERVER_EVENT_CURR_TIME:
        __this->play_time = argv[1];
        break;
    }
}

//*********************************************************************************//
//                              事件订阅                                            //
//*********************************************************************************//

static void on_sd_inserted(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_SD_INSERTED) return;
    LOGI(TAG, "SD 卡插入，切换播放源");
#if TCFG_SD0_ENABLE
    if (sdcard_storage_device_ready("sd0")) {
        music_switch_device(CONFIG_MUSIC_PATH_SD0);
    }
#endif
}

static void on_sd_removed(const d_event_t *evt, void *ctx)
{
    if (evt->id != EVT_SD_REMOVED) return;
    LOGI(TAG, "SD 卡拔出，停止播放");
    music_switch_device(NULL);
}

//*********************************************************************************//
//                              公共 API                                            //
//*********************************************************************************//

void music_service_play_pause(void)
{
    union audio_req r = {0};
    r.dec.cmd = AUDIO_DEC_PP;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &r);
}

void music_service_next(void)
{
    music_dec_switch_file(FSEL_NEXT_FILE);
}

void music_service_prev(void)
{
    music_dec_switch_file(FSEL_PREV_FILE);
}

void music_service_volume(int step)
{
    union audio_req req = {0};
    int volume = __this->volume + step;
    if (volume < MIN_VOLUME_VALUE) volume = MIN_VOLUME_VALUE;
    if (volume > MAX_VOLUME_VALUE) volume = MAX_VOLUME_VALUE;
    if (volume == __this->volume) return;
    __this->volume = volume;

    req.dec.cmd    = AUDIO_DEC_SET_VOLUME;
    req.dec.volume = volume;
    server_request(__this->dec_server, AUDIO_REQ_DEC, &req);
    LOGD(TAG, "volume=%d", volume);

#ifdef CONFIG_STORE_VOLUME
    syscfg_write(CFG_MUSIC_VOL, &__this->volume, sizeof(__this->volume));
#endif
}

d_err_t music_service_init(void)
{
    LOGI(TAG, "音乐服务初始化");

    memset(__this, 0, sizeof(struct music_hdl));

#ifdef CONFIG_STORE_VOLUME
    if (syscfg_read(CFG_MUSIC_VOL, &__this->volume, sizeof(__this->volume)) < 0 ||
        __this->volume < MIN_VOLUME_VALUE || __this->volume > MAX_VOLUME_VALUE) {
        __this->volume = INIT_VOLUME_VALUE;
    }
#else
    __this->volume = INIT_VOLUME_VALUE;
#endif
    __this->local_play_all = 1;

    __this->dec_server = server_open("audio_server", "dec");
    if (!__this->dec_server) {
        LOGE(TAG, "audio_server open failed");
        return D_ERR_IO;
    }
    server_register_event_handler_to_task(__this->dec_server, NULL, dec_server_event_handler, "app_core");

    /* 订阅存储事件 */
    if ((d_event_subscribe(EVT_SD_INSERTED, on_sd_inserted, NULL) != 0) ||
        (d_event_subscribe(EVT_SD_REMOVED, on_sd_removed, NULL) != 0)) {
        music_service_deinit();
        LOGE(TAG, "存储事件订阅失败");
        return D_ERR_IO;
    }

    /* 初始检测存储设备 */
#if TCFG_SD0_ENABLE
    if (sdcard_storage_device_ready("sd0")) {
        music_switch_device(CONFIG_MUSIC_PATH_SD0);
        return D_OK;
    }
#endif
    LOGW(TAG, "No storage device available, waiting for insertion...");
    return D_OK;
}

void music_service_deinit(void)
{
    d_event_unsubscribe(EVT_SD_INSERTED, on_sd_inserted);
    d_event_unsubscribe(EVT_SD_REMOVED,  on_sd_removed);
    music_switch_device(NULL);
    if (__this->dec_server) {
        server_close(__this->dec_server);
        __this->dec_server = NULL;
    }
}

#endif /* D_FEATURE_LOCAL_MUSIC */
