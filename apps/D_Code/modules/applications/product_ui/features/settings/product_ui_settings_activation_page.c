/** @file product_ui_settings_activation_page.c @brief 非阻塞 AI 激活输入与服务状态投影，页面只保留输入草稿。 */
#include "config/app_config.h"
#if D_QWEN_ACTIVATION_ENABLE && defined(D_LVGL_ENABLE) && D_UI_TARGET == D_UI_PRODUCT
#include "product_ui_settings_details_common.h"
#include "modules/integrations/qwen/qwen_activation.h"
#include "modules/integrations/qwen/qwen_service.h"
#include "modules/applications/product_ui/features/time/product_ui_time_controls.h"
#include "modules/applications/product_ui/product_ui_runtime.h"
#include "framework/d_page_manager.h"
#include "framework/d_ui_module.h"
#include "framework/d_ui_font.h"
#include <string.h>

enum { ACT_UI_NONE = 0, ACT_UI_EDIT, ACT_UI_RETRY, ACT_UI_CANCEL, ACT_UI_KEY = 100 };
static const char *s_key_map[] = {
    "0","1","2","3","4","5","6","7","\n",
    "8","9","A","B","C","D","E","F","\n",
    "G","H","J","K","M","N","P","Q","\n",
    "R","S","T","V","W","X","Y","Z","\n",
    LV_SYMBOL_BACKSPACE,"清空","确认",""
};
static const char s_characters[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static struct {
    product_ui_page_view_t view;
    lv_obj_t *status, *detail, *edit, *retry;
    lv_obj_t *layer, *entry, *keys, *pressed;
    qwen_activation_snapshot_t snapshot;
    char draft[QWEN_ACTIVATION_CODE_SIZE];
    uint32_t generation, pressed_generation, pending_generation;
    uint16_t pending, pressed_action, pressed_index;
    uint8_t armed, holds_pull;
} s_activation_page;

/**
 * @brief 把激活服务错误映射为静态用户说明。
 * @param[in] error 激活服务错误枚举。
 * @return 对应中文说明的借用指针，未知值返回无效响应说明。
 */
static const char *activation_error_text(qwen_activation_error_e error)
{
    switch (error) {
    case QWEN_ACT_ERROR_CODE: return "激活码应为16位大写字母或数字";
    case QWEN_ACT_ERROR_NETWORK: return "网络请求失败，请稍后重试";
    case QWEN_ACT_ERROR_HTTP: return "服务器未接受请求，请稍后重试";
    case QWEN_ACT_ERROR_CONFIG: return "产品配置不匹配，请联系管理员";
    case QWEN_ACT_ERROR_STORAGE: return "保存失败，请重试";
    case QWEN_ACT_ERROR_VERSION: return "记录版本不支持，请检查固件版本";
    case QWEN_ACT_ERROR_DENIED: return "激活码或凭证不可用，请联系管理员";
    case QWEN_ACT_ERROR_IDENTITY: return "设备标识不可用，请检查出厂配置";
    case QWEN_ACT_ERROR_SOURCE_EMPTY: return "未写入激活码，请通过烧录工具写入";
    case QWEN_ACT_ERROR_SOURCE_READ: return "激活码读取失败，请重试";
    case QWEN_ACT_ERROR_SOURCE_CRC: return "激活码校验失败，请重新烧录";
    case QWEN_ACT_ERROR_STORAGE_READ: return "激活记录读取失败，请重试";
    default: return "响应数据无效，请稍后重试";
    }
}

/**
 * @brief 校验按压对象、按键索引及页面代次后排入一个动作。
 * @param[in] event 按钮、键盘或滚动事件，用户数据编码页面动作。
 * @note 不在事件回调执行网络或凭据保存；滚动、重置、丢失及失焦可撤销资格。
 */
static void activation_input(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    uint16_t action = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
    uint32_t index = object == s_activation_page.keys ? lv_buttonmatrix_get_selected_button(object) : 0U;
    if (code == LV_EVENT_PRESSED) {
        s_activation_page.armed = !lv_obj_has_state(object, LV_STATE_DISABLED);
        s_activation_page.pressed = object;
        s_activation_page.pressed_action = action;
        s_activation_page.pressed_index = (uint16_t)index;
        s_activation_page.pressed_generation = s_activation_page.generation;
    } else if (code == LV_EVENT_SCROLL_BEGIN || code == LV_EVENT_INDEV_RESET ||
               code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DEFOCUSED) {
        if (object == s_activation_page.pressed || code == LV_EVENT_SCROLL_BEGIN) s_activation_page.armed = 0U;
    } else if (code == LV_EVENT_CLICKED) {
        if (s_activation_page.armed && object == s_activation_page.pressed &&
            s_activation_page.pressed_generation == s_activation_page.generation &&
            !s_activation_page.pending && (action != ACT_UI_KEY || index == s_activation_page.pressed_index)) {
            s_activation_page.pending = action == ACT_UI_KEY ? (uint16_t)(ACT_UI_KEY + index) : action;
            s_activation_page.pending_generation = s_activation_page.generation;
        }
        s_activation_page.armed = 0U;
    }
}

/**
 * @brief 撤销编辑手势、删除编辑浮层、清零激活草稿并释放下拉占用。
 */
static void activation_close_editor(void)
{
    s_activation_page.armed = 0U;
    s_activation_page.generation++;
    if (s_activation_page.layer) lv_obj_delete(s_activation_page.layer);
    s_activation_page.layer = s_activation_page.entry = s_activation_page.keys = NULL;
    memset(s_activation_page.draft, 0, sizeof(s_activation_page.draft));
    product_ui_control_center_release_pull(&s_activation_page.holds_pull);
}

/**
 * @brief 把当前激活码草稿及长度投影到编辑标签。
 * @note 调用方保证编辑标签仍存在；此处不校验或提交激活码。
 */
static void activation_draft_refresh(void)
{
    char text[48];
    lv_snprintf(text, sizeof(text), "%s  %u/16", s_activation_page.draft,
                (unsigned)strlen(s_activation_page.draft));
    lv_label_set_text(s_activation_page.entry, text);
}

/**
 * @brief 创建有界激活码键盘及取消操作，并持有控制中心下拉占用。
 * @return 完整创建返回 0；创建失败返回 -1，已建立编辑层经关闭路径清理。
 */
static int activation_open_editor(void)
{
    lv_obj_t *cancel;
    s_activation_page.layer = lv_obj_create(s_activation_page.view.root);
    if (!s_activation_page.layer) return -1;
    lv_obj_remove_style_all(s_activation_page.layer);
    lv_obj_set_size(s_activation_page.layer, 320, 240);
    lv_obj_set_pos(s_activation_page.layer, 0, 0);
    lv_obj_set_style_bg_opa(s_activation_page.layer, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_activation_page.layer, product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    lv_obj_remove_flag(s_activation_page.layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    cancel = product_ui_time_button(s_activation_page.layer, "取消输入", activation_input, (void *)ACT_UI_CANCEL);
    s_activation_page.entry = lv_label_create(s_activation_page.layer);
    s_activation_page.keys = lv_buttonmatrix_create(s_activation_page.layer);
    if (!cancel || !s_activation_page.entry || !s_activation_page.keys) {
        activation_close_editor(); return -1;
    }
    product_ui_dialog_button_style(cancel, PRODUCT_UI_DIALOG_SECONDARY);
    lv_obj_set_size(cancel, 320, 36); lv_obj_set_pos(cancel, 0, 0);
    lv_obj_set_style_text_font(s_activation_page.entry, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_activation_page.entry, 8, 40);
    lv_obj_set_style_text_color(s_activation_page.entry, product_ui_color(PRODUCT_UI_COLOR_TEXT), 0);
    lv_obj_set_size(s_activation_page.keys, 320, 180); lv_obj_set_pos(s_activation_page.keys, 0, 60);
    lv_obj_set_style_pad_all(s_activation_page.keys, 0, 0);
    lv_obj_set_style_border_width(s_activation_page.keys, 0, 0);
    lv_obj_set_style_pad_row(s_activation_page.keys, 0, 0);
    lv_obj_set_style_pad_column(s_activation_page.keys, 0, 0);
    lv_obj_set_style_text_font(s_activation_page.keys, d_ui_font_get_default(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_activation_page.keys, product_ui_color(PRODUCT_UI_COLOR_PAGE), 0);
    lv_obj_set_style_bg_color(s_activation_page.keys, product_ui_color(PRODUCT_UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_radius(s_activation_page.keys, PRODUCT_UI_RADIUS_MD, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_activation_page.keys, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_activation_page.keys, product_ui_color(PRODUCT_UI_COLOR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_activation_page.keys, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_activation_page.keys, product_ui_color(PRODUCT_UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_activation_page.keys, product_ui_color(PRODUCT_UI_COLOR_ACTION_PRESSED), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_color_filter_opa(s_activation_page.keys, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_activation_page.keys, product_ui_color(PRODUCT_UI_COLOR_ACTION_TEXT), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_buttonmatrix_set_map(s_activation_page.keys, s_key_map);
    lv_obj_add_event_cb(s_activation_page.keys, activation_input, LV_EVENT_ALL, (void *)ACT_UI_KEY);
    s_activation_page.generation++;
    s_activation_page.armed = 0U;
    product_ui_control_center_hold_pull(&s_activation_page.holds_pull);
    activation_draft_refresh();
    return 0;
}

/**
 * @brief 只在激活代次变化时刷新状态、来源说明及编辑／重试使能。
 * @note 服务变化推进输入代次，使此前页面手势失效；READY 与保存中均按真实快照分别展示。
 */
static void activation_refresh(void)
{
    qwen_activation_snapshot_t snapshot;
    const char *state;
    qwen_activation_get_snapshot(&snapshot);
    if (snapshot.revision == s_activation_page.snapshot.revision) return;
    s_activation_page.snapshot = snapshot;
    s_activation_page.armed = 0U;
    s_activation_page.generation++;
    state = snapshot.save_pending && snapshot.busy ? "正在保存" :
        snapshot.save_pending && snapshot.state == QWEN_ACT_QUEUED ? "等待保存" :
        snapshot.source_pending ? "正在读取激活码" :
        snapshot.state == QWEN_ACT_READY ? "已激活" :
        snapshot.state == QWEN_ACT_REQUESTING ? "正在激活" :
        snapshot.state == QWEN_ACT_QUEUED ? "等待联网校时后激活" :
        snapshot.state == QWEN_ACT_FAILED ? "激活未完成" : "尚未激活";
    lv_label_set_text(s_activation_page.status, state);
    lv_label_set_text(lv_obj_get_child(s_activation_page.retry, 0),
                      snapshot.save_pending ? "重试保存" :
                      snapshot.source == QWEN_CODE_PROGRAMMER &&
                      (snapshot.error == QWEN_ACT_ERROR_SOURCE_EMPTY ||
                       snapshot.error == QWEN_ACT_ERROR_SOURCE_READ ||
                       snapshot.error == QWEN_ACT_ERROR_SOURCE_CRC ||
                       snapshot.error == QWEN_ACT_ERROR_STORAGE_READ) ? "重新读取" : "重试激活");
    lv_label_set_text(s_activation_page.detail, snapshot.error ? activation_error_text(snapshot.error) :
        snapshot.state == QWEN_ACT_READY ? "激活信息已保存" :
        snapshot.source == QWEN_CODE_PROGRAMMER ? "使用烧录工具写入的激活码" :
        snapshot.source == QWEN_CODE_FACTORY ? "使用出厂写入的激活码" :
        snapshot.source == QWEN_CODE_STATIC ? "使用本台样机的固定激活码" : "输入本台设备的16位激活码");
    if (snapshot.busy || snapshot.state == QWEN_ACT_QUEUED) {
        lv_obj_add_state(s_activation_page.retry, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_activation_page.retry, LV_STATE_DISABLED);
    }
    if (s_activation_page.edit) {
        if (snapshot.busy) lv_obj_add_state(s_activation_page.edit, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_activation_page.edit, LV_STATE_DISABLED);
    }
}

/**
 * @brief 创建激活状态页及允许来源对应的编辑入口。
 * @param[in,out] parent 页面管理器拥有的父对象。
 * @return 完整创建返回 0，任一创建失败返回 -1，部分对象仍属页面树。
 */
static int activation_page_create(lv_obj_t *parent)
{
    lv_obj_t *content;
    memset(&s_activation_page, 0, sizeof(s_activation_page));
    s_activation_page.snapshot.revision = UINT32_MAX;
    content = product_ui_settings_detail_frame_create(parent, "AI激活", &s_activation_page.view);
    if (!content) return -1;
    s_activation_page.status = product_ui_time_label(content, "", PRODUCT_UI_STYLE_BODY);
    s_activation_page.detail = product_ui_time_label(content, "", PRODUCT_UI_STYLE_CAPTION);
    s_activation_page.retry = product_ui_time_button_create(content, &(product_ui_time_button_spec_t){
        .text = "重试激活", .role = PRODUCT_UI_BUTTON_PRIMARY,
        .callback = activation_input, .context = (void *)ACT_UI_RETRY});
#if D_QWEN_ACTIVATION_SOURCE == D_QWEN_ACTIVATION_SOURCE_UI
    s_activation_page.edit = product_ui_time_button_create(content, &(product_ui_time_button_spec_t){
        .text = "输入激活码", .role = PRODUCT_UI_BUTTON_SECONDARY,
        .callback = activation_input, .context = (void *)ACT_UI_EDIT});
    if (!s_activation_page.edit) return -1;
    lv_obj_set_size(s_activation_page.edit, 280, 40);
#endif
    if (!s_activation_page.status || !s_activation_page.detail || !s_activation_page.retry) return -1;
    lv_obj_set_size(s_activation_page.retry, 280, 40);
    lv_obj_set_width(s_activation_page.detail, 280);
    lv_label_set_long_mode(s_activation_page.detail, LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(content, activation_input, LV_EVENT_SCROLL_BEGIN, NULL);
    activation_refresh();
    return 0;
}

/**
 * @brief 推进公共 UI，复核动作代次后编辑草稿或向服务提交激活／重试意图。
 * @param[in] elapsed 本轮页面毫秒数。
 * @note 受理成功才关闭输入层；真实网络及保存完成来自后续快照。
 */
static void activation_page_update(uint32_t elapsed)
{
    uint16_t action = s_activation_page.pending;
    uint32_t generation = s_activation_page.pending_generation;
    size_t length;
    d_err_t result = D_OK;
    s_activation_page.pending = ACT_UI_NONE;
    product_ui_process(elapsed);
    if (!s_activation_page.view.root) return;
    activation_refresh();
    if (generation != s_activation_page.generation) action = ACT_UI_NONE;
    if (action == ACT_UI_EDIT && !s_activation_page.layer) {
        if (activation_open_editor() != 0) product_ui_toast_show("输入界面打开失败", 1500);
    } else if (action == ACT_UI_CANCEL) activation_close_editor();
    else if (action == ACT_UI_RETRY) result = qwen_service_activation_request(NULL);
    else if (action >= ACT_UI_KEY && s_activation_page.layer) {
        action -= ACT_UI_KEY;
        length = strlen(s_activation_page.draft);
        if (action < 32U && length < 16U) s_activation_page.draft[length] = s_characters[action];
        else if (action == 32U && length) s_activation_page.draft[length - 1U] = '\0';
        else if (action == 33U) memset(s_activation_page.draft, 0, sizeof(s_activation_page.draft));
        else if (action == 34U) {
            result = qwen_service_activation_request(s_activation_page.draft);
            if (result == D_OK) activation_close_editor();
        }
        if (s_activation_page.entry) activation_draft_refresh();
    }
    if (result != D_OK) product_ui_toast_show(result == D_ERR_BUSY ? "请先结束对话或稍后重试" :
        result == D_ERR_INVALID_ARG ? "请输入完整的16位激活码" : "暂时无法激活，请检查状态", 1600);
    activation_refresh();
}

/**
 * @brief 关闭编辑层、清零草稿及手势，再解除页面框架状态。
 */
static void activation_page_destroy(void)
{
    activation_close_editor();
    product_ui_page_view_reset(&s_activation_page.view);
    memset(&s_activation_page, 0, sizeof(s_activation_page));
}
static const page_ops_t s_activation_ops = {.create = activation_page_create,
    .destroy = activation_page_destroy, .update = activation_page_update, .flags = PAGE_FLAG_NO_GESTURE};
static page_desc_t s_activation_desc = {.name = "AI激活", .ops = &s_activation_ops};
/**
 * @brief 注册静态 AI 激活页面描述。
 * @param[in] type 装配分配的页面类型。
 * @return 原样返回页面管理器注册结果。
 */
static int activation_register(int type)
{ s_activation_desc.page_type = type; return page_manager_register(&s_activation_desc); }
D_UI_MODULE_DEFINE(product_settings_activation, "AI激活", LV_SYMBOL_SETTINGS, activation_register, NULL);
#endif
