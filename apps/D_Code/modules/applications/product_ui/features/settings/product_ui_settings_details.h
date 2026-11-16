/** @file product_ui_settings_details.h @brief 隐藏设置详情页的创建接口及页面所有权合同。 */

#ifndef __PRODUCT_UI_SETTINGS_DETAILS_H__
#define __PRODUCT_UI_SETTINGS_DETAILS_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "lvgl.h"

/**
 * @brief 创建 WiFi 状态、扫描列表及屏幕配网页；功能关闭时呈现不可用反馈。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 完整创建返回 0，框架或内容创建失败返回 -1；部分对象交页面管理器清理。
 * @note 网络工作经连接域请求受理；页面销毁不表示网络请求取消或完成。
 */
int product_ui_settings_wifi_page_create(lv_obj_t *parent);
/**
 * @brief 创建真实蓝牙能力及详情缺口的占位反馈页。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 完整创建返回 0，框架或反馈失败返回 -1。
 * @note 不在本页执行蓝牙配对或伪造连接状态。
 */
int product_ui_settings_bluetooth_page_create(lv_obj_t *parent);
/**
 * @brief 创建按实际服务能力投影的显示、亮度及动态效果设置页。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 完整页面或不可用反馈成功返回 0，框架或内容创建失败返回 -1。
 * @note 部分对象由页面管理器清理；控件事件只提交所有者接口，不拥有显示硬件或外观保存任务。
 */
int product_ui_settings_display_page_create(lv_obj_t *parent);
/**
 * @brief 创建音量滑条及请求／完成状态投影页。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 完整页面或不可用反馈成功返回 0，框架或内容创建失败返回 -1。
 * @note 销毁页面不停止设备音量后台事务。
 */
int product_ui_settings_sound_page_create(lv_obj_t *parent);
/**
 * @brief 创建壁纸选择与确认保存状态页。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 成功返回 0，目录数量或任一对象创建失败返回 -1，部分页面对象交管理器清理。
 * @note 销毁页面不取消外观服务已经受理的保存请求。
 */
int product_ui_settings_wallpaper_page_create(lv_obj_t *parent);
/**
 * @brief 创建本次开机的软件时间编辑页，以确认时的时钟代次提交变更。
 * @param[in,out] parent 页面管理器提供的父对象。
 * @return 完整创建返回 0；父对象为空或创建失败返回 -1。
 * @note 手动时间只作用于本次启动，不写 SDK／RTC；失败路径清理编辑根，框架对象交页面管理器删除。
 */
int product_ui_settings_time_page_create(lv_obj_t *parent);

#endif
#endif /* __PRODUCT_UI_SETTINGS_DETAILS_H__ */
