/** @file cipher_panel_aichat.h @brief Cipher Panel 的可滚动句子字幕，原文及分句由共用 Qwen 字幕层提供。 */
#ifndef __CIPHER_PANEL_AICHAT_H__
#define __CIPHER_PANEL_AICHAT_H__

#include "lvgl.h"

/**
 * @brief 尝试初始化共用字幕订阅，当前兼容 void 接口不返回失败。
 * @note 缓存与订阅超出页面寿命，不在页面隐藏时取消。
 */
void cipher_panel_aichat_init(void);
/**
 * @brief 清空本页引用并创建字幕卡、状态及滚动区域，强制首次准备。
 * @param[in,out] parent 调用方已准备并拥有的页面父对象。
 * @note 仅 LVGL 线程；调用方负责旧树删除，当前 void 接口不汇总局部创建失败，也不提供每行标签分配失败的完整重试合同。
 */
void cipher_panel_aichat_show(lv_obj_t *parent);
/**
 * @brief 字幕层有新内容时更新行、自动跟随及状态。
 * @note 只在字幕页存活且由 LVGL 周期调用，不在事件回调操作控件。
 */
void cipher_panel_aichat_update(void);

#endif /* __CIPHER_PANEL_AICHAT_H__ */
