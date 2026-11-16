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
 * @brief 在父对象或当前屏幕下创建标题、有限消息区及麦克风操作栏。
 * @param[in,out] parent 聊天容器父对象；NULL 使用当前活动屏幕。
 * @note 仅 LVGL 线程使用静态单实例引用；不提供销毁重置或重复创建幂等保证，父页面删除后不得继续调用旧引用。
 */
void chat_ui_create(lv_obj_t *parent);

/**
 * @brief 复制消息文字到新气泡，限制历史数量并滚动到最新消息。
 * @param[in] is_user 非零为右侧用户气泡，零为左侧 AI 气泡。
 * @param[in] text NUL 结尾的借用文字；空指针或空串忽略。
 * @note 须先创建；只存 UI 副本，不拥有业务消息或网络请求。
 */
void chat_ui_add_msg(int is_user, const char *text);

/**
 * @brief 创建或删除当前单个输入指示气泡。
 * @param[in] show 非零显示、零隐藏；须先建立消息区域。
 */
void chat_ui_show_typing(int show);

/**
 * @brief 借用当前静态聊天实例的麦克风按钮。
 * @return 按钮对象指针，初始未创建时为 NULL；随父页面删除失效。
 */
lv_obj_t *chat_ui_get_mic_btn(void);

#endif /* D_LVGL_ENABLE */
#endif /* __D_CHAT_UI_H__ */
