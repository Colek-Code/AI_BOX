/** @file product_ui_ai_chat_page.c @brief Product AI 对话页面的导航、状态和可重试字幕刷新。 */
#include "config/app_config.h"
#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)
#include "product_ui_ai_chat_controller.h"
#include "product_ui_ai_chat_view.h"
#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "framework/d_ui_module.h"
#include "framework/d_page_manager.h"
#include <string.h>

#define PRODUCT_AI_REFRESH_MS 100U
#define PRODUCT_AI_CLICK_GUARD_MS 200U

typedef struct {
    product_ui_page_view_t page;
    product_ui_ai_chat_view_t subtitles;
    product_ui_ai_chat_controls_t controls;
    product_ui_ai_chat_render_state_t rendering;
    qwen_subtitle_render_t text;
    lv_obj_t *status;
    lv_obj_t *caption;
    product_ui_label_cache_t status_text;
    product_ui_label_cache_t caption_text;
    product_ui_label_cache_t action_text;
    uint32_t refresh_elapsed;
    uint32_t last_click;
    uint8_t has_click;
} product_ui_ai_chat_page_state_t;

static product_ui_ai_chat_page_state_t s_ai_page;
static void product_ai_refresh(uint8_t force);

/**
 * @brief 按当前允许操作及点击间隔受理开始或停止，再刷新投影。
 * @param[in] event 借用的点击事件，当前不读取载荷。
 */
static void product_ai_click(lv_event_t *event)
{
    uint32_t now = lv_tick_get();
    (void)event;
    if (!s_ai_page.controls.enabled) return;
    if (s_ai_page.has_click && (uint32_t)(now - s_ai_page.last_click) < PRODUCT_AI_CLICK_GUARD_MS)
        return;
    s_ai_page.has_click = 1U;
    s_ai_page.last_click = now;
    (void)product_ui_ai_chat_activate(s_ai_page.controls.action);
    product_ai_refresh(0U);
}

/**
 * @brief 创建状态与字幕说明标签并配置有界显示区域。
 * @return 成功返回 0；任一标签创建失败返回 -1，已有对象交页面生命周期清理。
 */
static int product_ai_controls_create(void)
{
    s_ai_page.status = lv_label_create(s_ai_page.page.content);
    s_ai_page.caption = lv_label_create(s_ai_page.page.content);
    if (s_ai_page.status == NULL || s_ai_page.caption == NULL) {
        return -1;
    }
    lv_obj_set_style_text_font(s_ai_page.status, product_ui_font(PRODUCT_UI_FONT_STATUS), 0);
    lv_obj_set_style_text_color(s_ai_page.status, product_ui_color(PRODUCT_UI_COLOR_TEXT), 0);
    lv_obj_set_pos(s_ai_page.status, 0, 0);
    lv_obj_set_width(s_ai_page.status, 176);
    lv_obj_set_height(s_ai_page.status, product_ui_font(PRODUCT_UI_FONT_STATUS)->line_height);
    lv_label_set_long_mode(s_ai_page.status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_ai_page.caption, product_ui_font(PRODUCT_UI_FONT_CAPTION), 0);
    lv_obj_set_style_text_color(s_ai_page.caption, product_ui_color(PRODUCT_UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(s_ai_page.caption, 112);
    lv_obj_set_height(s_ai_page.caption, product_ui_font(PRODUCT_UI_FONT_CAPTION)->line_height);
    lv_obj_set_style_text_align(s_ai_page.caption, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_ai_page.caption, LV_LABEL_LONG_DOT);
    lv_obj_align(s_ai_page.caption, LV_ALIGN_TOP_RIGHT, 0, 2);
    return 0;
}

/**
 * @brief 根据字幕初始化、代次、裁剪与会话状态选择说明文字。
 * @return 指向静态说明文字的借用指针。
 */
static const char *product_ai_caption(void)
{
    if (s_ai_page.text.init_result != D_OK) return "字幕暂不可用";
    if (!s_ai_page.text.revision) return "等待回复";
    if (s_ai_page.text.is_clipped) return "已达显示上限";
    if (s_ai_page.text.state == QWEN_SUBTITLE_STATE_FINAL) return "完整回复";
    return s_ai_page.controls.service.session.phase == QWEN_SESSION_ACTIVE ?
           "实时回复" : "未完成回复";
}

/**
 * @brief 读取服务投影，更新标签缓存及操作使能并保留未变操作的手势资格。
 * @note 文本更新失败可重试；逻辑控制状态仍跟随最新服务，不把文本成功作为业务门禁。
 */
static void product_ai_refresh_controls(void)
{
    product_ui_ai_chat_controls_t next;
    const char *caption;
    product_ui_ai_chat_controls_read(&next);
    (void)product_ui_label_sync(s_ai_page.status, next.status, &s_ai_page.status_text);
    if (s_ai_page.controls.button != next.button ||
        s_ai_page.controls.enabled != next.enabled) {
        /* 页面框架同时拥有操作状态与按压资格；其显示操作变化时使标签缓存失效。 */
        memset(&s_ai_page.action_text, 0, sizeof(s_ai_page.action_text));
        product_ui_page_action_update(&s_ai_page.page, next.button, next.enabled, 0U);
    }
    /* 文本失败可重试，不重复重置未变操作的按压资格；逻辑控制始终跟随当前服务。 */
    (void)product_ui_label_sync(s_ai_page.page.nav_action_label, next.button, &s_ai_page.action_text);
    s_ai_page.controls = next;
    caption = product_ai_caption();
    (void)product_ui_label_sync(s_ai_page.caption, caption, &s_ai_page.caption_text);
}

/**
 * @brief 准备字幕及控制投影，并重试尚未完整呈现的句子内容。
 * @param[in] force 非零强制重新准备字幕。
 * @note 未初始化或无回复时显示静态占位句；仅成功呈现后清除 pending。
 */
static void product_ai_refresh(uint8_t force)
{
    static const char waiting[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX] = {
        "点击开始对话，或使用小乐唤醒。"
    };
    static const char unavailable[][QWEN_SUBTITLE_SENTENCE_TEXT_MAX] = {
        "字幕服务暂不可用，返回后可重试。"
    };
    qwen_subtitle_render_t display;
    int result;
    if (s_ai_page.status == NULL || s_ai_page.subtitles.panel == NULL) return;
    if (qwen_subtitle_prepare(force, s_ai_page.subtitles.font, &s_ai_page.text))
        product_ui_ai_chat_render_begin(&s_ai_page.rendering, s_ai_page.text.revision);
    product_ai_refresh_controls();
    if (!s_ai_page.rendering.pending) return;
    display = s_ai_page.text;
    if (display.init_result != D_OK || !display.revision) {
        display.sentences = display.init_result == D_OK ? waiting : unavailable;
        display.sentence_count = 1U;
    }
    result = product_ui_ai_chat_view_render(&s_ai_page.subtitles, &display);
    product_ui_ai_chat_render_finish(&s_ai_page.rendering, result == 0);
}

/**
 * @brief 创建对话页面框架、控制标签及字幕面板。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 成功返回 0；父对象为空或任一创建失败返回 -1，部分对象由页面管理器清理。
 */
static int product_ai_page_create(lv_obj_t *parent)
{
    static const product_ui_page_spec_t spec = {
        .title = "AI对话", .back_title = "桌面", .show_back = 1U,
        .role = PRODUCT_UI_PAGE_ROLE_CONTENT,
        .nav_action_text = "开始对话", .nav_action_cb = product_ai_click,
        .nav_action_disabled = 1U,
    };
    if (parent == NULL) return -1;
    memset(&s_ai_page, 0, sizeof(s_ai_page));
    if (product_ui_page_frame_create_ex(parent, &spec, &s_ai_page.page) != 0)
        return -1;
    lv_obj_remove_flag(s_ai_page.page.content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (product_ai_controls_create() != 0 ||
        product_ui_ai_chat_view_create(&s_ai_page.subtitles, s_ai_page.page.content) != 0)
        return -1;
    return 0;
}

/**
 * @brief 初始化字幕服务、恢复自动跟随并强制首次刷新。
 */
static void product_ai_page_enter(void)
{
    (void)qwen_subtitle_init();
    s_ai_page.subtitles.auto_follow = 1U;
    product_ai_refresh(1U);
}

/**
 * @brief 解除页面框架状态并清空静态页面上下文。
 * @note 页面管理器删除 LVGL 对象；本页不拥有 Qwen 会话资源。
 */
static void product_ai_page_destroy(void)
{
    product_ui_page_view_reset(&s_ai_page.page);
    memset(&s_ai_page, 0, sizeof(s_ai_page));
}

/**
 * @brief 推进公共 UI，并以限定刷新周期更新对话投影及字幕。
 * @param[in] elapsed_ms 本轮经过的毫秒数。
 */
static void product_ai_page_update(uint32_t elapsed_ms)
{
    product_ui_process(elapsed_ms);
    s_ai_page.refresh_elapsed += elapsed_ms;
    if (s_ai_page.refresh_elapsed < PRODUCT_AI_REFRESH_MS) return;
    s_ai_page.refresh_elapsed %= PRODUCT_AI_REFRESH_MS;
    product_ai_refresh(0U);
}

static const page_ops_t s_ai_page_ops = {
    .create = product_ai_page_create, .enter = product_ai_page_enter,
    .destroy = product_ai_page_destroy, .update = product_ai_page_update,
    .flags = PAGE_FLAG_NO_GESTURE,
};
static page_desc_t s_ai_page_desc = { .name = "ProductAIChat", .ops = &s_ai_page_ops };
/**
 * @brief 注册静态 AI 对话页面描述。
 * @param[in] page_type 模块装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int product_ai_register(int page_type)
{
    s_ai_page_desc.page_type = page_type;
    return page_manager_register(&s_ai_page_desc);
}
D_UI_MODULE_DEFINE(product_ai_chat, "AI对话", "AI", product_ai_register, NULL);
#endif
