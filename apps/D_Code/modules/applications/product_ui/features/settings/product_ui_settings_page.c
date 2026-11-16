/** @file product_ui_settings_page.c @brief 可滚动设置总览，有限复用行只投影实际服务能力。 */

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "modules/applications/product_ui/debug/product_ui_trace.h"
#include "modules/applications/product_ui/assets/product_ui_icons.h"
#include "modules/applications/product_ui/assets/product_ui_wallpapers.h"
#include "modules/applications/product_ui/debug/product_ui_perf.h"
#include "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"
#include "modules/applications/product_ui/debug/product_ui_visual_capture.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

typedef struct {
    product_ui_list_item_t item;
    uint8_t kind;
} product_ui_setting_info_t;

typedef struct {
    const product_ui_settings_snapshot_t *settings;
    const product_ui_status_t *status;
    char *subtitle;
    uint8_t subtitle_size;
} product_ui_setting_state_context_t;

typedef struct {
    product_ui_page_view_t page;
    product_ui_virtual_list_t list;
    product_ui_settings_snapshot_t settings;
    product_ui_status_t status;
    uint32_t service_revision;
    uint32_t refresh_elapsed_ms;
    char subtitles[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT][32];
} product_ui_settings_page_state_t;

enum {
    PRODUCT_UI_SETTING_WIFI,
    PRODUCT_UI_SETTING_BLUETOOTH,
    PRODUCT_UI_SETTING_DISPLAY,
    PRODUCT_UI_SETTING_SOUND,
    PRODUCT_UI_SETTING_WALLPAPER,
    PRODUCT_UI_SETTING_TIME,
    PRODUCT_UI_SETTING_OTA,
    PRODUCT_UI_SETTING_ACTIVATION,
};

static const product_ui_setting_info_t s_settings_rows[] = {
#if D_QWEN_ACTIVATION_ENABLE
    {{"product_settings_activation", "AI激活", PRODUCT_UI_ICON_SETTINGS,
      PRODUCT_UI_COLOR_ACCENT}, PRODUCT_UI_SETTING_ACTIVATION},
#endif
    {{"product_settings_wifi", "WiFi", PRODUCT_UI_ICON_WIFI,
      PRODUCT_UI_COLOR_SUCCESS}, PRODUCT_UI_SETTING_WIFI},
    {{"product_settings_bluetooth", "蓝牙", PRODUCT_UI_ICON_BLUETOOTH,
      PRODUCT_UI_COLOR_ACCENT}, PRODUCT_UI_SETTING_BLUETOOTH},
    {{"product_settings_display", "显示与亮度", PRODUCT_UI_ICON_BRIGHTNESS,
      PRODUCT_UI_COLOR_WARNING}, PRODUCT_UI_SETTING_DISPLAY},
    {{"product_settings_sound", "声音", PRODUCT_UI_ICON_VOLUME,
      PRODUCT_UI_COLOR_ACCENT}, PRODUCT_UI_SETTING_SOUND},
    {{"product_settings_wallpaper", "壁纸", PRODUCT_UI_ICON_GALLERY,
      PRODUCT_UI_COLOR_SUCCESS}, PRODUCT_UI_SETTING_WALLPAPER},
    {{"product_settings_time", "时间设置", PRODUCT_UI_ICON_CALENDAR,
      PRODUCT_UI_COLOR_WARNING}, PRODUCT_UI_SETTING_TIME},
#if D_FEATURE_OTA
    {{"product_settings_ota", "系统升级", PRODUCT_UI_ICON_SETTINGS,
      PRODUCT_UI_COLOR_ACCENT}, PRODUCT_UI_SETTING_OTA},
#endif
};
static product_ui_settings_page_state_t s_settings_page;
static product_ui_row_text_cache_t s_settings_row_text[PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT];
static uint16_t s_settings_first_visible;

/**
 * @brief 按静态行表把逻辑条目交给统一导航。
 * @param[in] item_index 静态设置表下标，越界时忽略。
 * @param[in] user_data 列表动作上下文，当前未使用。
 * @note 导航失败时显示提示，不修改服务设置。
 */
static void product_ui_settings_row_action(uint16_t item_index,
                                           void *user_data)
{
    const product_ui_setting_info_t *row_info;

    (void)user_data;
    if (item_index >= (uint16_t)(sizeof(s_settings_rows) /
                                 sizeof(s_settings_rows[0]))) {
        return;
    }
    row_info = &s_settings_rows[item_index];
    PRODUCT_UI_TRACE("settings row=%s\n", row_info->item.item_id);
    if (product_ui_open_module(row_info->item.item_id) != 0) {
        product_ui_toast_show("页面打开失败", 1200U);
    }
}

/**
 * @brief 把链路状态及有效性映射为静态说明。
 * @param[in] state 状态栏链路枚举。
 * @param[in] is_valid 该链路值是否有效。
 * @return 静态连接状态文字的借用指针，无有效位时为未知。
 */
static const char *product_ui_link_subtitle(
    product_ui_link_state_e state, uint8_t is_valid)
{
    if (!is_valid) {
        return "状态未知";
    }
    if (state == PRODUCT_UI_LINK_CONNECTED) {
        return "已连接";
    }
    if (state == PRODUCT_UI_LINK_ON) {
        return "已开启";
    }
    return state == PRODUCT_UI_LINK_OFF ? "已关闭" : "状态未知";
}

/**
 * @brief 同时检查设置服务 READY、命令绑定及指定能力／有效位。
 * @param[in] settings 调用链保证非空的设置快照。
 * @param[in] capability 待检查的设置能力位。
 * @return 全部成立返回非零，否则为零。
 */
static uint8_t product_ui_settings_capability_ready(
    const product_ui_settings_snapshot_t *settings, uint32_t capability)
{
    return product_ui_service_is_ready(PRODUCT_UI_SERVICE_SETTINGS) &&
           product_ui_service_command_available(
               PRODUCT_UI_SERVICE_SETTINGS) &&
           ((settings->capability_mask & capability) != 0U) &&
           ((settings->valid_mask & capability) != 0U);
}

/**
 * @brief 优先将亮度、其次背光开关投影到显示设置副标题。
 * @param[in,out] context 借用快照及可写副标题缓冲的描述。
 * @return 写入可用状态返回 1，没有可用能力返回 0。
 */
static uint8_t product_ui_settings_display_state(
    const product_ui_setting_state_context_t *context)
{
    uint32_t capability;

    capability = PRODUCT_UI_SETTING_CAP_BRIGHTNESS;
    if (product_ui_settings_capability_ready(context->settings, capability)) {
        lv_snprintf(context->subtitle, context->subtitle_size, "%u%%",
                    context->settings->brightness_percent);
        return 1U;
    }
    capability = PRODUCT_UI_SETTING_CAP_BACKLIGHT_SWITCH;
    if (product_ui_settings_capability_ready(context->settings, capability)) {
        lv_snprintf(context->subtitle, context->subtitle_size, "背光%s",
                    context->settings->backlight_enabled ?
                    "已开启" : "已关闭");
        return 1U;
    }
    return 0U;
}

/**
 * @brief 按设置种类及实际能力生成副标题。
 * @param[in] kind 静态设置行种类。
 * @param[in,out] context 借用的快照及接收副标题的缓冲描述。
 * @return 有可展示值返回 1，否则返回 0。
 */
static uint8_t product_ui_settings_value_state(
    uint8_t kind, const product_ui_setting_state_context_t *context)
{
    uint32_t capability;
    const product_ui_wallpaper_t *wallpaper;

    switch (kind) {
#if D_QWEN_ACTIVATION_ENABLE
    case PRODUCT_UI_SETTING_ACTIVATION:
        lv_snprintf(context->subtitle, context->subtitle_size, "激活码与连接状态");
        return 1U;
#endif
#if D_FEATURE_OTA
    case PRODUCT_UI_SETTING_OTA:
        lv_snprintf(context->subtitle, context->subtitle_size, "检查并确认升级");
        return 1U;
#endif
    case PRODUCT_UI_SETTING_DISPLAY:
        return product_ui_settings_display_state(context);
    case PRODUCT_UI_SETTING_SOUND:
        capability = PRODUCT_UI_SETTING_CAP_VOLUME;
        if (product_ui_settings_capability_ready(context->settings,
                                                 capability)) {
            lv_snprintf(context->subtitle, context->subtitle_size, "%u%%",
                        context->settings->volume_percent);
            return 1U;
        }
        break;
    case PRODUCT_UI_SETTING_WALLPAPER:
        capability = PRODUCT_UI_SETTING_CAP_WALLPAPER;
        if (product_ui_settings_capability_ready(context->settings,
                                                 capability)) {
            wallpaper = product_ui_wallpaper_get(context->settings->wallpaper_id);
            if (wallpaper == NULL) return 0U;
            lv_snprintf(context->subtitle, context->subtitle_size, "%s", wallpaper->name);
            return 1U;
        }
        break;
    case PRODUCT_UI_SETTING_TIME:
        if (product_ui_settings_capability_ready(
                context->settings, PRODUCT_UI_SETTING_CAP_TIME) &&
            product_ui_module_available("product_settings_time")) {
            lv_snprintf(context->subtitle, context->subtitle_size,
                        "日期与时间");
            return 1U;
        }
        break;
    default:
        return 0U;
    }
    return 0U;
}

/**
 * @brief 综合连接域、设置域及状态栏能力，填写副标题并决定行可操作性。
 * @param[in] row 静态行信息。
 * @param[in,out] context 值快照及可写副标题缓冲描述。
 * @return 允许进入返回非零，否则为零且副标题写为当前不可用。
 */
static uint8_t product_ui_settings_row_state(
    const product_ui_setting_info_t *row,
    const product_ui_setting_state_context_t *context)
{
    const product_ui_status_t *status = context->status;
    uint8_t enabled = 0U;

    if (row->kind == PRODUCT_UI_SETTING_WIFI) {
        enabled = product_ui_settings_capability_ready(
                      context->settings, PRODUCT_UI_SETTING_CAP_WIFI) &&
                  product_ui_service_is_ready(
                      PRODUCT_UI_SERVICE_CONNECTION) &&
                  product_ui_service_command_available(
                      PRODUCT_UI_SERVICE_CONNECTION) &&
                  ((status->capability_mask &
                    PRODUCT_UI_STATUS_CAP_WIFI) != 0U);
        if (enabled) {
            lv_snprintf(context->subtitle, context->subtitle_size, "%s",
                        product_ui_link_subtitle(status->wifi_state,
                            status->valid_mask & PRODUCT_UI_STATUS_VALID_WIFI));
        }
    } else if (row->kind == PRODUCT_UI_SETTING_BLUETOOTH) {
        enabled = product_ui_settings_capability_ready(
                      context->settings,
                      PRODUCT_UI_SETTING_CAP_BLUETOOTH) &&
                  product_ui_service_is_ready(
                      PRODUCT_UI_SERVICE_CONNECTION) &&
                  product_ui_service_command_available(
                      PRODUCT_UI_SERVICE_CONNECTION) &&
                  ((status->capability_mask &
                    PRODUCT_UI_STATUS_CAP_BLUETOOTH) != 0U);
        if (enabled) {
            lv_snprintf(context->subtitle, context->subtitle_size, "%s",
                        product_ui_link_subtitle(status->bluetooth_state,
                            status->valid_mask &
                            PRODUCT_UI_STATUS_VALID_BLUETOOTH));
        }
    } else {
        enabled = product_ui_settings_value_state(row->kind, context);
    }
    if (!enabled) {
        lv_snprintf(context->subtitle, context->subtitle_size, "当前不可用");
    }
    return enabled;
}

/**
 * @brief 为固定复用槽生成带稳定种类键的设置导航或不可用行。
 * @param[in] item_index 静态行表逻辑下标。
 * @param[in] slot_index 有限复用槽下标。
 * @param[out] row_spec 接收借用静态行及槽内副标题的行规格。
 * @param[in,out] user_data 页面状态，槽副标题会被更新。
 * @return 绑定成功返回 0；上下文或下标无效返回 -1。
 * @note 虚拟列表注入动作并拥有其手势校验；未就绪行不提供导航交互。
 */
static int product_ui_settings_row_bind(uint16_t item_index,
                                        uint8_t slot_index,
                                        product_ui_row_spec_t *row_spec,
                                        void *user_data)
{
    product_ui_settings_page_state_t *page = user_data;
    product_ui_setting_state_context_t context;
    uint8_t enabled;

    if ((page == NULL) || (slot_index >= PRODUCT_UI_VIRTUAL_LIST_SLOT_COUNT) ||
        (item_index >= (uint16_t)(sizeof(s_settings_rows) /
                                  sizeof(s_settings_rows[0])))) {
        return -1;
    }
    context = (product_ui_setting_state_context_t) {
        .settings = &page->settings,
        .status = &page->status,
        .subtitle = page->subtitles[slot_index],
        .subtitle_size = sizeof(page->subtitles[slot_index]),
    };
    enabled = product_ui_settings_row_state(&s_settings_rows[item_index],
                                            &context);
    *row_spec = (product_ui_row_spec_t) {
        .item_key = (uint32_t)s_settings_rows[item_index].kind + 1U,
        .item = &s_settings_rows[item_index].item,
        .subtitle = page->subtitles[slot_index],
        .status_text = enabled ? NULL : "不可用",
        /* PRODUCT_UI_VIRTUAL_ACTION_INJECTED */
        .interaction = enabled ? PRODUCT_UI_ROW_INTERACTION_NAVIGATE :
                                 PRODUCT_UI_ROW_INTERACTION_NONE,
        .trailing = enabled ? PRODUCT_UI_ROW_TRAILING_CHEVRON :
                              PRODUCT_UI_ROW_TRAILING_STATUS,
        .is_enabled = enabled,
        .appearance = PRODUCT_UI_ROW_APPEARANCE_INSET,
    };
    return 0;
}

/**
 * @brief 读取设置与状态投影，创建有限列表并恢复保存的滚动目标。
 * @param[in,out] content 列表父容器。
 * @return 完整创建及刷新返回 0，快照或列表失败返回 -1。
 */
static int product_ui_settings_add_rows(lv_obj_t *content)
{
    product_ui_service_snapshot_t service = {0};
    uint16_t row_count = (uint16_t)(sizeof(s_settings_rows) /
                                    sizeof(s_settings_rows[0]));
    const product_ui_virtual_list_spec_t spec = {
        .item_count = row_count,
        .row_height = PRODUCT_UI_ROW_DOUBLE_H,
        .bind_cb = product_ui_settings_row_bind,
        .action_cb = product_ui_settings_row_action,
        .user_data = &s_settings_page,
        .text_caches = s_settings_row_text,
    };

    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_SETTINGS, &service);
    s_settings_page.service_revision = service.revision;
    if (service.state == PRODUCT_UI_SERVICE_READY) {
        if (product_ui_settings_service_snapshot(
                &s_settings_page.settings) != 0) {
            return -1;
        }
    } else {
        memset(&s_settings_page.settings, 0,
               sizeof(s_settings_page.settings));
    }
    product_ui_status_snapshot(&s_settings_page.status);
    if ((product_ui_virtual_list_create(content, &s_settings_page.list, &spec) != 0) ||
        (product_ui_virtual_list_refresh(&s_settings_page.list) != 0)) {
        return -1;
    }
    product_ui_virtual_list_scroll_to(&s_settings_page.list,
                                      s_settings_first_visible);
    return 0;
}

/**
 * @brief 创建设置总览框架、有限列表，并登记现有性能／截图观察点。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 成功返回 0，框架或列表失败返回 -1，部分对象交页面管理器清理。
 */
static int product_ui_settings_create(lv_obj_t *parent)
{
    const product_ui_page_spec_t spec = {
        .title = "设置总览",
        .back_title = "桌面",
        .show_back = 1,
        .role = PRODUCT_UI_PAGE_ROLE_SETTINGS,
    };
    lv_obj_t *content;
    uint32_t started_at;

    started_at = lv_tick_get();
    memset(&s_settings_page, 0, sizeof(s_settings_page));
    memset(s_settings_row_text, 0, sizeof(s_settings_row_text));
    if (product_ui_page_frame_create_ex(parent, &spec,
                                        &s_settings_page.page) != 0) {
        return -1;
    }
    content = s_settings_page.page.content;
    lv_obj_set_style_pad_ver(content, PRODUCT_UI_SPACE_SM, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, PRODUCT_UI_SPACE_XS, 0);

    if (product_ui_settings_add_rows(content) != 0) {
        return -1;
    }
    product_ui_visual_capture_request("ST-00");
    product_ui_perf_page_created("Settings", started_at);
    return 0;
}

/**
 * @brief 推进公共 UI 和列表，按节奏读取状态，变化后重绑行。
 * @param[in] elapsed_ms 本轮页面毫秒数。
 * @note 只有列表刷新成功才提交已见服务代次，失败保留可重试条件。
 */
static void product_ui_settings_update(uint32_t elapsed_ms)
{
    product_ui_settings_snapshot_t next = {0};
    product_ui_service_snapshot_t service = {0};
    product_ui_status_t status = {0};

    product_ui_process(elapsed_ms);
    product_ui_virtual_list_update(&s_settings_page.list, elapsed_ms);
    if (elapsed_ms < 200U && s_settings_page.refresh_elapsed_ms < 200U - elapsed_ms) {
        s_settings_page.refresh_elapsed_ms += elapsed_ms;
        return;
    }
    s_settings_page.refresh_elapsed_ms = 0U;
    (void)product_ui_service_snapshot(PRODUCT_UI_SERVICE_SETTINGS, &service);
    (void)product_ui_settings_service_snapshot(&next);
    product_ui_status_snapshot(&status);
    if (next.revision != s_settings_page.settings.revision ||
        service.revision != s_settings_page.service_revision ||
        status.valid_mask != s_settings_page.status.valid_mask ||
        status.hour != s_settings_page.status.hour || status.minute != s_settings_page.status.minute ||
        status.wifi_state != s_settings_page.status.wifi_state ||
        status.bluetooth_state != s_settings_page.status.bluetooth_state) {
        s_settings_page.settings = next;
        s_settings_page.status = status;
        if (product_ui_virtual_list_refresh(&s_settings_page.list) == 0) {
            s_settings_page.service_revision = service.revision;
        }
    }
}

/**
 * @brief 保存首个可见行位置，再解除框架、页面副本及文本缓存。
 */
static void product_ui_settings_destroy(void)
{
    if (s_settings_page.list.root != NULL) {
        s_settings_first_visible = product_ui_virtual_list_first_visible(
            &s_settings_page.list);
    }
    product_ui_page_view_reset(&s_settings_page.page);
    memset(&s_settings_page, 0, sizeof(s_settings_page));
    memset(s_settings_row_text, 0, sizeof(s_settings_row_text));
}

static const page_ops_t s_product_ui_settings_ops = {
    .create = product_ui_settings_create,
    .destroy = product_ui_settings_destroy,
    .update = product_ui_settings_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};

static page_desc_t s_product_ui_settings_desc = {
    .name = "ProductSettings",
    .ops = &s_product_ui_settings_ops,
};

/**
 * @brief 注册静态设置总览页面描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int product_ui_settings_register_page(int page_type)
{
    s_product_ui_settings_desc.page_type = page_type;
    return page_manager_register(&s_product_ui_settings_desc);
}

D_UI_MODULE_DEFINE(product_settings, "设置", LV_SYMBOL_SETTINGS,
                product_ui_settings_register_page, NULL);

#endif /* D_LVGL_ENABLE && (D_UI_TARGET == D_UI_PRODUCT) */
