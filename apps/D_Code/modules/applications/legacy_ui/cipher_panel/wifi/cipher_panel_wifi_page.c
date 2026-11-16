/** @file cipher_panel_wifi_page.c @brief 旧根 UI 的 WiFi 状态、扫描及显式请求完成观察。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_CIPHER_PANEL)

#include "modules/applications/legacy_ui/cipher_panel/pages/cipher_panel_pages.h"
#include "modules/applications/legacy_ui/cipher_panel/wifi/cipher_panel_wifi_input.h"
#include "framework/d_ui_font.h"
#if D_FEATURE_WIFI
#include "modules/services/wifi/wifi_service.h"
#endif
#include <string.h>

#define CIPHER_WIFI_LIST_W      276
#define CIPHER_WIFI_ROW_H       64
#define CIPHER_WIFI_STATUS_Y    88
#define CIPHER_WIFI_STATUS_H    98
#define CIPHER_WIFI_SCAN_Y      192
#define CIPHER_WIFI_LIST_Y      212
#define CIPHER_WIFI_LIST_H      214
#define CIPHER_WIFI_STATUS_REFRESH_MS 2000U

#if D_FEATURE_WIFI
static lv_obj_t *s_wifi_page = NULL;
static lv_obj_t *s_wifi_status_card = NULL;
static lv_obj_t *s_wifi_scan_label = NULL;
static lv_obj_t *s_wifi_list = NULL;
static wifi_service_scan_result_t s_wifi_results[WIFI_SERVICE_SCAN_MAX_RESULTS];
static uint8_t s_wifi_result_count = 0;
static uint8_t s_wifi_row_index[WIFI_SERVICE_SCAN_MAX_RESULTS];
static uint32_t s_wifi_seen_scan_version = 0xFFFFFFFFU;
static uint32_t s_wifi_seen_status_version = 0xFFFFFFFFU;
static uint32_t s_wifi_status_refresh_ms = 0;
static uint32_t s_wifi_interest_id;
static wifi_service_request_kind_e s_wifi_interest_kind;

static void cipher_wifi_render_scan(void);
static void cipher_wifi_render_status(void);

/**
 * @brief 状态卡根删除时撤销本页请求兴趣及全部输入／列表引用。
 * @param[in] event 状态卡 DELETE 事件。
 * @note 只撤销本页观察身份，不取消 WiFi 服务已经受理的工作。
 */
static void cipher_wifi_page_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_wifi_status_card) return;
    s_wifi_interest_id = 0U;
    s_wifi_interest_kind = WIFI_SERVICE_REQUEST_NONE;
    s_wifi_status_card = NULL;
    s_wifi_scan_label = NULL;
    s_wifi_list = NULL;
    s_wifi_page = NULL;
    cipher_wifi_input_reset();
}

/**
 * @brief 把扫描阶段映射为静态既有状态文字。
 * @param[in] state 服务扫描阶段。
 * @return 阶段说明的借用指针，未列状态为刷新提示。
 */
static const char *cipher_wifi_scan_text(wifi_service_scan_state_t state)
{
    switch (state) {
    case WIFI_SERVICE_SCAN_SCANNING:
        return "Scanning...";
    case WIFI_SERVICE_SCAN_BUSY:
        return "WiFi busy";
    case WIFI_SERVICE_SCAN_NOT_READY:
        return "WiFi not ready";
    case WIFI_SERVICE_SCAN_TIMEOUT:
        return "Scan timeout";
    case WIFI_SERVICE_SCAN_ERROR:
        return "Scan error";
    default:
        return "Tap refresh";
    }
}

/**
 * @brief 把连接错误映射为静态既有说明。
 * @param[in] error 服务连接错误枚举。
 * @return 错误文字的借用指针，未列值返回既有无错误说明。
 */
static const char *cipher_wifi_error_text(wifi_service_conn_error_t error)
{
    switch (error) {
    case WIFI_SERVICE_ERR_NOT_READY:
        return "WiFi not ready";
    case WIFI_SERVICE_ERR_BUSY:
        return "WiFi busy";
    case WIFI_SERVICE_ERR_NO_SAVED:
        return "No saved network";
    case WIFI_SERVICE_ERR_NOT_FOUND:
        return "SSID not found";
    case WIFI_SERVICE_ERR_ASSOC_FAIL:
        return "Associate failed";
    case WIFI_SERVICE_ERR_ASSOC_TIMEOUT:
        return "Associate timeout";
    case WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT:
        return "Switch timeout (old link)";
    case WIFI_SERVICE_ERR_DHCP_TIMEOUT:
        return "DHCP timeout";
    case WIFI_SERVICE_ERR_DISCONNECTED:
        return "Disconnected";
    default:
        return "No error";
    }
}

/**
 * @brief 把请求受理错误映射为静态连接说明。
 * @param[in] err 请求受理结果。
 * @return 对应说明的借用指针。
 */
static const char *cipher_wifi_connect_error_text(d_err_t err)
{
    switch (err) {
    case D_ERR_BUSY:
        return "WiFi busy";
    case D_ERR_NOT_READY:
        return "WiFi not ready";
    case D_ERR_NOT_FOUND:
        return "No saved network";
    case D_ERR_INVALID_ARG:
        return "Invalid network";
    default:
        return "Connect failed";
    }
}

/**
 * @brief 密码层打开时更新其提示，否则更新当前扫描说明。
 * @param[in] text 本次复制的文字。
 * @param[in] color 输入提示使用的 RGB 颜色，扫描标签分支不改颜色。
 */
static void cipher_wifi_set_input_message(const char *text, uint32_t color)
{
    if (cipher_wifi_input_is_open() != 0U) {
        cipher_wifi_input_set_message(text, color);
        return;
    }
    if (s_wifi_scan_label != NULL) {
        lv_label_set_text(s_wifi_scan_label, text);
    }
}

/**
 * @brief 验证长度后原样复制 SSID／密码并受理显式连接请求。
 * @param[in] ssid 借用目标 SSID，保留原始身份字节。
 * @param[in] pwd 借用密码，开放网络使用非空指针的空串。
 * @return 有效受理为 1，参数或服务失败为 0。
 * @note 成功记录兴趣编号及种类，真实完成由页面 update 继续观察。
 */
static uint8_t cipher_wifi_start_connect(const char *ssid, const char *pwd)
{
    wifi_service_request_t request = {.kind = WIFI_SERVICE_REQUEST_CONNECT};
    uint32_t ticket = 0U;
    d_err_t err;
    if (ssid == NULL || pwd == NULL || strlen(ssid) >= sizeof(request.ssid) ||
        strlen(pwd) >= sizeof(request.pwd)) return 0U;
    strcpy(request.ssid, ssid);
    strcpy(request.pwd, pwd);
    err = wifi_service_request_submit(&request, &ticket);

    if (err == D_OK) {
        s_wifi_interest_id = ticket;
        s_wifi_interest_kind = request.kind;
        cipher_wifi_render_status();
        return 1U;
    }
    cipher_wifi_set_input_message(cipher_wifi_connect_error_text(err), CIPHER_WARN);
    cipher_wifi_render_status();
    return 0U;
}

/**
 * @brief 把密码面板的本次借用输入转交显式连接受理。
 * @param[in] ssid 本次借用网络身份。
 * @param[in] pwd 本次借用密码。
 * @param[in] user_data 密码回调上下文，当前未使用。
 * @return 连接受理成功为 1，否则为 0。
 */
static uint8_t cipher_wifi_connect_input_cb(const char *ssid, const char *pwd,
                                            void *user_data)
{
    (void)user_data;
    return cipher_wifi_start_connect(ssid, pwd);
}

/**
 * @brief 创建旧根状态卡操作按钮，回调可选。
 * @param[in,out] parent 按钮父对象。
 * @param[in] text 本次复制的文字。
 * @param[in] x 局部横坐标。
 * @param[in] y 局部纵坐标。
 * @param[in] w 按钮宽度。
 * @param[in] accent RGB 强调色。
 * @param[in] cb 可为空的持久点击回调。
 * @return 成功返回父树拥有的按钮，按钮创建失败为 NULL。
 */
static lv_obj_t *cipher_wifi_action_button(lv_obj_t *parent, const char *text,
                                           int x, int y, int w, uint32_t accent,
                                           lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);

    if (btn == NULL) {
        return NULL;
    }
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, 28);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x03101B), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    cipher_text(btn, text, 0, 7, w, &lv_font_montserrat_12,
                accent, LV_TEXT_ALIGN_CENTER);
    return btn;
}

/**
 * @brief 受理重连已存网络，并记录本页兴趣编号及即时受理说明。
 * @param[in] e 重连事件，当前未读取载荷。
 */
static void cipher_wifi_reconnect_event(lv_event_t *e)
{
    wifi_service_request_t request = {.kind = WIFI_SERVICE_REQUEST_RECONNECT};
    uint32_t ticket = 0U;
    d_err_t result;
    (void)e;
    result = wifi_service_request_submit(&request, &ticket);
    if (result == D_OK) {
        s_wifi_interest_id = ticket;
        s_wifi_interest_kind = request.kind;
        cipher_wifi_set_input_message("Reconnect requested", CIPHER_ACCENT);
    } else cipher_wifi_set_input_message(cipher_wifi_connect_error_text(result), CIPHER_WARN);
    cipher_wifi_render_status();
}

/**
 * @brief 关闭密码层并受理忘记网络，真实清除结果留给完成确认。
 * @param[in] e 忘记网络事件，当前未读取载荷。
 */
static void cipher_wifi_forget_event(lv_event_t *e)
{
    wifi_service_request_t request = {.kind = WIFI_SERVICE_REQUEST_FORGET};
    uint32_t ticket = 0U;
    d_err_t result;
    (void)e;
    cipher_wifi_input_close();
    result = wifi_service_request_submit(&request, &ticket);
    if (result == D_OK) {
        s_wifi_interest_id = ticket;
        s_wifi_interest_kind = request.kind;
        cipher_wifi_set_input_message("Forgetting...", CIPHER_ACCENT);
    } else cipher_wifi_set_input_message(cipher_wifi_connect_error_text(result), CIPHER_WARN);
    cipher_wifi_render_status();
}

/**
 * @brief 按当前扫描下标选择网络，开放网络直接受理，其他打开密码层。
 * @param[in] e 行点击事件，用户数据借用静态下标字节。
 */
static void cipher_wifi_row_event(lv_event_t *e)
{
    uint8_t *index = (uint8_t *)lv_event_get_user_data(e);
    const wifi_service_scan_result_t *item;

    if ((index == NULL) || (*index >= s_wifi_result_count)) {
        return;
    }
    item = &s_wifi_results[*index];
    if (wifi_service_auth_requires_password(item->auth_mode) == 0U) {
        (void)cipher_wifi_start_connect(item->ssid, "");
        return;
    }
    cipher_wifi_input_open(s_wifi_page, item, cipher_wifi_connect_input_cb, NULL);
}

/**
 * @brief 在现有状态卡中创建连接、信号、获取 IP 或错误说明。
 * @param[in,out] card 状态卡父对象。
 * @param[in] status 本次借用的内存状态快照。
 */
static void cipher_wifi_render_status_text(lv_obj_t *card,
                                           const wifi_service_status_t *status)
{
    if ((card == NULL) || (status == NULL)) {
        return;
    }
    if (status->state == WIFI_SERVICE_CONN_CONNECTED) {
        lv_obj_t *signal_label;

        cipher_text(card, status->ssid, 18, 38, 170, d_ui_font_get_default(),
                    CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
        signal_label = cipher_text(card, "", 18, 64, 170,
                                   &lv_font_montserrat_12, CIPHER_MUTED,
                                   LV_TEXT_ALIGN_LEFT);
        if (signal_label != NULL) {
            lv_label_set_text_fmt(signal_label, "%d dBm  %d%%",
                                  (int)status->rssi,
                                  (int)status->signal_strength);
        }
    } else if ((status->state == WIFI_SERVICE_CONN_CONNECTING) ||
               (status->state == WIFI_SERVICE_CONN_ASSOCIATED)) {
        cipher_text(card, status->ssid[0] ? status->ssid : "WiFi",
                    18, 42, 170, d_ui_font_get_default(), CIPHER_TEXT,
                    LV_TEXT_ALIGN_LEFT);
        cipher_text(card,
                    status->state == WIFI_SERVICE_CONN_ASSOCIATED ? "IP setup..." : "Connecting...",
                    18, 66, 170, &lv_font_montserrat_12, CIPHER_ACCENT,
                    LV_TEXT_ALIGN_LEFT);
    } else if (status->state == WIFI_SERVICE_CONN_ERROR) {
        cipher_text(card, cipher_wifi_error_text(status->error),
                    18, 42, 170, &lv_font_montserrat_14, CIPHER_WARN,
                    LV_TEXT_ALIGN_LEFT);
    } else {
        cipher_text(card, "WiFi not connected", 18, 44, 170,
                    &lv_font_montserrat_14, CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    }
}

/**
 * @brief 状态版本变化时清空并重建当前网络文字及重连／忘记操作。
 * @note 只读取服务内存缓存；当前提前记录已见版本，不提供子控件分配失败的独立重试确认。
 */
static void cipher_wifi_render_status(void)
{
    wifi_service_status_t status;
    uint32_t version;

    if (s_wifi_status_card == NULL) {
        return;
    }
    version = wifi_service_get_status_version();
    if (version == s_wifi_seen_status_version) {
        return;
    }
    s_wifi_seen_status_version = version;
    wifi_service_get_cached_status(&status);
    lv_obj_clean(s_wifi_status_card);

    cipher_rect(s_wifi_status_card, 12, 0, 90, 2, CIPHER_ACCENT_2, 0);
    cipher_text(s_wifi_status_card, "CURRENT NETWORK", 18, 14, 164,
                &lv_font_montserrat_12, CIPHER_MUTED, LV_TEXT_ALIGN_LEFT);
    cipher_wifi_render_status_text(s_wifi_status_card, &status);

    cipher_wifi_action_button(s_wifi_status_card, "Reconnect", 178, 22, 80,
                              CIPHER_ACCENT, cipher_wifi_reconnect_event);
    cipher_wifi_action_button(s_wifi_status_card, "Forget", 178, 58, 80,
                              CIPHER_WARN, cipher_wifi_forget_event);
}

/**
 * @brief 创建当前扫描结果行，绑定静态下标上下文并显示信号及认证说明。
 * @param[in,out] parent 扫描列表父容器。
 * @param[in] y 行纵向位置。
 * @param[in] index 当前扫描结果下标。
 */
static void cipher_wifi_add_row(lv_obj_t *parent, int y, uint8_t index)
{
    lv_obj_t *row;
    lv_obj_t *label;
    const wifi_service_scan_result_t *item;

    if ((parent == NULL) || (index >= s_wifi_result_count)) {
        return;
    }
    item = &s_wifi_results[index];

    row = cipher_row(parent, 8, y, CIPHER_WIFI_LIST_W - 16, CIPHER_WIFI_ROW_H);
    if (row == NULL) {
        return;
    }

    s_wifi_row_index[index] = index;
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cipher_wifi_row_event, LV_EVENT_CLICKED,
                        &s_wifi_row_index[index]);
    cipher_rect(row, 0, 0, 3, CIPHER_WIFI_ROW_H, CIPHER_ACCENT_2, 0);
    label = cipher_text(row, "", 14, 8, 224, d_ui_font_get_default(),
                        CIPHER_TEXT, LV_TEXT_ALIGN_LEFT);
    if (label != NULL) {
        lv_label_set_text_fmt(label, "%s\n%d dBm  %d%%  %s",
                              item->ssid, (int)item->rssi,
                              (int)item->signal_strength,
                              wifi_service_auth_name(item->auth_mode));
        lv_obj_set_style_text_line_space(label, 6, 0);
    }
    cipher_text(row, ">", 236, 22, 18, &lv_font_montserrat_14,
                CIPHER_ACCENT, LV_TEXT_ALIGN_RIGHT);
}

/**
 * @brief 扫描版本变化时复制有限结果并重建列表，只有 DONE 且非空时创建网络行。
 * @note 保留旧根 UI 的既有刷新策略，不提供 Product 虚拟列表的稳定条目键或部分呈现重试合同。
 */
static void cipher_wifi_render_scan(void)
{
    wifi_service_scan_state_t state;
    uint32_t version;
    uint8_t i;

    if (s_wifi_list == NULL) {
        return;
    }

    state = wifi_service_scan_get_state();
    version = wifi_service_scan_get_version();
    if (version == s_wifi_seen_scan_version) {
        return;
    }

    s_wifi_result_count = wifi_service_scan_copy_results(s_wifi_results,
                                                         WIFI_SERVICE_SCAN_MAX_RESULTS);
    s_wifi_seen_scan_version = version;
    if (s_wifi_scan_label != NULL) {
        if (state == WIFI_SERVICE_SCAN_DONE) {
            lv_label_set_text_fmt(s_wifi_scan_label, "Found %d networks",
                                  (int)s_wifi_result_count);
        } else {
            lv_label_set_text(s_wifi_scan_label, cipher_wifi_scan_text(state));
        }
    }
    lv_obj_clean(s_wifi_list);

    if ((state != WIFI_SERVICE_SCAN_DONE) || (s_wifi_result_count == 0U)) {
        cipher_text(s_wifi_list, cipher_wifi_scan_text(state), 0, 88, CIPHER_WIFI_LIST_W,
                    &lv_font_montserrat_14, CIPHER_MUTED, LV_TEXT_ALIGN_CENTER);
        return;
    }

    for (i = 0; i < s_wifi_result_count; i++) {
        int y = 8 + (int)i * 74;
        cipher_wifi_add_row(s_wifi_list, y, i);
    }
}
#endif

/**
 * @brief 创建旧根 WiFi 状态、扫描列表及操作，功能启用时受理首次扫描。
 */
void cipher_panel_show_wifi(lv_obj_t *parent)
{
#if D_FEATURE_WIFI
    s_wifi_page = parent;
    s_wifi_status_card = NULL;
    s_wifi_scan_label = NULL;
    s_wifi_list = NULL;
    cipher_wifi_input_reset();
    s_wifi_result_count = 0;
    s_wifi_seen_scan_version = 0xFFFFFFFFU;
    s_wifi_seen_status_version = 0xFFFFFFFFU;
    s_wifi_status_refresh_ms = 0;
    s_wifi_interest_id = 0U;
    s_wifi_interest_kind = WIFI_SERVICE_REQUEST_NONE;
#endif

    cipher_page_header(parent, "WiFi");

#if D_FEATURE_WIFI
    cipher_button(parent, "", LV_SYMBOL_REFRESH, 270, 44, 38, 34,
                  CIPHER_PAGE_WIFI, CIPHER_ACCENT_2);
    s_wifi_status_card = cipher_row(parent, 22, CIPHER_WIFI_STATUS_Y,
                                    CIPHER_WIFI_LIST_W, CIPHER_WIFI_STATUS_H);
    if (s_wifi_status_card != NULL)
        lv_obj_add_event_cb(s_wifi_status_card, cipher_wifi_page_deleted, LV_EVENT_DELETE, NULL);
    s_wifi_scan_label = cipher_text(parent, "Scanning...", 22, CIPHER_WIFI_SCAN_Y,
                                    276, &lv_font_montserrat_12, CIPHER_MUTED,
                                    LV_TEXT_ALIGN_LEFT);
    s_wifi_list = cipher_row(parent, 22, CIPHER_WIFI_LIST_Y,
                             CIPHER_WIFI_LIST_W, CIPHER_WIFI_LIST_H);
    if (s_wifi_list != NULL) {
        lv_obj_add_flag(s_wifi_list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_pad_top(s_wifi_list, 8, 0);
        lv_obj_set_style_pad_bottom(s_wifi_list, 8, 0);
        lv_obj_set_style_clip_corner(s_wifi_list, 1, 0);
    }

    {
        wifi_service_request_t request = {.kind = WIFI_SERVICE_REQUEST_SCAN};
        uint32_t ticket = 0U;
        if (wifi_service_request_submit(&request, &ticket) == D_OK) {
            s_wifi_interest_id = ticket;
            s_wifi_interest_kind = request.kind;
        }
    }
    cipher_wifi_render_status();
    cipher_wifi_render_scan();
#else
    cipher_text(parent, "WiFi disabled", 22, 128, 276,
                &lv_font_montserrat_14, CIPHER_MUTED, LV_TEXT_ALIGN_CENTER);
#endif
}

/**
 * @brief 消费匹配编号及种类的 WiFi 请求完成，按周期刷新状态及扫描。
 */
void cipher_panel_wifi_update(uint32_t elapsed_ms)
{
#if D_FEATURE_WIFI
    wifi_service_request_snapshot_t request;
    wifi_service_get_request_snapshot(&request);
    if (s_wifi_interest_id != 0U && request.completed_id == s_wifi_interest_id &&
        request.completed_kind == s_wifi_interest_kind) {
        wifi_service_request_kind_e kind = s_wifi_interest_kind;
        s_wifi_interest_id = 0U;
        s_wifi_interest_kind = WIFI_SERVICE_REQUEST_NONE;
        if (request.completed_result != D_OK) {
            cipher_wifi_set_input_message(request.completed_result == D_ERR_IO && kind == WIFI_SERVICE_REQUEST_FORGET ?
                "Clear credentials failed" : cipher_wifi_connect_error_text(request.completed_result), CIPHER_WARN);
        } else if (kind == WIFI_SERVICE_REQUEST_FORGET) {
            cipher_wifi_set_input_message("Forgot network", CIPHER_ACCENT);
        }
        s_wifi_seen_status_version = 0xFFFFFFFFU;
    }
    s_wifi_status_refresh_ms += elapsed_ms;
    if (s_wifi_status_refresh_ms >= CIPHER_WIFI_STATUS_REFRESH_MS) {
        s_wifi_status_refresh_ms = 0;
        s_wifi_seen_status_version = 0xFFFFFFFFU;
    }

    wifi_service_scan_update(elapsed_ms);
    cipher_wifi_render_status();
    cipher_wifi_render_scan();
#else
    (void)elapsed_ms;
#endif
}

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_CIPHER_PANEL) */
