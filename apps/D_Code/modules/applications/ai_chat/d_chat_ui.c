/**
 * @file d_chat_ui.c
 * @brief WeChat风格聊天UI界面 - 实现
 * @note  屏幕 320x480, LVGL v9, 英文字体
 *
 * 布局:
 * ┌──────────────────────┐
 * │    AI Assistant       │  标题栏 40px
 * ├──────────────────────┤
 * │ [AI bubble]          │
 * │          [User bubble]│  消息滚动区 390px
 * │ [AI bubble]          │
 * ├──────────────────────┤
 * │  [Hold to Talk]      │  底部栏 50px
 * └──────────────────────┘
 */

#include "config/app_config.h"

#if (D_FEATURE_AI_CHAT) && defined(D_LVGL_ENABLE)

#include "lvgl.h"
#include "d_chat_ui.h"
#include "framework/d_ui_font.h"
#include "framework/compat/d_stdio_compat.h"
#include <string.h>

#define CHAT_TAG "[CHAT_UI] "

//*********************************************************************************//
//                              布局常量                                           //
//*********************************************************************************//

#define SCR_W           320
#define SCR_H           480
#define FACE_AREA_H     200     /* 表情引擎占用的上部区域高度 */
#define CHAT_H          (SCR_H - FACE_AREA_H)  /* 聊天区总高度: 280px */
#define TITLE_BAR_H     30
#define BOTTOM_BAR_H    46
#define MSG_AREA_H      (CHAT_H - TITLE_BAR_H - BOTTOM_BAR_H)

/* 气泡样式 */
#define BUBBLE_MAX_W    220
#define BUBBLE_PAD      10
#define BUBBLE_RADIUS   12
#define MSG_SPACING     8
#define SIDE_MARGIN     10

/* 消息数量上限（节省内存） */
#define MAX_MESSAGES    20


//*********************************************************************************//
//                              颜色定义（WeChat风格）                             //
//*********************************************************************************//

#define C_TITLE_BG      lv_color_hex(0x2E2E2E)
#define C_TITLE_TEXT    lv_color_hex(0xFFFFFF)
#define C_MSG_BG        lv_color_hex(0xEDEDED)
#define C_BUBBLE_AI     lv_color_hex(0xFFFFFF)
#define C_BUBBLE_USER   lv_color_hex(0x95EC69)
#define C_TEXT_DARK     lv_color_hex(0x1C1C1C)
#define C_TEXT_GRAY     lv_color_hex(0x999999)
#define C_BOTTOM_BG     lv_color_hex(0xF7F7F7)
#define C_BORDER        lv_color_hex(0xD9D9D9)
#define C_BTN_BG        lv_color_hex(0xFCFCFC)
#define C_BTN_PRESSED   lv_color_hex(0xC9C9C9)

//*********************************************************************************//
//                              UI对象                                             //
//*********************************************************************************//

static lv_obj_t *s_msg_area   = NULL;
static lv_obj_t *s_mic_btn    = NULL;
static lv_obj_t *s_typing_row = NULL;
static int       s_msg_count  = 0;

static lv_obj_t *s_bottom_bar   = NULL;

//*********************************************************************************//
//                              标题栏                                             //
//*********************************************************************************//

static void create_title_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, TITLE_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, C_TITLE_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "AI Assistant");
    lv_obj_set_style_text_color(title, C_TITLE_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_center(title);
}

//*********************************************************************************//
//                              消息滚动区                                         //
//*********************************************************************************//

static void create_msg_area(lv_obj_t *parent)
{
    s_msg_area = lv_obj_create(parent);
    lv_obj_set_size(s_msg_area, SCR_W, MSG_AREA_H);
    lv_obj_set_pos(s_msg_area, 0, TITLE_BAR_H);

    /* 背景 */
    lv_obj_set_style_bg_color(s_msg_area, C_MSG_BG, 0);
    lv_obj_set_style_bg_opa(s_msg_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_msg_area, 0, 0);
    lv_obj_set_style_radius(s_msg_area, 0, 0);

    /* 内边距 */
    lv_obj_set_style_pad_left(s_msg_area, SIDE_MARGIN, 0);
    lv_obj_set_style_pad_right(s_msg_area, SIDE_MARGIN, 0);
    lv_obj_set_style_pad_top(s_msg_area, MSG_SPACING, 0);
    lv_obj_set_style_pad_bottom(s_msg_area, MSG_SPACING, 0);
    lv_obj_set_style_pad_row(s_msg_area, MSG_SPACING, 0);

    /* 纵向flex布局，消息从上到下排列 */
    lv_obj_set_flex_flow(s_msg_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_msg_area, LV_SCROLLBAR_MODE_OFF);
}

//*********************************************************************************//
//                              底部操作栏                                         //
//*********************************************************************************//

static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    s_bottom_bar = bar;
    lv_obj_set_size(bar, SCR_W, BOTTOM_BAR_H);
    lv_obj_set_pos(bar, 0, TITLE_BAR_H + MSG_AREA_H);

    /* 样式 */
    lv_obj_set_style_bg_color(bar, C_BOTTOM_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, C_BORDER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 麦克风按钮 */
    s_mic_btn = lv_btn_create(bar);
    lv_obj_set_size(s_mic_btn, SCR_W - 24, BOTTOM_BAR_H - 14);
    lv_obj_center(s_mic_btn);

    /* 按钮正常状态 */
    lv_obj_set_style_bg_color(s_mic_btn, C_BTN_BG, 0);
    lv_obj_set_style_radius(s_mic_btn, 6, 0);
    lv_obj_set_style_shadow_width(s_mic_btn, 0, 0);
    lv_obj_set_style_border_width(s_mic_btn, 1, 0);
    lv_obj_set_style_border_color(s_mic_btn, C_BORDER, 0);

    /* 按钮按下状态 */
    lv_obj_set_style_bg_color(s_mic_btn, C_BTN_PRESSED, LV_STATE_PRESSED);

    /* 按钮文字 */
    lv_obj_t *lbl = lv_label_create(s_mic_btn);
    lv_label_set_text(lbl, LV_SYMBOL_AUDIO " Hold to Talk");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
}

//*********************************************************************************//
//                              内存管理：限制消息数量                             //
//*********************************************************************************//

static void limit_messages(void)
{
    while (s_msg_count > MAX_MESSAGES && s_msg_area) {
        lv_obj_t *oldest = lv_obj_get_child(s_msg_area, 0);
        if (oldest && oldest != s_typing_row) {
            lv_obj_delete(oldest);
            s_msg_count--;
        } else {
            break;
        }
    }
}


//*********************************************************************************//
//                              公共接口                                           //
//*********************************************************************************//

static lv_obj_t *s_chat_container = NULL;

void chat_ui_create(lv_obj_t *parent)
{
    /* 使用传入的父容器，如果为空则使用当前活跃屏幕 */
    lv_obj_t *scr = parent ? parent : lv_scr_act();

    /* 聊天区容器：位于表情区下方 */
    s_chat_container = lv_obj_create(scr);
    lv_obj_set_size(s_chat_container, SCR_W, CHAT_H);
    lv_obj_set_pos(s_chat_container, 0, FACE_AREA_H);
    lv_obj_set_style_pad_all(s_chat_container, 0, 0);
    lv_obj_set_style_border_width(s_chat_container, 0, 0);
    lv_obj_set_style_radius(s_chat_container, 0, 0);
    lv_obj_set_style_bg_color(s_chat_container, C_MSG_BG, 0);
    lv_obj_set_style_bg_opa(s_chat_container, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_chat_container, LV_OBJ_FLAG_SCROLLABLE);

    create_title_bar(s_chat_container);
    create_msg_area(s_chat_container);
    create_bottom_bar(s_chat_container);

    printf(CHAT_TAG "Chat UI created (%dx%d, offset Y=%d)\n", SCR_W, CHAT_H, FACE_AREA_H);
}

void chat_ui_add_msg(int is_user, const char *text)
{
    if (!s_msg_area || !text || !text[0]) {
        return;
    }

    /* 行容器：占满消息区宽度，背景透明 */
    lv_obj_t *row = lv_obj_create(s_msg_area);
    lv_obj_set_width(row, SCR_W - 2 * SIDE_MARGIN);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* 气泡容器 */
    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, BUBBLE_MAX_W, 0);
    lv_obj_set_style_radius(bubble, BUBBLE_RADIUS, 0);
    lv_obj_set_style_pad_all(bubble, BUBBLE_PAD, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_shadow_width(bubble, 0, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    /* 气泡内的文字标签 */
    lv_obj_t *label = lv_label_create(bubble);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, C_TEXT_DARK, 0);
    lv_obj_set_style_text_font(label, d_ui_font_get_default(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, BUBBLE_MAX_W - 2 * BUBBLE_PAD);

    /* 根据发送者设置颜色和对齐方式 */
    if (is_user) {
        /* 用户消息：绿色气泡，右对齐 */
        lv_obj_set_style_bg_color(bubble, C_BUBBLE_USER, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, 0, 0);
    } else {
        /* AI消息：白色气泡，左对齐 */
        lv_obj_set_style_bg_color(bubble, C_BUBBLE_AI, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);
    }

    s_msg_count++;
    limit_messages();

    /* 自动滚动到最新消息 */
    lv_obj_scroll_to_view(row, LV_ANIM_ON);
}

void chat_ui_show_typing(int show)
{
    if (show && !s_typing_row) {
        /* 创建 "正在输入" 指示气泡 */
        lv_obj_t *row = lv_obj_create(s_msg_area);
        lv_obj_set_width(row, SCR_W - 2 * SIDE_MARGIN);
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *bubble = lv_obj_create(row);
        lv_obj_set_width(bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(bubble, BUBBLE_RADIUS, 0);
        lv_obj_set_style_pad_all(bubble, BUBBLE_PAD, 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        lv_obj_set_style_shadow_width(bubble, 0, 0);
        lv_obj_set_style_bg_color(bubble, C_BUBBLE_AI, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *label = lv_label_create(bubble);
        lv_label_set_text(label, ". . .");
        lv_obj_set_style_text_color(label, C_TEXT_GRAY, 0);
        lv_obj_set_style_text_font(label, d_ui_font_get_default(), 0);

        s_typing_row = row;
        lv_obj_scroll_to_view(row, LV_ANIM_ON);
    } else if (!show && s_typing_row) {
        lv_obj_delete(s_typing_row);
        s_typing_row = NULL;
    }
}

lv_obj_t *chat_ui_get_mic_btn(void)
{
    return s_mic_btn;
}


#endif /* D_FEATURE_AI_CHAT && D_LVGL_ENABLE */
