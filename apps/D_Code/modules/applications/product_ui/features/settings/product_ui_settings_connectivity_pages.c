/** @file product_ui_settings_connectivity_pages.c @brief WiFi 屏幕配网详情及蓝牙能力占位，所有网络工作经服务边界受理。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/features/settings/product_ui_settings_details.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_details_common.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/shared/text/product_ui_text.h"
#include "modules/applications/product_ui/shared/theme/product_ui_theme.h"
#include "modules/applications/product_ui/runtime/connectivity/product_ui_connection_service.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

#if D_FEATURE_WIFI
#include "modules/services/wifi/wifi_service.h"
#endif

static product_ui_page_view_t s_wifi_page_view;
static product_ui_page_view_t s_bluetooth_page_view;

#if D_FEATURE_WIFI

#define PRODUCT_UI_WIFI_SCAN_MAX        WIFI_SERVICE_SCAN_MAX_RESULTS
#define PRODUCT_UI_WIFI_STATUS_FORCE_MS 2000U
#define PRODUCT_UI_WIFI_SSID_SHOW       WIFI_SERVICE_SCAN_SSID_MAX
#define PRODUCT_UI_WIFI_SUB_SHOW        40U
#define PRODUCT_UI_WIFI_PWD_MAX         PRODUCT_UI_CONNECTION_PWD_MAX
/* 320×240 横屏采用四行大触摸区 QWERTY 键盘。 */
#define PRODUCT_UI_WIFI_KB_H            152
#define PRODUCT_UI_WIFI_KB_Y            (PRODUCT_UI_SCREEN_H - PRODUCT_UI_WIFI_KB_H)
#define PRODUCT_UI_WIFI_TA_Y            40
#define PRODUCT_UI_WIFI_TA_H            40
#define PRODUCT_UI_WIFI_TA_X            12
#define PRODUCT_UI_WIFI_EYE_W           PRODUCT_UI_TOUCH_MIN
#define PRODUCT_UI_WIFI_EYE_GAP         8
/* 输入宽度扣除左右边距、可见性按钮和按钮间距。 */
#define PRODUCT_UI_WIFI_TA_W \
    (PRODUCT_UI_SCREEN_W - (PRODUCT_UI_WIFI_TA_X * 2) - \
     PRODUCT_UI_WIFI_EYE_W - PRODUCT_UI_WIFI_EYE_GAP)
#define PRODUCT_UI_WIFI_EYE_X \
    (PRODUCT_UI_WIFI_TA_X + PRODUCT_UI_WIFI_TA_W + PRODUCT_UI_WIFI_EYE_GAP)
#define PRODUCT_UI_WIFI_KB_CTRL(w)      (LV_KEYBOARD_CTRL_BUTTON_FLAGS | (w))

typedef struct {
    product_ui_page_view_t *page;
    lv_obj_t *status_label;
    lv_obj_t *scan_label;
    lv_obj_t *empty_label;
    lv_obj_t *pwd_layer;
    uint8_t is_pull_held;
    lv_obj_t *pwd_title;
    lv_obj_t *pwd_ta;
    lv_obj_t *pwd_eye_btn;
    lv_obj_t *pwd_eye_label;
    lv_obj_t *pwd_kb;
    product_ui_virtual_list_t list;
    product_ui_list_item_t slot_rows[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
    char slot_title[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT][PRODUCT_UI_WIFI_SSID_SHOW];
    char slot_sub[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT][PRODUCT_UI_WIFI_SUB_SHOW];
    wifi_service_scan_result_t results[PRODUCT_UI_WIFI_SCAN_MAX];
    uint32_t item_keys[PRODUCT_UI_WIFI_SCAN_MAX];
    uint32_t item_key_version, next_item_key;
    uint8_t item_keys_valid;
    wifi_service_scan_result_t pending;
    uint8_t result_count;
    uint8_t list_item_count;
    uint8_t list_active;
    uint8_t expect_connect_toast;
    uint8_t cred_save_fail_toasted;
    uint32_t seen_scan_version;
    uint32_t seen_status_version;
    uint32_t status_force_ms;
    uint32_t interest_id;
    product_ui_connection_request_kind_e interest_kind;
    uint32_t connect_status_baseline;
    uint8_t wait_link;
    uint8_t require_new_link_status;
    char connect_target[PRODUCT_UI_CONNECTION_SSID_MAX];
    product_ui_label_cache_t status_text, scan_text, empty_text;
} product_ui_wifi_page_t;

static product_ui_wifi_page_t s_wifi;
static product_ui_row_text_cache_t s_wifi_row_text[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
static wifi_service_scan_result_t s_wifi_scan_candidate[PRODUCT_UI_WIFI_SCAN_MAX];

static void product_ui_wifi_render_status(void);

/**
 * @brief 只消费匹配请求编号及种类的完成确认，再决定失败提示或等待真实链路。
 * @note 完成 SSID 保留原始身份字节；连接请求执行完成仍不等于已获 IP，成功提示继续等待匹配链路状态。
 */
static void product_ui_wifi_poll_request(void)
{
    product_ui_connection_snapshot_t snapshot;
    product_ui_connection_request_kind_e kind;
    if (s_wifi.interest_id == 0U || product_ui_connection_service_snapshot(&snapshot) != D_OK ||
        snapshot.completed_id != s_wifi.interest_id || snapshot.completed_kind != s_wifi.interest_kind) return;
    kind = s_wifi.interest_kind;
    s_wifi.interest_id = 0U;
    s_wifi.interest_kind = PRODUCT_UI_CONN_REQUEST_NONE;
    if (snapshot.completed_result != D_OK) {
        s_wifi.expect_connect_toast = 0U;
        s_wifi.wait_link = 0U;
        product_ui_toast_show(snapshot.completed_result == D_ERR_NOT_FOUND ? "无已保存网络" :
            kind == PRODUCT_UI_CONN_REQUEST_FORGET && snapshot.completed_result == D_ERR_IO ? "清除凭证失败" :
            snapshot.completed_result == D_ERR_BUSY ? "WiFi 忙，请稍后" : "操作失败，请重试", 1500U);
    } else if (kind == PRODUCT_UI_CONN_REQUEST_FORGET) {
        product_ui_toast_show("已忘记网络", 1200U);
    } else if (kind == PRODUCT_UI_CONN_REQUEST_CONNECT || kind == PRODUCT_UI_CONN_REQUEST_RECONNECT) {
        if (snapshot.completed_ssid[0] != '\0') {
            memcpy(s_wifi.connect_target, snapshot.completed_ssid, sizeof(s_wifi.connect_target));
            s_wifi.connect_target[sizeof(s_wifi.connect_target) - 1U] = '\0';
        }
        s_wifi.wait_link = 1U;
        s_wifi.expect_connect_toast = 1U;
    }
    s_wifi.seen_status_version = 0xFFFFFFFFU;
}

/**
 * @brief 受理连接域请求，成功时记录关注编号、种类和链路状态基线。
 * @param[in] command 连接域命令编号。
 * @param[in] payload 调用期间借用的载荷，提供者返回前复制。
 * @param[in] size 载荷字节数。
 * @param[in] kind 后续完成确认必须匹配的请求种类。
 * @return 失败原样返回；受理却无有效请求编号返回 IO；有效受理返回 D_OK。
 * @note 成功只表示受理；新连接要求后续状态版本不同于受理前，避免借用旧连接状态误报成功。
 */
static d_err_t product_ui_wifi_request(uint16_t command, const void *payload, uint16_t size,
    product_ui_connection_request_kind_e kind)
{
    uint32_t ticket = 0U;
    uint32_t baseline = wifi_service_get_status_version();
    d_err_t result = (d_err_t)product_ui_connection_request(command, payload, size, &ticket);
    if (result != D_OK) return result;
    if (ticket == 0U) return D_ERR_IO;
    s_wifi.interest_id = ticket;
    s_wifi.interest_kind = kind;
    s_wifi.wait_link = 0U;
    s_wifi.expect_connect_toast = 0U;
    s_wifi.connect_status_baseline = baseline;
    s_wifi.require_new_link_status = kind == PRODUCT_UI_CONN_REQUEST_CONNECT;
    s_wifi.connect_target[0] = '\0';
    if (kind == PRODUCT_UI_CONN_REQUEST_CONNECT && payload != NULL)
        (void)product_ui_utf8_copy(s_wifi.connect_target, sizeof(s_wifi.connect_target),
            ((const product_ui_connection_wifi_connect_t *)payload)->ssid);
    return D_OK;
}

/**
 * @brief 从内存缓存判断后端是否正在连接或已经关联待获 IP。
 * @return CONNECTING 或 ASSOCIATED 返回 1，其余为 0。
 */
static uint8_t product_ui_wifi_is_busy_connecting(void)
{
    wifi_service_status_t status;

    wifi_service_get_cached_status(&status);
    return ((status.state == WIFI_SERVICE_CONN_CONNECTING) ||
            (status.state == WIFI_SERVICE_CONN_ASSOCIATED)) ? 1U : 0U;
}

/**
 * @brief 将扫描阶段映射为静态中文说明。
 * @param[in] state WiFi 服务扫描阶段。
 * @return 对应阶段文字的借用指针，未列状态使用刷新提示。
 */
static const char *product_ui_wifi_scan_text(wifi_service_scan_state_t state)
{
    switch (state) {
    case WIFI_SERVICE_SCAN_SCANNING:
        return "正在扫描...";
    case WIFI_SERVICE_SCAN_BUSY:
        return "等待扫描结果...";
    case WIFI_SERVICE_SCAN_NOT_READY:
        return "WiFi 未就绪";
    case WIFI_SERVICE_SCAN_TIMEOUT:
        return "扫描超时";
    case WIFI_SERVICE_SCAN_ERROR:
        return "扫描失败";
    case WIFI_SERVICE_SCAN_DONE:
        return "扫描完成";
    default:
        return "点击右上角刷新";
    }
}

/**
 * @brief 将连接错误映射为静态中文说明。
 * @param[in] error WiFi 服务连接错误。
 * @return 对应错误文字的借用指针，未列错误使用未知错误。
 */
static const char *product_ui_wifi_error_text(wifi_service_conn_error_t error)
{
    switch (error) {
    case WIFI_SERVICE_ERR_NOT_READY:
        return "WiFi 未就绪";
    case WIFI_SERVICE_ERR_BUSY:
        return "WiFi 忙";
    case WIFI_SERVICE_ERR_NO_SAVED:
        return "无已保存网络";
    case WIFI_SERVICE_ERR_NOT_FOUND:
        return "找不到该网络";
    case WIFI_SERVICE_ERR_ASSOC_FAIL:
        return "关联失败（密码可能错误）";
    case WIFI_SERVICE_ERR_ASSOC_TIMEOUT:
        return "关联超时";
    case WIFI_SERVICE_ERR_SWITCH_DISCONNECT_TIMEOUT:
        return "切换超时，旧网络未断开";
    case WIFI_SERVICE_ERR_DHCP_TIMEOUT:
        return "获取 IP 超时";
    case WIFI_SERVICE_ERR_DISCONNECTED:
        return "已断开";
    default:
        return "未知错误";
    }
}

/* 320×240 横屏静态 QWERTY 布局，保持常见的二十六字母顺序。 */
static const char *s_wifi_kb_map_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "@", "-", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_lower[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 1, 1, 1, 1, PRODUCT_UI_WIFI_KB_CTRL(2),
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 4, PRODUCT_UI_WIFI_KB_CTRL(2)
};

static const char *s_wifi_kb_map_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "1#", "@", "-", " ", LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_upper[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 1, 1, 1, 1, PRODUCT_UI_WIFI_KB_CTRL(2),
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 4, PRODUCT_UI_WIFI_KB_CTRL(2)
};

static const char *s_wifi_kb_map_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "!", "?", "#", "$", "%", "&", "*", "/", "\\", "\n",
    "abc", ".", ",", ":", ";", "'", "\"", "(", ")", "\n",
    "ABC", "_", "+", "-", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t s_wifi_kb_ctrl_special[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 1, 1, 1, 1, 1,
    PRODUCT_UI_WIFI_KB_CTRL(2), 1, 1, 1, 3,
    PRODUCT_UI_WIFI_KB_CTRL(2), PRODUCT_UI_WIFI_KB_CTRL(2)
};

/**
 * @brief 先解除密码层引用，清零候选网络并释放下拉占用，再删除浮层。
 */
static void product_ui_wifi_pwd_close(void)
{
    lv_obj_t *layer = s_wifi.pwd_layer;
    s_wifi.pwd_layer = NULL;
    s_wifi.pwd_title = NULL;
    s_wifi.pwd_ta = NULL;
    s_wifi.pwd_eye_btn = NULL;
    s_wifi.pwd_eye_label = NULL;
    s_wifi.pwd_kb = NULL;
    memset(&s_wifi.pending, 0, sizeof(s_wifi.pending));
    product_ui_control_center_release_pull(&s_wifi.is_pull_held);
    if (layer != NULL) lv_obj_delete(layer);
}

/**
 * @brief 当外部删除当前密码根时归还占用并清空剩余状态，避免递归删除同根。
 * @param[in] event 密码浮层删除事件。
 */
static void product_ui_wifi_pwd_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_wifi.pwd_layer) return;
    s_wifi.pwd_layer = NULL;
    product_ui_wifi_pwd_close();
}

/**
 * @brief 删除本页复用列表并撤销标签引用、显示数量及文字缓存。
 */
static void product_ui_wifi_list_teardown(void)
{
    if (s_wifi.list.root != NULL) {
        lv_obj_delete(s_wifi.list.root);
    }
    s_wifi.status_label = NULL;
    s_wifi.scan_label = NULL;
    s_wifi.empty_label = NULL;
    s_wifi.list_active = 0U;
    s_wifi.list_item_count = 0U;
    memset(s_wifi_row_text, 0, sizeof(s_wifi_row_text));
    memset(&s_wifi.status_text, 0, sizeof(s_wifi.status_text));
    memset(&s_wifi.scan_text, 0, sizeof(s_wifi.scan_text));
    memset(&s_wifi.empty_text, 0, sizeof(s_wifi.empty_text));
}

/**
 * @brief 复制并清洗有限 SSID／密码到请求值，受理连接后刷新真实状态投影。
 * @param[in] ssid 本次借用的目标网络名称。
 * @param[in] pwd 本次借用的密码，开放网络可为空或空串。
 * @return 空清洗结果返回 INVALID_ARG；其他原样返回连接请求受理结果。
 * @note 只有受理成功显示正在连接，不宣称 DHCP 或凭据保存已完成。
 */
static d_err_t product_ui_wifi_start_connect(const char *ssid, const char *pwd)
{
    product_ui_connection_wifi_connect_t req;
    d_err_t err;

    memset(&req, 0, sizeof(req));
    (void)product_ui_utf8_copy(req.ssid, sizeof(req.ssid), ssid);
    if (pwd != NULL) {
        (void)product_ui_utf8_copy(req.pwd, sizeof(req.pwd), pwd);
    }
    if (req.ssid[0] == '\0') {
        product_ui_toast_show("连接失败", 1500U);
        return D_ERR_INVALID_ARG;
    }
    err = product_ui_wifi_request(PRODUCT_UI_CONNECTION_WIFI_CONNECT, &req,
        (uint16_t)sizeof(req), PRODUCT_UI_CONN_REQUEST_CONNECT);
    if (err == D_OK) {
        product_ui_toast_show("正在连接...", 1200U);
    } else if (err == D_ERR_BUSY) {
        product_ui_toast_show("WiFi 忙，请稍后", 1500U);
    } else if (err == D_ERR_NOT_READY) {
        product_ui_toast_show("WiFi 未就绪", 1500U);
    } else {
        product_ui_toast_show("连接失败", 1500U);
    }
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    product_ui_wifi_poll_request();
    product_ui_wifi_render_status();
    return err;
}

/**
 * @brief 密码非空时提交当前候选网络连接，只有受理成功才关闭密码层。
 * @param[in] event 确认事件，当前不读取载荷。
 */
static void product_ui_wifi_pwd_ok_cb(lv_event_t *event)
{
    const char *pwd;

    (void)event;
    if ((s_wifi.pwd_ta == NULL) || (s_wifi.pending.ssid[0] == '\0')) {
        product_ui_wifi_pwd_close();
        return;
    }
    pwd = lv_textarea_get_text(s_wifi.pwd_ta);
    if ((pwd == NULL) || (pwd[0] == '\0')) {
        product_ui_toast_show("请输入密码", 1200U);
        return;
    }
    if (product_ui_wifi_start_connect(s_wifi.pending.ssid, pwd) == D_OK)
        product_ui_wifi_pwd_close();
}

/**
 * @brief 取消密码输入并关闭当前层，不提交连接。
 * @param[in] event 取消事件，当前不读取载荷。
 */
static void product_ui_wifi_pwd_cancel_cb(lv_event_t *event)
{
    (void)event;
    product_ui_wifi_pwd_close();
}

/**
 * @brief 把键盘 READY 转为确认，把 CANCEL 转为关闭输入。
 * @param[in] event 密码键盘事件。
 */
static void product_ui_wifi_pwd_kb_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_READY) {
        product_ui_wifi_pwd_ok_cb(event);
        return;
    }
    if (code == LV_EVENT_CANCEL) {
        product_ui_wifi_pwd_close();
    }
}

/**
 * @brief 切换密码明文／掩码和操作图标，并恢复输入框焦点。
 * @param[in] event 密码可见性按钮事件，当前不读取载荷。
 */
static void product_ui_wifi_pwd_eye_cb(lv_event_t *event)
{
    uint8_t is_hidden;

    (void)event;
    if ((s_wifi.pwd_ta == NULL) || (s_wifi.pwd_eye_label == NULL)) {
        return;
    }
    is_hidden = lv_textarea_get_password_mode(s_wifi.pwd_ta) ? 1U : 0U;
    lv_textarea_set_password_mode(s_wifi.pwd_ta, is_hidden == 0U);
    lv_label_set_text(s_wifi.pwd_eye_label,
                      (is_hidden != 0U) ? LV_SYMBOL_EYE_CLOSE :
                                          LV_SYMBOL_EYE_OPEN);
    lv_obj_add_state(s_wifi.pwd_ta, LV_STATE_FOCUSED);
    if (s_wifi.pwd_kb != NULL) {
        lv_keyboard_set_textarea(s_wifi.pwd_kb, s_wifi.pwd_ta);
    }
}

/**
 * @brief 为横屏键盘安装静态小写、大写及符号布局。
 * @param[in,out] keyboard 借用的键盘对象，NULL 时忽略。
 * @note 布局数组静态存活；只改变输入布局，不修改密码或网络配置。
 */
static void product_ui_wifi_pwd_apply_kb_map(lv_obj_t *keyboard)
{
    if (keyboard == NULL) {
        return;
    }
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        s_wifi_kb_map_lower, s_wifi_kb_ctrl_lower);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        s_wifi_kb_map_upper, s_wifi_kb_ctrl_upper);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL,
                        s_wifi_kb_map_special, s_wifi_kb_ctrl_special);
}

/**
 * @brief 设置密码键盘的主题样式、间距及按下外观。
 * @param[in,out] keyboard 借用的键盘对象，NULL 时忽略。
 */
static void product_ui_wifi_pwd_style_keyboard(lv_obj_t *keyboard)
{
    if (keyboard == NULL) {
        return;
    }
    lv_obj_set_style_pad_row(keyboard, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE_ALT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14,
                               LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, PRODUCT_UI_RADIUS_MD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, product_ui_color(PRODUCT_UI_COLOR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, product_ui_color(PRODUCT_UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, product_ui_color(PRODUCT_UI_COLOR_ACTION_PRESSED), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_color_filter_opa(keyboard, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard, product_ui_color(PRODUCT_UI_COLOR_ACTION_TEXT), LV_PART_ITEMS | LV_STATE_PRESSED);
}

/**
 * @brief 复制候选网络并创建有界密码层、可见性开关及横屏键盘。
 * @param[in] item 本次复制的扫描结果条目。
 * @return 完整创建返回 0；输入、页面或任一对象失败返回 -1，失败归还本层占用。
 * @note 新建前关闭旧密码层；显示 SSID 使用 UTF-8 清洗副本，候选扫描结果独立保存。
 */
static int product_ui_wifi_pwd_open(const wifi_service_scan_result_t *item)
{
    lv_obj_t *label;
    lv_obj_t *cancel;
    char title[48];
    char ssid_show[PRODUCT_UI_WIFI_SSID_SHOW];

    if ((item == NULL) || (s_wifi.page == NULL) ||
        (s_wifi.page->root == NULL)) {
        return -1;
    }
    product_ui_wifi_pwd_close();
    s_wifi.pending = *item;

    s_wifi.pwd_layer = lv_obj_create(s_wifi.page->root);
    if (s_wifi.pwd_layer == NULL) {
        memset(&s_wifi.pending, 0, sizeof(s_wifi.pending));
        return -1;
    }
    product_ui_control_center_hold_pull(&s_wifi.is_pull_held);
    lv_obj_add_event_cb(s_wifi.pwd_layer, product_ui_wifi_pwd_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(s_wifi.pwd_layer);
    lv_obj_set_size(s_wifi.pwd_layer, PRODUCT_UI_SCREEN_W, PRODUCT_UI_SCREEN_H);
    lv_obj_set_pos(s_wifi.pwd_layer, 0, 0);
    lv_obj_set_style_bg_color(s_wifi.pwd_layer,
                              product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    lv_obj_set_style_bg_opa(s_wifi.pwd_layer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_wifi.pwd_layer, LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_wifi.pwd_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_wifi.pwd_layer);

    cancel = lv_button_create(s_wifi.pwd_layer);
    if (cancel == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_remove_style_all(cancel);
    lv_obj_add_event_cb(cancel, product_ui_wifi_pwd_cancel_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(cancel);
    if (label == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    product_ui_dialog_button_style(cancel, PRODUCT_UI_DIALOG_SECONDARY);
    lv_obj_set_size(cancel, PRODUCT_UI_TOUCH_MIN, PRODUCT_UI_TOUCH_MIN);
    lv_obj_set_pos(cancel, 4, 2);
    lv_obj_center(label);

    s_wifi.pwd_title = lv_label_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_title == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    (void)product_ui_utf8_copy(ssid_show, sizeof(ssid_show), item->ssid);
    lv_snprintf(title, sizeof(title), "输入密码: %s", ssid_show);
    lv_obj_add_style(s_wifi.pwd_title, product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
    lv_obj_set_pos(s_wifi.pwd_title, 44, 10);
    lv_obj_set_width(s_wifi.pwd_title, 268);
    lv_obj_set_height(s_wifi.pwd_title, product_ui_font(PRODUCT_UI_FONT_BODY)->line_height);
    lv_label_set_long_mode(s_wifi.pwd_title, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_wifi.pwd_title, title);

    s_wifi.pwd_ta = lv_textarea_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_ta == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_set_size(s_wifi.pwd_ta, PRODUCT_UI_WIFI_TA_W, PRODUCT_UI_WIFI_TA_H);
    lv_obj_set_pos(s_wifi.pwd_ta, PRODUCT_UI_WIFI_TA_X, PRODUCT_UI_WIFI_TA_Y);
    lv_textarea_set_one_line(s_wifi.pwd_ta, true);
    lv_textarea_set_password_mode(s_wifi.pwd_ta, true);
    lv_textarea_set_max_length(s_wifi.pwd_ta, PRODUCT_UI_WIFI_PWD_MAX - 1);
    lv_textarea_set_placeholder_text(s_wifi.pwd_ta, "WiFi 密码");
    lv_obj_set_style_text_font(s_wifi.pwd_ta,
                               product_ui_font(PRODUCT_UI_FONT_BODY), 0);
    lv_obj_set_style_text_font(s_wifi.pwd_ta,
                               product_ui_font(PRODUCT_UI_FONT_BODY),
                               LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(s_wifi.pwd_ta,
                              product_ui_color(PRODUCT_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_wifi.pwd_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wifi.pwd_ta, 1, 0);
    lv_obj_set_style_border_color(s_wifi.pwd_ta,
                                  product_ui_color(PRODUCT_UI_COLOR_ACCENT), 0);
    lv_obj_set_style_radius(s_wifi.pwd_ta, PRODUCT_UI_RADIUS_MD, 0);
    lv_obj_set_style_pad_hor(s_wifi.pwd_ta, 10, 0);
    lv_obj_set_style_pad_ver(s_wifi.pwd_ta, 8, 0);

    /* 密码显示／隐藏沿用 Cipher 竖屏输入的相同操作图标语义。 */
    s_wifi.pwd_eye_btn = lv_button_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_eye_btn == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_remove_style_all(s_wifi.pwd_eye_btn);
    lv_obj_add_event_cb(s_wifi.pwd_eye_btn, product_ui_wifi_pwd_eye_cb,
                        LV_EVENT_CLICKED, NULL);
    s_wifi.pwd_eye_label = lv_label_create(s_wifi.pwd_eye_btn);
    if (s_wifi.pwd_eye_label == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_label_set_text(s_wifi.pwd_eye_label, LV_SYMBOL_EYE_OPEN);
    product_ui_dialog_button_style(s_wifi.pwd_eye_btn, PRODUCT_UI_DIALOG_SECONDARY);
    lv_obj_set_size(s_wifi.pwd_eye_btn, PRODUCT_UI_WIFI_EYE_W, PRODUCT_UI_WIFI_TA_H);
    lv_obj_set_pos(s_wifi.pwd_eye_btn, PRODUCT_UI_WIFI_EYE_X, PRODUCT_UI_WIFI_TA_Y);
    lv_obj_center(s_wifi.pwd_eye_label);

    s_wifi.pwd_kb = lv_keyboard_create(s_wifi.pwd_layer);
    if (s_wifi.pwd_kb == NULL) {
        product_ui_wifi_pwd_close();
        return -1;
    }
    lv_obj_set_size(s_wifi.pwd_kb, PRODUCT_UI_SCREEN_W, PRODUCT_UI_WIFI_KB_H);
    /* 覆盖默认底部对齐，按明确的左上坐标布局键盘。 */
    lv_obj_align(s_wifi.pwd_kb, LV_ALIGN_TOP_LEFT, 0, PRODUCT_UI_WIFI_KB_Y);
    product_ui_wifi_pwd_apply_kb_map(s_wifi.pwd_kb);
    product_ui_wifi_pwd_style_keyboard(s_wifi.pwd_kb);
    lv_keyboard_set_textarea(s_wifi.pwd_kb, s_wifi.pwd_ta);
    lv_keyboard_set_mode(s_wifi.pwd_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(s_wifi.pwd_kb, product_ui_wifi_pwd_kb_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_wifi.pwd_kb, product_ui_wifi_pwd_kb_cb,
                        LV_EVENT_CANCEL, NULL);
    return 0;
}

/**
 * @brief 按固定槽清洗显示标题并生成带扫描身份键的网络行。
 * @param[in] item_index 扫描结果逻辑下标。
 * @param[in] slot_index 固定复用槽下标。
 * @param[out] row_spec 接收借用槽内文字的行规格。
 * @param[in,out] user_data 本页扫描状态，其显示缓冲会更新。
 * @return 成功返回 0，参数或下标无效返回 -1。
 * @note 网络身份不使用复用槽或旧扫描下标；虚拟列表注入并校验动作分发。
 */
static int product_ui_wifi_row_bind(uint16_t item_index, uint8_t slot_index,
                                    product_ui_row_spec_t *row_spec,
                                    void *user_data)
{
    product_ui_wifi_page_t *page = user_data;
    const wifi_service_scan_result_t *item;

    if ((page == NULL) || (row_spec == NULL) ||
        (slot_index >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) ||
        (item_index >= page->result_count)) {
        return -1;
    }
    item = &page->results[item_index];
    (void)product_ui_utf8_copy(page->slot_title[slot_index],
                               sizeof(page->slot_title[slot_index]),
                               item->ssid);
    lv_snprintf(page->slot_sub[slot_index], sizeof(page->slot_sub[slot_index]),
                "%d dBm · %u%% · %s",
                (int)item->rssi,
                (unsigned)item->signal_strength,
                wifi_service_auth_name(item->auth_mode));
    page->slot_rows[slot_index] = (product_ui_list_item_t) {
        .item_id = "wifi_ap",
        .title = page->slot_title[slot_index],
        .icon_id = PRODUCT_UI_ICON_WIFI,
        .accent = PRODUCT_UI_COLOR_SUCCESS,
    };
    *row_spec = (product_ui_row_spec_t) {
        .item_key = page->item_keys[item_index],
        .item = &page->slot_rows[slot_index],
        .subtitle = page->slot_sub[slot_index],
        .interaction = PRODUCT_UI_ROW_INTERACTION_NAVIGATE,
        /* PRODUCT_UI_VIRTUAL_ACTION_INJECTED */
        .trailing = PRODUCT_UI_ROW_TRAILING_CHEVRON,
        .is_enabled = 1U,
        .appearance = PRODUCT_UI_ROW_APPEARANCE_INSET,
    };
    return 0;
}

/**
 * @brief 开放网络直接受理连接，加密网络打开其密码层。
 * @param[in] item_index 当前扫描结果下标。
 * @param[in,out] user_data 页面状态及当前扫描结果。
 */
static void product_ui_wifi_row_action(uint16_t item_index, void *user_data)
{
    product_ui_wifi_page_t *page = user_data;
    const wifi_service_scan_result_t *item;

    if ((page == NULL) || (item_index >= page->result_count)) {
        return;
    }
    item = &page->results[item_index];
    if (wifi_service_auth_requires_password(item->auth_mode) == 0U) {
        product_ui_wifi_start_connect(item->ssid, "");
        return;
    }
    if (product_ui_wifi_pwd_open(item) != 0) {
        product_ui_toast_show("无法打开密码输入", 1500U);
    }
}

/**
 * @brief 非连接中时受理已保存网络重连，并刷新请求／链路投影。
 * @param[in] event 重连按钮事件，当前不读取载荷。
 */
static void product_ui_wifi_reconnect_cb(lv_event_t *event)
{
    d_err_t err;

    (void)event;
    if (product_ui_wifi_is_busy_connecting() != 0U) {
        product_ui_toast_show("正在连接，请稍后", 1200U);
        return;
    }
    err = product_ui_wifi_request(PRODUCT_UI_CONNECTION_WIFI_RECONNECT, NULL, 0U,
        PRODUCT_UI_CONN_REQUEST_RECONNECT);
    if (err == D_OK) {
        product_ui_toast_show("正在重连...", 1200U);
    } else if (err == D_ERR_NOT_FOUND) {
        product_ui_toast_show("无已保存网络", 1500U);
    } else if (err == D_ERR_BUSY) {
        product_ui_toast_show("WiFi 忙，请稍后", 1500U);
    } else {
        product_ui_toast_show("重连失败", 1500U);
    }
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    product_ui_wifi_poll_request();
    product_ui_wifi_render_status();
}

/**
 * @brief 关闭密码层并受理忘记网络，完成结果由匹配请求继续观察。
 * @param[in] event 忘记网络事件，当前不读取载荷。
 */
static void product_ui_wifi_forget_cb(lv_event_t *event)
{
    d_err_t err;

    (void)event;
    product_ui_wifi_pwd_close();
    err = product_ui_wifi_request(PRODUCT_UI_CONNECTION_WIFI_FORGET, NULL, 0U,
        PRODUCT_UI_CONN_REQUEST_FORGET);
    if (err == D_OK) {
        product_ui_toast_show("请稍后", 1200U);
    } else if (err == D_ERR_IO) {
        product_ui_toast_show("清除凭证失败", 1500U);
    } else {
        product_ui_toast_show("忘记网络失败", 1500U);
    }
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    product_ui_wifi_poll_request();
    product_ui_wifi_render_status();
}

/**
 * @brief 关闭密码层并受理扫描，强制下一轮重新观察扫描版本。
 * @param[in] event 刷新事件，当前不读取载荷。
 */
static void product_ui_wifi_refresh_cb(lv_event_t *event)
{
    d_err_t err;

    (void)event;
    product_ui_wifi_pwd_close();
    err = product_ui_wifi_request(PRODUCT_UI_CONNECTION_WIFI_SCAN_START, NULL, 0U,
        PRODUCT_UI_CONN_REQUEST_SCAN);
    if (err == D_OK) {
        product_ui_toast_show("开始扫描", 800U);
    } else if (err == D_ERR_BUSY) {
        product_ui_toast_show("WiFi 忙，请稍后", 1500U);
    } else {
        product_ui_toast_show("无法开始扫描", 1200U);
    }
    s_wifi.seen_scan_version = 0xFFFFFFFFU;
}

/**
 * @brief 从内存状态投影链路及凭据保存状态，文字成功后才提交已见版本。
 * @note 连接成功提示要求请求已完成、原始目标 SSID 匹配且新连接版本成立；显示清洗文字不参与身份比较。
 */
static void product_ui_wifi_render_status(void)
{
    wifi_service_status_t status;
    uint32_t version;
    char line[96];
    char ssid_show[PRODUCT_UI_WIFI_SSID_SHOW];
    uint8_t matching_connection;
    if (s_wifi.status_label == NULL) {
        return;
    }
    version = wifi_service_get_status_version();
    if (version == s_wifi.seen_status_version) {
        return;
    }
    wifi_service_get_cached_status(&status);
    matching_connection = s_wifi.wait_link && s_wifi.connect_target[0] != '\0' &&
        strcmp(status.ssid, s_wifi.connect_target) == 0 &&
        (!s_wifi.require_new_link_status || version != s_wifi.connect_status_baseline);
    (void)product_ui_utf8_copy(ssid_show, sizeof(ssid_show), status.ssid);

    if (status.state == WIFI_SERVICE_CONN_CONNECTED) {
        if (status.cred_save_failed != 0U) {
            if (matching_connection) { s_wifi.expect_connect_toast = 0U; s_wifi.wait_link = 0U; }
            lv_snprintf(line, sizeof(line), "已连接: %s（未保存）",
                        ssid_show[0] != '\0' ? ssid_show : "WiFi");
            if (s_wifi.cred_save_fail_toasted == 0U) {
                s_wifi.cred_save_fail_toasted = 1U;
                product_ui_toast_show("已上网，但未保存密码", 2000U);
            }
        } else {
            s_wifi.cred_save_fail_toasted = 0U;
            lv_snprintf(line, sizeof(line), "已连接: %s  %d%%",
                        ssid_show[0] != '\0' ? ssid_show : "WiFi",
                        (int)status.signal_strength);
            if (s_wifi.expect_connect_toast != 0U && matching_connection) {
                s_wifi.expect_connect_toast = 0U;
                s_wifi.wait_link = 0U;
                product_ui_toast_show("已连接", 1500U);
            }
        }
    } else if ((status.state == WIFI_SERVICE_CONN_CONNECTING) ||
               (status.state == WIFI_SERVICE_CONN_ASSOCIATED)) {
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "连接中: %s",
                    ssid_show[0] != '\0' ? ssid_show : "...");
    } else if ((status.state == WIFI_SERVICE_CONN_ERROR) &&
               (status.error == WIFI_SERVICE_ERR_NO_SAVED)) {
        if (status.has_saved_credentials != 0U) {
            lv_snprintf(line, sizeof(line), "已保存: %s（未连接）",
                        ssid_show[0] != '\0' ? ssid_show : "网络");
        } else {
            lv_snprintf(line, sizeof(line), "未连接（无已保存网络）");
        }
    } else if (status.state == WIFI_SERVICE_CONN_ERROR) {
        s_wifi.expect_connect_toast = 0U;
        s_wifi.wait_link = 0U;
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "错误: %s",
                    product_ui_wifi_error_text(status.error));
    } else if (status.has_saved_credentials != 0U) {
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "已保存: %s（未连接）",
                    ssid_show[0] != '\0' ? ssid_show : "网络");
    } else {
        s_wifi.cred_save_fail_toasted = 0U;
        lv_snprintf(line, sizeof(line), "未连接（可用手机 BLE 或本页配网）");
    }
    if (product_ui_label_sync(s_wifi.status_label, line, &s_wifi.status_text) >= 0)
        s_wifi.seen_status_version = version;
}

/**
 * @brief 显示空列表说明并清空可操作行及其文本缓存。
 * @param[in] message 本次借用的提示，NULL 使用默认刷新说明。
 * @return 文字与列表均更新成功返回 0，任一失败返回 -1。
 */
static int product_ui_wifi_show_list_empty(const char *message)
{
    if (s_wifi.empty_label == NULL) {
        return -1;
    }
    int text_result = product_ui_label_sync(s_wifi.empty_label,
        message != NULL ? message : "点击右上角刷新", &s_wifi.empty_text);
    lv_obj_clear_flag(s_wifi.empty_label, LV_OBJ_FLAG_HIDDEN);
    if (product_ui_virtual_list_set_count(&s_wifi.list, 0U) != 0) return -1;
    s_wifi.list_active = 0U; s_wifi.list_item_count = 0U;
    memset(s_wifi_row_text, 0, sizeof(s_wifi_row_text));
    return text_result < 0 ? -1 : 0;
}

/**
 * @brief 更新复用列表数量，切换为空列表之外的可操作显示并重置行文字缓存。
 * @param[in] count 当前扫描结果数量。
 * @return 成功返回 0，无列表或更新失败返回 -1。
 */
static int product_ui_wifi_list_rebuild(uint8_t count)
{
    if (s_wifi.list.root == NULL) {
        return -1;
    }
    lv_obj_add_flag(s_wifi.empty_label, LV_OBJ_FLAG_HIDDEN);
    if (product_ui_virtual_list_set_count(&s_wifi.list, count) != 0) return -1;
    s_wifi.list_active = 1U; s_wifi.list_item_count = count;
    memset(s_wifi_row_text, 0, sizeof(s_wifi_row_text));
    return 0;
}

/**
 * @brief 为完整扫描快照分配非零条目身份键，再复制结果数组。
 * @param[in] version 已经前后复核的扫描版本。
 * @param[in] count 调用方保证在扫描数组容量内的数量。
 * @param[in] results 本次复制的完整扫描快照。
 * @note 同版本、同数量且逐字节相同的快照保留条目键；其他变化重新分配，不能以视口槽身份代表 AP。
 */
static void product_ui_wifi_scan_keys(uint32_t version, uint8_t count,
    const wifi_service_scan_result_t *results)
{
    uint8_t same_snapshot = s_wifi.item_keys_valid && s_wifi.item_key_version == version &&
        s_wifi.result_count == count && memcmp(s_wifi.results, results, (size_t)count * sizeof(*results)) == 0;
    if (!same_snapshot) {
        for (uint8_t index = 0U; index < count; index++) {
            if (++s_wifi.next_item_key == 0U) ++s_wifi.next_item_key;
            s_wifi.item_keys[index] = s_wifi.next_item_key;
        }
    }
    memcpy(s_wifi.results, results, (size_t)count * sizeof(*results));
    s_wifi.result_count = count;
    s_wifi.item_key_version = version;
    s_wifi.item_keys_valid = 1U;
}

/**
 * @brief 前后复核扫描版本，再更新状态文字与可选择列表。
 * @note 扫描进度不隐藏最近仍可用的结果；只有所有文字及列表更新成功才提交已见扫描版本。
 */
static void product_ui_wifi_render_scan(void)
{
    wifi_service_scan_state_t state;
    uint32_t version;
    uint8_t count;
    int text_result = 0;
    char text[40];

    if (s_wifi.list.root == NULL) {
        return;
    }
    version = wifi_service_scan_get_version();
    if (version == s_wifi.seen_scan_version) {
        return;
    }
    state = wifi_service_scan_get_state();
    count = wifi_service_scan_copy_results(s_wifi_scan_candidate,
                                           PRODUCT_UI_WIFI_SCAN_MAX);
    if (count > PRODUCT_UI_WIFI_SCAN_MAX) return;
    if (version != wifi_service_scan_get_version()) return;
    product_ui_wifi_scan_keys(version, count, s_wifi_scan_candidate);

    if (s_wifi.scan_label != NULL) {
        if (state == WIFI_SERVICE_SCAN_DONE) {
            lv_snprintf(text, sizeof(text), "发现 %u 个网络", (unsigned)count);
        } else {
            lv_snprintf(text, sizeof(text), "%s", product_ui_wifi_scan_text(state));
        }
        text_result = product_ui_label_sync(s_wifi.scan_label, text, &s_wifi.scan_text);
    }

    /* 刷新进度不能隐藏最近仍可用且可选择的扫描列表。 */
    if (count == 0U) {
        if (product_ui_wifi_show_list_empty(product_ui_wifi_scan_text(state)) == 0 && text_result >= 0)
            s_wifi.seen_scan_version = version;
        return;
    }

    if ((s_wifi.list_active != 0U) &&
        (s_wifi.list_item_count == count) &&
        (s_wifi.list.root != NULL)) {
        if (product_ui_virtual_list_refresh(&s_wifi.list) == 0 && text_result >= 0) {
            s_wifi.seen_scan_version = version;
        }
        return;
    }

    if (product_ui_wifi_list_rebuild(count) == 0 && text_result >= 0) {
        s_wifi.seen_scan_version = version;
    }
}

/**
 * @brief 创建不参与父滚动链的固定重连／忘记操作条。
 * @param[in,out] content 操作条父容器。
 * @return 操作条及按钮创建成功返回 0，任一步失败返回 -1；部分对象交页面树清理。
 */
static int product_ui_wifi_build_actions(lv_obj_t *content)
{
    lv_obj_t *row;
    lv_obj_t *btn;
    const char *labels[2] = { "重连已保存", "忘记网络" };
    lv_event_cb_t cbs[2] = {
        product_ui_wifi_reconnect_cb,
        product_ui_wifi_forget_cb,
    };
    uint8_t i;

    row = lv_obj_create(content);
    if (row == NULL) {
        return -1;
    }
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD),
                    PRODUCT_UI_TOUCH_MIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    /* 固定操作条：禁止参与父级滚动链，避免被拖走后回不来。 */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (i = 0U; i < 2U; i++) {
        btn = lv_button_create(row);
        if (btn == NULL) {
            return -1;
        }
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 140, PRODUCT_UI_TOUCH_MIN);
        lv_obj_add_style(btn,
                         product_ui_style(PRODUCT_UI_STYLE_INTERACTIVE), 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, NULL);
        {
            lv_obj_t *lab = lv_label_create(btn);
            if (lab != NULL) {
                lv_label_set_text(lab, labels[i]);
                lv_obj_center(lab);
                lv_obj_add_style(lab,
                                 product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
            }
        }
        product_ui_button_style(btn, i == 1U ? PRODUCT_UI_BUTTON_DANGER : PRODUCT_UI_BUTTON_SECONDARY);
        lv_obj_set_size(btn, 140, PRODUCT_UI_TOUCH_MIN);
    }
    return 0;
}

/**
 * @brief 推进公共 UI、列表和扫描，观察匹配请求并刷新状态及结果。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 */
static void product_ui_wifi_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
    product_ui_virtual_list_update(&s_wifi.list, elapsed_ms);
    s_wifi.status_force_ms += elapsed_ms;
    if (s_wifi.status_force_ms >= PRODUCT_UI_WIFI_STATUS_FORCE_MS) {
        s_wifi.status_force_ms = 0U;
        s_wifi.seen_status_version = 0xFFFFFFFFU;
    }
    wifi_service_scan_update(elapsed_ms);
    product_ui_wifi_poll_request();
    product_ui_wifi_render_status();
    product_ui_wifi_render_scan();
}

/**
 * @brief 关闭密码层、归还下拉占用并清理列表及页面副本，不取消已受理网络请求。
 */
static void product_ui_wifi_page_destroy(void)
{
    product_ui_wifi_pwd_close();
    product_ui_wifi_list_teardown();
    product_ui_page_view_reset(&s_wifi_page_view);
    memset(&s_wifi, 0, sizeof(s_wifi));
    s_wifi.seen_scan_version = 0xFFFFFFFFU;
    s_wifi.seen_status_version = 0xFFFFFFFFU;
}

/**
 * @brief 创建 WiFi 有限列表、状态及操作条，并受理首次扫描。
 */
int product_ui_settings_wifi_page_create(lv_obj_t *parent)
{
    product_ui_page_spec_t spec = {
        .title = "WiFi",
        .back_title = "设置",
        .show_back = 1,
        .role = PRODUCT_UI_PAGE_ROLE_SETTINGS,
        .nav_action_symbol = LV_SYMBOL_REFRESH,
        .nav_action_cb = product_ui_wifi_refresh_cb,
    };
    const product_ui_virtual_list_spec_t list_spec = {
        .row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_wifi_row_bind,
        .action_cb = product_ui_wifi_row_action,
        .user_data = &s_wifi,
        .text_caches = s_wifi_row_text,
    };
    lv_obj_t *content;

    memset(&s_wifi, 0, sizeof(s_wifi));
    s_wifi.seen_scan_version = 0xFFFFFFFFU;
    s_wifi.seen_status_version = 0xFFFFFFFFU;
    s_wifi.page = &s_wifi_page_view;

    if (product_ui_page_frame_create_ex(parent, &spec,
                                        &s_wifi_page_view) != 0) {
        return -1;
    }
    content = s_wifi_page_view.content;
    if (content == NULL) {
        return -1;
    }
    if (product_ui_virtual_list_create(content, &s_wifi.list, &list_spec) != 0) {
        return -1;
    }
    memset(s_wifi_row_text, 0, sizeof(s_wifi_row_text));
    content = s_wifi.list.header;
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, PRODUCT_UI_SPACE_SM, 0);

    s_wifi.status_label = lv_label_create(content);
    if (s_wifi.status_label == NULL) {
        return -1;
    }
    lv_obj_set_width(s_wifi.status_label,
                     PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD));
    lv_label_set_long_mode(s_wifi.status_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(s_wifi.status_label,
                     product_ui_style(PRODUCT_UI_STYLE_BODY), 0);
    lv_label_set_text(s_wifi.status_label, "读取状态...");

    if (product_ui_wifi_build_actions(content) != 0) {
        return -1;
    }

    s_wifi.scan_label = lv_label_create(content);
    if (s_wifi.scan_label == NULL) {
        return -1;
    }
    lv_obj_set_width(s_wifi.scan_label,
                     PRODUCT_UI_SCREEN_W - (2 * PRODUCT_UI_PAGE_PAD));
    lv_obj_add_style(s_wifi.scan_label,
                     product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_set_style_text_color(
        s_wifi.scan_label, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(s_wifi.scan_label, "正在扫描...");

    s_wifi.empty_label = lv_label_create(content);
    if (s_wifi.empty_label == NULL) {
        return -1;
    }
    lv_obj_add_style(s_wifi.empty_label, product_ui_style(PRODUCT_UI_STYLE_CAPTION), 0);
    lv_obj_set_width(s_wifi.empty_label, LV_PCT(100));
    lv_label_set_long_mode(s_wifi.empty_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_wifi.empty_label, "正在扫描...");

    (void)product_ui_wifi_request(PRODUCT_UI_CONNECTION_WIFI_SCAN_START, NULL, 0U,
        PRODUCT_UI_CONN_REQUEST_SCAN);
    product_ui_wifi_render_status();
    product_ui_wifi_render_scan();
    return 0;
}

#else /**
 * @brief WiFi 功能关闭时创建不可用说明页。
 */

int product_ui_settings_wifi_page_create(lv_obj_t *parent)
{
    lv_obj_t *content;

    content = product_ui_settings_detail_frame_create(parent, "WiFi",
                                                       &s_wifi_page_view);
    if (content == NULL) {
        return -1;
    }
    return product_ui_settings_detail_feedback(
        content, PRODUCT_UI_FEEDBACK_ERROR, "WiFi 不可用",
        "当前固件未启用 WiFi");
}

/**
 * @brief 功能关闭时只推进公共详情页周期。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 */
static void product_ui_wifi_page_update(uint32_t elapsed_ms)
{
    product_ui_settings_detail_page_update(elapsed_ms);
}

/**
 * @brief 解除功能关闭占位页的框架状态。
 */
static void product_ui_wifi_page_destroy(void)
{
    product_ui_page_view_reset(&s_wifi_page_view);
}

#endif /**
 * @brief 创建真实能力不足或详情未实现的反馈页面。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @param[in] page_title 详情页标题。
 * @param[in] feature_title 能力缺失时的说明标题。
 * @param[in] capability 对应设置能力位。
 * @param[out] view 接收页面框架引用。
 * @return 反馈完整创建返回 0，框架或反馈创建失败返回 -1。
 */

static int product_ui_settings_connectivity_unavailable(
    lv_obj_t *parent, const char *page_title, const char *feature_title,
    uint32_t capability, product_ui_page_view_t *view)
{
    product_ui_settings_snapshot_t snapshot;
    lv_obj_t *content;

    content = product_ui_settings_detail_frame_create(parent, page_title,
                                                       view);
    if (content == NULL) {
        return -1;
    }
    if (product_ui_settings_detail_snapshot(&snapshot) != 0) {
        return product_ui_settings_detail_feedback(
            content, PRODUCT_UI_FEEDBACK_ERROR, "设置服务不可用",
            "无法读取连接能力状态");
    }
    if ((snapshot.capability_mask & capability) == 0U) {
        return product_ui_settings_detail_feedback(
            content, PRODUCT_UI_FEEDBACK_ERROR, feature_title,
            "当前固件未提供此能力");
    }
    return product_ui_settings_detail_feedback(
        content, PRODUCT_UI_FEEDBACK_ERROR, "详情不可用",
        "设置服务未提供可验证的连接详情");
}

/**
 * @brief 创建蓝牙能力占位页，不伪造尚未提供的连接详情。
 */
int product_ui_settings_bluetooth_page_create(lv_obj_t *parent)
{
    return product_ui_settings_connectivity_unavailable(
        parent, "蓝牙", "蓝牙不可用",
        PRODUCT_UI_SETTING_CAP_BLUETOOTH, &s_bluetooth_page_view);
}

/**
 * @brief 解除蓝牙占位页框架状态，实际对象由管理器删除。
 */
static void product_ui_settings_bluetooth_destroy(void)
{
    product_ui_page_view_reset(&s_bluetooth_page_view);
}

static const page_ops_t s_settings_wifi_ops = {
    .create = product_ui_settings_wifi_page_create,
    .destroy = product_ui_wifi_page_destroy,
    .update = product_ui_wifi_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_settings_wifi_desc = {
    .name = "WiFi 详情",
    .ops = &s_settings_wifi_ops,
};

/**
 * @brief 注册静态 WiFi 详情页面描述。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int settings_wifi_register_page(int page_type)
{
    s_settings_wifi_desc.page_type = page_type;
    return page_manager_register(&s_settings_wifi_desc);
}

static const page_ops_t s_settings_bluetooth_ops = {
    .create = product_ui_settings_bluetooth_page_create,
    .destroy = product_ui_settings_bluetooth_destroy,
    .update = product_ui_settings_detail_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_settings_bluetooth_desc = {
    .name = "蓝牙详情",
    .ops = &s_settings_bluetooth_ops,
};

/**
 * @brief 注册静态蓝牙详情页面描述。
 * @param[in] page_type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int settings_bluetooth_register_page(int page_type)
{
    s_settings_bluetooth_desc.page_type = page_type;
    return page_manager_register(&s_settings_bluetooth_desc);
}

D_UI_MODULE_DEFINE(product_settings_wifi, "WiFi 详情", LV_SYMBOL_SETTINGS,
                settings_wifi_register_page, NULL);
D_UI_MODULE_DEFINE(product_settings_bluetooth, "蓝牙详情", LV_SYMBOL_SETTINGS,
                settings_bluetooth_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
