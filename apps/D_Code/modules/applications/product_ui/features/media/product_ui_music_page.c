/** @file product_ui_music_page.c @brief 十项歌曲窗口及播放状态投影，页面生命周期不拥有播放资源。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && D_UI_TARGET == D_UI_PRODUCT
#include "product_ui_music_page.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/assets/product_ui_icons.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#if D_PRODUCT_MUSIC_ENABLE
#include "modules/services/local_music/music_service.h"
#include "modules/applications/product_ui/features/media/product_ui_media_runtime.h"
#endif
#include <string.h>

#if D_PRODUCT_MUSIC_ENABLE
/* 页面动作编号独立于服务命令枚举。 */
typedef enum {
    PRODUCT_UI_MUSIC_SELECT_FIRST = 0,
    PRODUCT_UI_MUSIC_TOGGLE_TRACK = 1,
    PRODUCT_UI_MUSIC_NEXT_TRACK = 2,
    PRODUCT_UI_MUSIC_PREVIOUS_TRACK = 3,
    PRODUCT_UI_MUSIC_RESCAN_LIBRARY = 4,
    PRODUCT_UI_MUSIC_PAGE_PREVIOUS = 10,
    PRODUCT_UI_MUSIC_PAGE_NEXT = 11,
    PRODUCT_UI_MUSIC_SHOW_LIBRARY = 20,
    PRODUCT_UI_MUSIC_SHOW_PLAYER = 21,
    PRODUCT_UI_MUSIC_OPEN_SOUND = 22
} product_ui_music_action_e;

/* 每轮刷新派生的显示阶段，不是另一个服务状态机。 */
typedef enum {
    PRODUCT_UI_MUSIC_WINDOW_UNREQUESTED,
    PRODUCT_UI_MUSIC_WINDOW_LOADING,
    PRODUCT_UI_MUSIC_WINDOW_READY,
    PRODUCT_UI_MUSIC_WINDOW_FAILED
} product_ui_music_window_phase_e;

/* UI 只拥有窗口身份，目录及播放器仍由服务持有。 */
typedef struct {
    music_page_t cache;
    uint32_t request_id;
    uint16_t first;
} product_ui_music_window_t;

typedef struct {
    uint16_t current_index;
    music_state_e state;
    uint8_t hold_mask;
} product_ui_music_render_cache_t;

enum {
    PRODUCT_UI_MUSIC_TEXT_TITLE, PRODUCT_UI_MUSIC_TEXT_MINI_TITLE,
    PRODUCT_UI_MUSIC_TEXT_STATUS, PRODUCT_UI_MUSIC_TEXT_EMPTY,
    PRODUCT_UI_MUSIC_TEXT_LIBRARY_STATUS, PRODUCT_UI_MUSIC_TEXT_PAGES,
    PRODUCT_UI_MUSIC_TEXT_TRACK_NUMBER, PRODUCT_UI_MUSIC_TEXT_ELAPSED,
    PRODUCT_UI_MUSIC_TEXT_DURATION, PRODUCT_UI_MUSIC_TEXT_COUNT
};

#endif

typedef struct {
    product_ui_page_view_t view;
#if D_PRODUCT_MUSIC_ENABLE
    music_snapshot_t service; /* 可丢弃的状态副本，不拥有播放状态。 */
    product_ui_music_window_t window;
    product_ui_music_render_cache_t rendered;
    product_ui_label_cache_t text_cache[PRODUCT_UI_MUSIC_TEXT_COUNT];
    product_ui_virtual_list_t list;
    product_ui_list_item_t rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    /* 控件及其绑定只存活到页面销毁。 */
    lv_obj_t *library, *player, *title, *status, *pages, *toggle_icon;
    lv_obj_t *library_status, *mini_title, *mini_toggle, *mini_toggle_icon, *dock, *empty;
    lv_obj_t *progress, *elapsed, *duration, *track_number, *refresh;
    lv_obj_t *previous, *toggle, *next, *page_previous, *page_next;
    uint32_t refresh_ms;
    d_err_t last_submit_result; /* 只记录受理结果，完成确认来自服务。 */
#endif
} product_ui_music_page_state_t;

static product_ui_music_page_state_t s_music_page;
#if D_PRODUCT_MUSIC_ENABLE
static product_ui_row_text_cache_t s_music_row_text[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
#endif

#if D_PRODUCT_MUSIC_ENABLE
/**
 * @brief 切换歌曲库与播放器子页面的可见性。
 * @param[in] show 非零显示播放器，零显示歌曲库。
 */
static void product_ui_music_show_player(uint8_t show)
{
    if (show) {
        lv_obj_add_flag(s_music_page.library, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_music_page.player, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_music_page.player, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_music_page.library, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 使用当前服务媒体代次提交播放控制，只保存受理结果。
 * @param[in] command 音乐服务控制命令。
 * @param[in] index SELECT 的服务目录下标，其余命令按服务合同解释。
 * @note 播放完成及失败由后续服务快照确认，页面不拥有播放资源。
 */
static void product_ui_music_request(music_command_e command, uint16_t index)
{
    uint32_t request;
    s_music_page.last_submit_result = music_service_request(command,
        s_music_page.service.media_generation, index, &request);
}

/**
 * @brief 请求指定起点的有限歌曲窗口，受理成功后记录编号并清除旧行。
 * @param[in] first 服务目录内的窗口起点。
 * @note 成功只表示受理，窗口内容等待 page_completed_id 匹配。
 */
static void product_ui_music_page_request(uint16_t first)
{
    uint32_t request;
    d_err_t result = music_service_request_page(s_music_page.service.media_generation, first, &request);
    s_music_page.last_submit_result = result;
    if (result == D_OK) {
        s_music_page.window.first = first;
        s_music_page.window.request_id = request;
        s_music_page.window.cache.count = 0U;
        (void)product_ui_virtual_list_set_count(&s_music_page.list, 0U);
    }
}

/**
 * @brief 把页面独立动作编号映射为播放、翻页、显示切换或声音设置导航。
 * @param[in] event 仅处理 CLICKED，用户数据保存页面动作编号。
 */
static void product_ui_music_action(lv_event_t *event)
{
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    switch (action) {
    case PRODUCT_UI_MUSIC_SELECT_FIRST:
        product_ui_music_request(MUSIC_SELECT, 0U);
        break;
    case PRODUCT_UI_MUSIC_TOGGLE_TRACK:
        product_ui_music_request(MUSIC_TOGGLE, 0U);
        break;
    case PRODUCT_UI_MUSIC_NEXT_TRACK:
        product_ui_music_request(MUSIC_NEXT, 0U);
        break;
    case PRODUCT_UI_MUSIC_PREVIOUS_TRACK:
        product_ui_music_request(MUSIC_PREVIOUS, 0U);
        break;
    case PRODUCT_UI_MUSIC_RESCAN_LIBRARY:
        product_ui_music_request(MUSIC_RESCAN, 0U);
        product_ui_music_show_player(0U);
        break;
    case PRODUCT_UI_MUSIC_PAGE_PREVIOUS:
        if (s_music_page.window.first >= MUSIC_PAGE_CAPACITY)
            product_ui_music_page_request(s_music_page.window.first - MUSIC_PAGE_CAPACITY);
        break;
    case PRODUCT_UI_MUSIC_PAGE_NEXT:
        if (s_music_page.window.first + MUSIC_PAGE_CAPACITY < s_music_page.service.count)
            product_ui_music_page_request(s_music_page.window.first + MUSIC_PAGE_CAPACITY);
        break;
    case PRODUCT_UI_MUSIC_SHOW_LIBRARY:
        product_ui_music_show_player(0U);
        break;
    case PRODUCT_UI_MUSIC_SHOW_PLAYER:
        if (s_music_page.service.current_index != MUSIC_INDEX_NONE) product_ui_music_show_player(1U);
        break;
    case PRODUCT_UI_MUSIC_OPEN_SOUND:
        (void)product_ui_open_module("product_settings_sound");
        break;
    default:
        break;
    }
}

/**
 * @brief 确认缓存媒体代次仍匹配后选择歌曲，受理成功即切到播放器视图。
 * @param[in] index 当前窗口内逻辑下标。
 * @param[in] context 列表动作上下文，当前未使用。
 * @note 进入播放器视图不表示播放已经开始。
 */
static void product_ui_music_select(uint16_t index, void *context)
{
    (void)context;
    if (index >= s_music_page.window.cache.count || s_music_page.window.cache.generation != s_music_page.service.media_generation) return;
    product_ui_music_request(MUSIC_SELECT, s_music_page.window.cache.items[index].index);
    if (s_music_page.last_submit_result == D_OK) product_ui_music_show_player(1U);
}

/**
 * @brief 用缓存条目填充固定槽行规格，显示当前曲目及持有状态。
 * @param[in] index 窗口内曲目下标。
 * @param[in] slot 固定复用槽下标。
 * @param[out] spec 接收借用缓存标题的行规格。
 * @param[in] context 列表绑定上下文，当前未使用。
 * @return 成功返回 0；窗口下标或槽下标越界返回 -1。
 * @note 动作由共用虚拟列表注入并校验，行本身不持有播放所有权。
 */
static int product_ui_music_bind_row(uint16_t index, uint8_t slot, product_ui_row_spec_t *spec, void *context)
{
    music_item_t *item;
    product_ui_list_item_t *row;
    (void)context;
    if (index >= s_music_page.window.cache.count || slot >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) return -1;
    item = &s_music_page.window.cache.items[index];
    row = &s_music_page.rows[slot];
    memset(row, 0, sizeof(*row));
    row->item_id = "music_track";
    row->title = item->title[0] ? item->title : "未命名歌曲";
    row->icon_id = PRODUCT_UI_ICON_MUSIC;
    row->accent = PRODUCT_UI_COLOR_ACTION;
    *spec = (product_ui_row_spec_t){.item = row, .interaction = PRODUCT_UI_ROW_INTERACTION_ACTION,
        /* PRODUCT_UI_VIRTUAL_ACTION_INJECTED: 虚拟列表拥有带校验的动作分发。 */
        .status_text = item->index == s_music_page.service.current_index ?
            (s_music_page.service.state == MUSIC_PLAYING ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_PAUSE) : "",
        .trailing = PRODUCT_UI_ROW_TRAILING_STATUS, .is_enabled = !s_music_page.service.held,
        .is_single_line = 1U, .item_key = (uint32_t)item->index + 1U,
        .appearance = PRODUCT_UI_ROW_APPEARANCE_INSET};
    return 0;
}

/**
 * @brief 按投影使能或禁用现有对象。
 * @param[in,out] object 调用链保证有效的对象。
 * @param[in] enabled 非零允许操作，零禁用。
 */
static void product_ui_music_enabled(lv_obj_t *object, uint8_t enabled)
{
    if (enabled) lv_obj_remove_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
}

/**
 * @brief 按就绪、介质、占用、错误及完成确认选择音乐状态文字。
 * @return 静态中文状态文字的借用指针。
 */
static const char *product_ui_music_status(void)
{
    const music_snapshot_t *state = &s_music_page.service;
    if (!state->ready || !product_ui_media_runtime_ready()) return "音乐服务不可用";
    if (!state->online) return "请插入 TF 卡";
    if (state->state == MUSIC_SCANNING) return "正在扫描歌曲";
    if (state->state == MUSIC_STOPPING) return "正在停止，请稍候";
    if (state->held) return "音频忙，音乐已暂停";
    if (state->error != D_OK) return "读取或播放失败，请重试";
    if (!state->count) return "没有 MP3/WAV 歌曲";
    if (s_music_page.last_submit_result != D_OK) return "操作未完成，请稍后重试";
    if (state->resume_error != D_OK && state->state == MUSIC_PAUSED) return "断点不可用，请重新选择歌曲";
    if (state->command_result != D_OK && state->request_id == state->completed_id && state->request_id)
        return "播放操作失败，请重新选择歌曲";
    if (state->state == MUSIC_PAUSED) return "已暂停，点击继续播放";
    if (state->state == MUSIC_PLAYING) return "正在播放";
    return "点击歌曲开始播放";
}

/**
 * @brief 核对媒体代次及页完成编号后更新可丢弃的窗口副本。
 * @param[in] next 本轮借用的音乐服务快照。
 * @note 媒体代次变化先丢弃旧窗口和请求身份；只接受匹配窗口请求的完成页，不把受理当内容已到达。
 */
static void product_ui_music_refresh_window(const music_snapshot_t *next)
{
    if (next->media_generation != s_music_page.service.media_generation) {
        s_music_page.window.cache.count = 0U;
        s_music_page.window.request_id = 0U;
        s_music_page.window.first = 0U;
        s_music_page.last_submit_result = D_OK;
        (void)product_ui_virtual_list_set_count(&s_music_page.list, 0U);
        product_ui_music_show_player(0U);
    }
    s_music_page.service = *next;
    if (!s_music_page.window.request_id && next->count) product_ui_music_page_request(0U);
    if (s_music_page.window.request_id && next->page_completed_id == s_music_page.window.request_id &&
        s_music_page.window.cache.request_id != next->page_completed_id) {
        music_service_get_page(&s_music_page.window.cache);
        if (next->page_result != D_OK) s_music_page.window.cache.count = 0U;
        (void)product_ui_virtual_list_set_count(&s_music_page.list, s_music_page.window.cache.count);
        product_ui_virtual_list_scroll_to(&s_music_page.list, 0U);
    }
}

/**
 * @brief 用页面指定槽调用共用文字缓存更新，保留共用的裁剪与失败重试策略。
 * @param[in,out] label 待更新的借用标签。
 * @param[in] slot 页面文本缓存槽，越界时忽略。
 * @param[in] text 本次借用的目标文字。
 */
static void product_ui_music_text_sync(lv_obj_t *label, unsigned slot, const char *text)
{
    if (slot >= PRODUCT_UI_MUSIC_TEXT_COUNT) return;
    (void)product_ui_label_sync(label, text, &s_music_page.text_cache[slot]);
}

/**
 * @brief 只用已取得的服务副本更新文字、进度、图标和操作能力。
 * @note 不执行目录扫描、解码或资源所有权变更；列表窗口阶段仅为本次显示派生结果。
 */
static void product_ui_music_render(void)
{
    const music_snapshot_t *next = &s_music_page.service;
    product_ui_music_window_phase_e phase;
    uint8_t can_transport;
    char text[128];
    if (s_music_page.rendered.current_index != next->current_index || s_music_page.rendered.state != next->state ||
        s_music_page.rendered.hold_mask != next->held) {
        s_music_page.rendered.current_index = next->current_index;
        s_music_page.rendered.state = next->state;
        s_music_page.rendered.hold_mask = next->held;
        (void)product_ui_virtual_list_refresh(&s_music_page.list);
    }
    product_ui_music_text_sync(s_music_page.title, PRODUCT_UI_MUSIC_TEXT_TITLE, next->current_index == MUSIC_INDEX_NONE ? "选择一首歌曲" : next->current.title);
    product_ui_music_text_sync(s_music_page.mini_title, PRODUCT_UI_MUSIC_TEXT_MINI_TITLE, next->current_index == MUSIC_INDEX_NONE ? "选择一首歌曲" : next->current.title);
    product_ui_music_text_sync(s_music_page.status, PRODUCT_UI_MUSIC_TEXT_STATUS, product_ui_music_status());
    if (next->current_index == MUSIC_INDEX_NONE) lv_obj_add_flag(s_music_page.dock, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(s_music_page.dock, LV_OBJ_FLAG_HIDDEN);
    phase = !s_music_page.window.request_id ? PRODUCT_UI_MUSIC_WINDOW_UNREQUESTED :
        next->page_completed_id != s_music_page.window.request_id ? PRODUCT_UI_MUSIC_WINDOW_LOADING :
        next->page_result != D_OK ? PRODUCT_UI_MUSIC_WINDOW_FAILED : PRODUCT_UI_MUSIC_WINDOW_READY;
    if (next->count && phase != PRODUCT_UI_MUSIC_WINDOW_FAILED && phase != PRODUCT_UI_MUSIC_WINDOW_LOADING) lv_obj_add_flag(s_music_page.empty, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_remove_flag(s_music_page.empty, LV_OBJ_FLAG_HIDDEN);
        product_ui_music_text_sync(s_music_page.empty, PRODUCT_UI_MUSIC_TEXT_EMPTY, phase == PRODUCT_UI_MUSIC_WINDOW_FAILED ? "歌曲列表读取失败，请刷新" :
            phase == PRODUCT_UI_MUSIC_WINDOW_LOADING ? "正在读取列表" : product_ui_music_status());
    }
    if (next->count && next->error == D_OK) {
        (void)snprintf(text, sizeof(text), "%u 首%s", (unsigned)next->count, next->limited ? " · 已达上限" : "");
        product_ui_music_text_sync(s_music_page.library_status, PRODUCT_UI_MUSIC_TEXT_LIBRARY_STATUS, text);
    } else product_ui_music_text_sync(s_music_page.library_status, PRODUCT_UI_MUSIC_TEXT_LIBRARY_STATUS, "本地歌曲");
    (void)snprintf(text, sizeof(text), "%u / %u",
        next->count ? (unsigned)(s_music_page.window.first / MUSIC_PAGE_CAPACITY + 1U) : 0U,
        (unsigned)((next->count + MUSIC_PAGE_CAPACITY - 1U) / MUSIC_PAGE_CAPACITY));
    product_ui_music_text_sync(s_music_page.pages, PRODUCT_UI_MUSIC_TEXT_PAGES, text);
    (void)snprintf(text, sizeof(text), "TF  /  %03u — %03u",
        next->current_index == MUSIC_INDEX_NONE ? 0U : (unsigned)next->current_index + 1U, (unsigned)next->count);
    product_ui_music_text_sync(s_music_page.track_number, PRODUCT_UI_MUSIC_TEXT_TRACK_NUMBER, text);
    (void)snprintf(text, sizeof(text), "%u:%02u", (unsigned)(next->position_seconds / 60U),
        (unsigned)(next->position_seconds % 60U));
    product_ui_music_text_sync(s_music_page.elapsed, PRODUCT_UI_MUSIC_TEXT_ELAPSED, text);
    if (next->duration_seconds) (void)snprintf(text, sizeof(text), "%u:%02u",
        (unsigned)(next->duration_seconds / 60U), (unsigned)(next->duration_seconds % 60U));
    else (void)snprintf(text, sizeof(text), "--:--");
    product_ui_music_text_sync(s_music_page.duration, PRODUCT_UI_MUSIC_TEXT_DURATION, text);
    lv_bar_set_value(s_music_page.progress, next->duration_seconds ?
        (int32_t)((uint64_t)next->position_seconds * 1000U / next->duration_seconds) : 0, LV_ANIM_OFF);
    {
        const void *icon = product_ui_icon_get_sized(next->state == MUSIC_PLAYING ? PRODUCT_UI_ICON_CC_PAUSE : PRODUCT_UI_ICON_CC_PLAY, 24U);
        const void *mini_icon = product_ui_icon_get_sized(next->state == MUSIC_PLAYING ? PRODUCT_UI_ICON_CC_PAUSE : PRODUCT_UI_ICON_CC_PLAY, 20U);
        if (lv_image_get_src(s_music_page.toggle_icon) != icon) lv_image_set_src(s_music_page.toggle_icon, icon);
        if (lv_image_get_src(s_music_page.mini_toggle_icon) != mini_icon) lv_image_set_src(s_music_page.mini_toggle_icon, mini_icon);
    }
    can_transport = next->ready && next->mounted && next->current_index != MUSIC_INDEX_NONE && !next->held && !next->busy;
    product_ui_music_enabled(s_music_page.previous, can_transport);
    product_ui_music_enabled(s_music_page.next, can_transport);
    product_ui_music_enabled(s_music_page.toggle, can_transport && (next->state == MUSIC_PLAYING || next->can_resume));
    product_ui_music_enabled(s_music_page.mini_toggle, can_transport && (next->state == MUSIC_PLAYING || next->can_resume));
    product_ui_music_enabled(s_music_page.page_previous, s_music_page.window.first > 0U && !next->busy);
    product_ui_music_enabled(s_music_page.page_next, s_music_page.window.first + MUSIC_PAGE_CAPACITY < next->count && !next->busy);
    product_ui_music_enabled(s_music_page.refresh, next->ready && next->online && !next->held && !next->busy);
}

/**
 * @brief 取得音乐快照，更新窗口身份后刷新控件。
 */
static void product_ui_music_refresh(void)
{
    music_snapshot_t next;
    music_service_get_snapshot(&next);
    product_ui_music_refresh_window(&next);
    product_ui_music_render();
}

/**
 * @brief 创建指定主题字体及颜色的空标签。
 * @param[in,out] parent 标签父对象。
 * @param[in] font 主题字体标记。
 * @param[in] color 主题颜色标记。
 * @return 成功返回父对象拥有的标签，失败返回 NULL。
 */
static lv_obj_t *product_ui_music_label(lv_obj_t *parent, product_ui_font_e font, product_ui_color_e color)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label != NULL) {
        lv_obj_set_style_text_font(label, product_ui_font(font), 0);
        lv_obj_set_style_text_color(label, product_ui_color(color), 0);
        lv_label_set_text(label, "");
    }
    return label;
}

/**
 * @brief 创建具名页面动作按钮及静态图标或文字子对象。
 * @param[in,out] parent 按钮父对象。
 * @param[in] text 无专用图标时使用的本次借用文字。
 * @param[in] action 独立页面动作编号，不是服务命令枚举。
 * @param[out] out_icon 可为空；成功时接收按钮图标或标签子对象，失败不写入。
 * @return 成功返回父对象拥有的按钮，失败返回 NULL，并删除已创建的局部按钮。
 */
static lv_obj_t *product_ui_music_button(lv_obj_t *parent, const char *text, uintptr_t action, lv_obj_t **out_icon)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *caption;
    uint8_t primary = parent == s_music_page.player && action == PRODUCT_UI_MUSIC_TOGGLE_TRACK;
    uint8_t dark = parent == s_music_page.player || parent == s_music_page.dock;
    product_ui_icon_id_t icon = PRODUCT_UI_ICON_COUNT;
    if (button == NULL) return NULL;
    product_ui_button_style(button, primary ? PRODUCT_UI_BUTTON_PRIMARY :
        dark ? PRODUCT_UI_BUTTON_ICON_DARK : PRODUCT_UI_BUTTON_ICON_LIGHT);
    lv_obj_set_size(button, primary ? 44 : PRODUCT_UI_TOUCH_MIN, primary ? 44 : PRODUCT_UI_TOUCH_MIN);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_opa(button, LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_add_event_cb(button, product_ui_music_action, LV_EVENT_CLICKED, (void *)action);
    switch (action) {
    case PRODUCT_UI_MUSIC_TOGGLE_TRACK: icon = PRODUCT_UI_ICON_CC_PLAY; break;
    case PRODUCT_UI_MUSIC_PREVIOUS_TRACK: icon = PRODUCT_UI_ICON_CC_PREVIOUS; break;
    case PRODUCT_UI_MUSIC_NEXT_TRACK: icon = PRODUCT_UI_ICON_CC_NEXT; break;
    case PRODUCT_UI_MUSIC_SHOW_LIBRARY: icon = PRODUCT_UI_ICON_MEDIA_LIST; break;
    case PRODUCT_UI_MUSIC_OPEN_SOUND: icon = PRODUCT_UI_ICON_CC_VOLUME; break;
    default: break;
    }
    if (icon != PRODUCT_UI_ICON_COUNT) {
        const lv_image_dsc_t *source = product_ui_icon_get_sized(icon, primary ? 24U : 20U);
        if (source == NULL) { lv_obj_delete(button); return NULL; }
        caption = lv_image_create(button);
        if (caption == NULL) { lv_obj_delete(button); return NULL; }
        lv_image_set_src(caption, source);
        lv_obj_set_style_image_recolor(caption, product_ui_color(dark ?
            PRODUCT_UI_COLOR_ON_MUSIC_SURFACE : PRODUCT_UI_COLOR_MUSIC_LIST_GLYPH), 0);
        lv_obj_set_style_image_recolor_opa(caption, LV_OPA_COVER, 0);
        lv_obj_remove_flag(caption, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    } else {
        caption = product_ui_music_label(button, PRODUCT_UI_FONT_BODY,
            dark ? PRODUCT_UI_COLOR_ON_MUSIC_SURFACE : PRODUCT_UI_COLOR_MUSIC_LIST_GLYPH);
        if (caption == NULL) { lv_obj_delete(button); return NULL; }
        lv_label_set_text(caption, text);
    }
    lv_obj_center(caption);
    if (out_icon != NULL) *out_icon = caption;
    return button;
}

/**
 * @brief 创建歌曲库、有限复用列表、分页控制和迷你播放器。
 * @return 完整创建返回 0，任一步失败返回 -1，部分对象仍属页面树。
 */
static int product_ui_music_library_create(void)
{
    lv_obj_t *header, *cover, *note;
    product_ui_virtual_list_spec_t list = {.row_height = PRODUCT_UI_ROW_SINGLE_H,
        .bind_cb = product_ui_music_bind_row, .action_cb = product_ui_music_select,
        .text_caches = s_music_row_text};
    s_music_page.library = lv_obj_create(s_music_page.view.content);
    if (!s_music_page.library) return -1;
    lv_obj_remove_style_all(s_music_page.library);
    lv_obj_set_size(s_music_page.library, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_music_page.library, LV_FLEX_FLOW_COLUMN);
    header = lv_obj_create(s_music_page.library);
    if (!header) return -1;
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 36);
    s_music_page.library_status = product_ui_music_label(header, PRODUCT_UI_FONT_CAPTION, PRODUCT_UI_COLOR_MUSIC_LIST_MUTED);
    s_music_page.pages = product_ui_music_label(header, PRODUCT_UI_FONT_CAPTION, PRODUCT_UI_COLOR_MUSIC_LIST_MUTED);
    s_music_page.refresh = product_ui_music_button(header, LV_SYMBOL_REFRESH, PRODUCT_UI_MUSIC_RESCAN_LIBRARY, NULL);
    s_music_page.page_previous = product_ui_music_button(header, LV_SYMBOL_LEFT, PRODUCT_UI_MUSIC_PAGE_PREVIOUS, NULL);
    s_music_page.page_next = product_ui_music_button(header, LV_SYMBOL_RIGHT, PRODUCT_UI_MUSIC_PAGE_NEXT, NULL);
    if (!s_music_page.library_status || !s_music_page.pages || !s_music_page.refresh ||
        !s_music_page.page_previous || !s_music_page.page_next) return -1;
    lv_obj_set_width(s_music_page.library_status, 126);
    lv_label_set_long_mode(s_music_page.library_status, LV_LABEL_LONG_DOT);
    lv_obj_align(s_music_page.library_status, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(s_music_page.refresh, LV_ALIGN_LEFT_MID, 126, 0);
    lv_obj_align(s_music_page.page_previous, LV_ALIGN_RIGHT_MID, -84, 0);
    lv_obj_align(s_music_page.pages, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_obj_align(s_music_page.page_next, LV_ALIGN_RIGHT_MID, 0, 0);
    if (product_ui_virtual_list_create(s_music_page.library, &s_music_page.list, &list) != 0) return -1;
    lv_obj_set_height(s_music_page.list.root, 0);
    lv_obj_set_flex_grow(s_music_page.list.root, 1U);
    s_music_page.dock = lv_obj_create(s_music_page.library);
    if (!s_music_page.dock) return -1;
    lv_obj_remove_style_all(s_music_page.dock);
    lv_obj_set_size(s_music_page.dock, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(s_music_page.dock, product_ui_color(PRODUCT_UI_COLOR_MUSIC_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_music_page.dock, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_music_page.dock, 10, 0);
    lv_obj_add_flag(s_music_page.dock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_music_page.dock, product_ui_music_action, LV_EVENT_CLICKED, (void *)(uintptr_t)PRODUCT_UI_MUSIC_SHOW_PLAYER);
    cover = lv_obj_create(s_music_page.dock);
    if (!cover) return -1;
    lv_obj_remove_style_all(cover);
    lv_obj_set_size(cover, 28, 28);
    lv_obj_align(cover, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_radius(cover, 7, 0);
    lv_obj_set_style_bg_color(cover, product_ui_color(PRODUCT_UI_COLOR_ACTION), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    note = product_ui_music_label(cover, PRODUCT_UI_FONT_BODY, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE);
    s_music_page.mini_title = product_ui_music_label(s_music_page.dock, PRODUCT_UI_FONT_BODY, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE);
    s_music_page.mini_toggle = product_ui_music_button(s_music_page.dock, LV_SYMBOL_PLAY, PRODUCT_UI_MUSIC_TOGGLE_TRACK,
                                                      &s_music_page.mini_toggle_icon);
    if (!note || !s_music_page.mini_title || !s_music_page.mini_toggle) return -1;
    lv_label_set_text(note, LV_SYMBOL_AUDIO); lv_obj_center(note);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(s_music_page.mini_title, 196);
    lv_label_set_long_mode(s_music_page.mini_title, LV_LABEL_LONG_DOT);
    lv_obj_align(s_music_page.mini_title, LV_ALIGN_LEFT_MID, 46, 0);
    lv_obj_align(s_music_page.mini_toggle, LV_ALIGN_RIGHT_MID, -4, 0);
    s_music_page.empty = product_ui_music_label(s_music_page.library, PRODUCT_UI_FONT_BODY, PRODUCT_UI_COLOR_MUSIC_LIST_MUTED);
    if (!s_music_page.empty) return -1;
    lv_obj_add_flag(s_music_page.empty, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_music_page.empty, 270);
    lv_obj_set_style_text_align(s_music_page.empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_music_page.empty, LV_ALIGN_CENTER, 0, 0);
    return 0;
}

/**
 * @brief 创建播放器的曲目信息、进度及传输操作控件。
 * @return 完整创建返回 0，任一步失败返回 -1，部分对象仍属页面树。
 */
static int product_ui_music_player_create(void)
{
    lv_obj_t *disc, *ring, *hub, *note, *list_button, *volume;
    s_music_page.player = lv_obj_create(s_music_page.view.content);
    if (!s_music_page.player) return -1;
    lv_obj_remove_style_all(s_music_page.player);
    lv_obj_set_size(s_music_page.player, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_music_page.player, product_ui_color(PRODUCT_UI_COLOR_MUSIC_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_music_page.player, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_music_page.player, 12, 0);
    disc = lv_obj_create(s_music_page.player);
    if (!disc) return -1;
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 64, 64); lv_obj_set_pos(disc, 16, 14);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, product_ui_color(PRODUCT_UI_COLOR_MUSIC_DISC), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 2, 0);
    lv_obj_set_style_border_color(disc, product_ui_color(PRODUCT_UI_COLOR_MUSIC_OUTLINE), 0);
    ring = lv_obj_create(disc); hub = lv_obj_create(disc);
    if (!ring || !hub) return -1;
    lv_obj_remove_style_all(ring); lv_obj_remove_style_all(hub);
    lv_obj_set_size(ring, 48, 48); lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, product_ui_color(PRODUCT_UI_COLOR_MUSIC_DISC_RING), 0);
    lv_obj_set_size(hub, 28, 28); lv_obj_center(hub);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, product_ui_color(PRODUCT_UI_COLOR_ACTION), 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    note = product_ui_music_label(hub, PRODUCT_UI_FONT_BODY, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE);
    s_music_page.track_number = product_ui_music_label(s_music_page.player, PRODUCT_UI_FONT_CAPTION, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE_MUTED);
    s_music_page.title = product_ui_music_label(s_music_page.player, PRODUCT_UI_FONT_BODY, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE);
    s_music_page.status = product_ui_music_label(s_music_page.player, PRODUCT_UI_FONT_CAPTION, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE_MUTED);
    s_music_page.elapsed = product_ui_music_label(s_music_page.player, PRODUCT_UI_FONT_CAPTION, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE_MUTED);
    s_music_page.duration = product_ui_music_label(s_music_page.player, PRODUCT_UI_FONT_CAPTION, PRODUCT_UI_COLOR_ON_MUSIC_SURFACE_MUTED);
    if (!note || !s_music_page.track_number || !s_music_page.title || !s_music_page.status ||
        !s_music_page.elapsed || !s_music_page.duration) return -1;
    lv_label_set_text(note, LV_SYMBOL_AUDIO); lv_obj_center(note);
    lv_obj_set_pos(s_music_page.track_number, 94, 14);
    lv_obj_set_pos(s_music_page.title, 94, 32); lv_obj_set_size(s_music_page.title, 186, 34);
    lv_label_set_long_mode(s_music_page.title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_music_page.status, 94, 68); lv_obj_set_width(s_music_page.status, 186);
    lv_label_set_long_mode(s_music_page.status, LV_LABEL_LONG_DOT);
    s_music_page.progress = lv_bar_create(s_music_page.player);
    if (!s_music_page.progress) return -1;
    lv_obj_set_size(s_music_page.progress, 264, 3);
    lv_obj_align(s_music_page.progress, LV_ALIGN_BOTTOM_MID, 0, -66);
    lv_bar_set_range(s_music_page.progress, 0, 1000);
    lv_obj_set_style_bg_color(s_music_page.progress, product_ui_color(PRODUCT_UI_COLOR_MUSIC_OUTLINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_music_page.progress, product_ui_color(PRODUCT_UI_COLOR_ACTION), LV_PART_INDICATOR);
    lv_obj_align(s_music_page.elapsed, LV_ALIGN_BOTTOM_LEFT, 16, -49);
    lv_obj_align(s_music_page.duration, LV_ALIGN_BOTTOM_RIGHT, -16, -49);
    list_button = product_ui_music_button(s_music_page.player, LV_SYMBOL_LIST, PRODUCT_UI_MUSIC_SHOW_LIBRARY, NULL);
    volume = product_ui_music_button(s_music_page.player, LV_SYMBOL_VOLUME_MAX, PRODUCT_UI_MUSIC_OPEN_SOUND, NULL);
    s_music_page.previous = product_ui_music_button(s_music_page.player, LV_SYMBOL_PREV, PRODUCT_UI_MUSIC_PREVIOUS_TRACK, NULL);
    s_music_page.toggle = product_ui_music_button(s_music_page.player, LV_SYMBOL_PLAY, PRODUCT_UI_MUSIC_TOGGLE_TRACK, &s_music_page.toggle_icon);
    s_music_page.next = product_ui_music_button(s_music_page.player, LV_SYMBOL_NEXT, PRODUCT_UI_MUSIC_NEXT_TRACK, NULL);
    if (!list_button || !volume || !s_music_page.previous || !s_music_page.toggle || !s_music_page.next) return -1;
    lv_obj_align(list_button, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_align(volume, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    lv_obj_align(s_music_page.previous, LV_ALIGN_BOTTOM_MID, -58, -6);
    lv_obj_align(s_music_page.toggle, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_align(s_music_page.next, LV_ALIGN_BOTTOM_MID, 58, -6);
    return 0;
}

/**
 * @brief 创建歌曲库及播放器，首轮读取投影后按是否有当前曲目选择视图。
 * @return 成功返回 0，任一内容创建失败返回 -1。
 */
static int product_ui_music_content(void)
{
    if (product_ui_music_library_create() != 0 || product_ui_music_player_create() != 0) return -1;
    product_ui_music_show_player(0U);
    product_ui_music_refresh();
    if (s_music_page.service.current_index != MUSIC_INDEX_NONE) product_ui_music_show_player(1U);
    return 0;
}
#endif

/**
 * @brief 创建音乐框架及已启用的播放器内容，功能关闭时创建不可用反馈。
 */
int product_ui_music_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {.title = "音乐", .back_title = "桌面", .show_back = 1};
    memset(&s_music_page, 0, sizeof(s_music_page));
#if D_PRODUCT_MUSIC_ENABLE
    memset(s_music_row_text, 0, sizeof(s_music_row_text));
#endif
    if (product_ui_page_frame_create_ex(parent, &spec, &s_music_page.view) != 0) return -1;
    lv_obj_set_style_pad_ver(s_music_page.view.content, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_clear_flag(s_music_page.view.content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(s_music_page.view.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_music_page.view.content, PRODUCT_UI_SPACE_XS, 0);
#if D_PRODUCT_MUSIC_ENABLE
    return product_ui_music_content();
#else
    return product_ui_feedback_create(s_music_page.view.content, PRODUCT_UI_FEEDBACK_ERROR,
        "音乐未启用", "当前固件未开启本地音乐") != NULL ? 0 : -1;
#endif
}

/**
 * @brief 推进公共 UI 和列表，按限定周期刷新当前音乐投影。
 * @param[in] elapsed_ms 本轮页面经过的毫秒数。
 */
static void product_ui_music_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
#if D_PRODUCT_MUSIC_ENABLE
    product_ui_virtual_list_update(&s_music_page.list, elapsed_ms);
    s_music_page.refresh_ms += elapsed_ms;
    if (s_music_page.refresh_ms >= 250U) {
        s_music_page.refresh_ms = 0U;
        product_ui_music_refresh();
    }
#endif
}

/**
 * @brief 解除页面框架、缓存及控件引用，不停止后台播放。
 * @note 页面管理器删除对象，音乐服务继续拥有目录和播放器资源。
 */
static void product_ui_music_page_destroy(void)
{
    product_ui_page_view_reset(&s_music_page.view);
    memset(&s_music_page, 0, sizeof(s_music_page));
#if D_PRODUCT_MUSIC_ENABLE
    memset(s_music_row_text, 0, sizeof(s_music_row_text));
#endif
}
static const page_ops_t s_product_ui_music_ops = {
    .create = product_ui_music_page_create, .destroy = product_ui_music_page_destroy,
    .update = product_ui_music_page_update, .flags = PAGE_FLAG_NO_GESTURE,
};
static page_desc_t s_product_ui_music_desc = {.name = "ProductMusic", .ops = &s_product_ui_music_ops};
/**
 * @brief 将静态音乐页面描述注册到页面管理器。
 */
int product_ui_music_register_page(int page_type)
{
    s_product_ui_music_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_music_desc);
}
D_UI_MODULE_DEFINE(music, "音乐", LV_SYMBOL_AUDIO, product_ui_music_register_page, NULL);
#endif
