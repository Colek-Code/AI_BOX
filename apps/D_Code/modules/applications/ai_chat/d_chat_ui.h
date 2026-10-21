/**
 * @file d_chat_ui.h
 * @brief WeChat风格聊天UI界面 - 公共接口
 * @note  依赖 LVGL v9，屏幕分辨率 320x480
 */

#ifndef __D_CHAT_UI_H__
#define __D_CHAT_UI_H__

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "lvgl.h"

/**
 * @brief 创建聊天UI界面（标题栏 + 消息区 + 底部栏）
 * @param parent  父容器对象，如果为NULL则使用当前活跃屏幕
 */
void chat_ui_create(lv_obj_t *parent);

/**
 * @brief 添加一条消息气泡
 * @param is_user  1=用户消息(右侧绿色), 0=AI消息(左侧白色)
 * @param text     消息文本内容
 */
void chat_ui_add_msg(int is_user, const char *text);

/**
 * @brief 显示/隐藏AI正在输入指示器
 * @param show  1=显示, 0=隐藏
 */
void chat_ui_show_typing(int show);

/**
 * @brief 获取麦克风按钮对象（用于外部绑定事件）
 * @return 麦克风按钮的 lv_obj_t 指针
 */
lv_obj_t *chat_ui_get_mic_btn(void);

#endif /* D_LVGL_ENABLE */
#endif /* __D_CHAT_UI_H__ */
