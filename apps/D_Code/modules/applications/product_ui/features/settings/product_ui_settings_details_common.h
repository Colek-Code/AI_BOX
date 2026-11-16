/** @file product_ui_settings_details_common.h @brief 隐藏设置详情页的共用框架、值行及固定格式辅助。 */

#ifndef __PRODUCT_UI_SETTINGS_DETAILS_COMMON_H__
#define __PRODUCT_UI_SETTINGS_DETAILS_COMMON_H__

#include "config/app_config.h"

#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)

#include "modules/applications/product_ui/shared/widgets/product_ui_components.h"
#include "modules/applications/product_ui/features/settings/product_ui_settings_service.h"

typedef struct {
    const char *title;
    const char *value;
    product_ui_icon_id_t icon_id;
    product_ui_color_e accent;
    lv_obj_t **row_out;
} product_ui_settings_value_row_t;

/**
 * @brief 创建带设置返回导航的可滚动详情框架。
 * @param[in,out] parent 页面管理器拥有的父对象。
 * @param[in] title 本次创建使用的借用标题。
 * @param[out] view 接收框架对象引用，须维持至页面清理。
 * @return 成功返回 view 内的内容对象，参数或创建失败返回 NULL。
 * @note 部分创建对象仍由页面树持有并由页面管理器删除。
 */
lv_obj_t *product_ui_settings_detail_frame_create(lv_obj_t *parent,
                                                  const char *title,
                                                  product_ui_page_view_t *view);
/**
 * @brief 在详情内容区创建加载、空或错误反馈。
 * @param[in,out] content 父内容容器。
 * @param[in] state 反馈类型。
 * @param[in] title 借用标题。
 * @param[in] message 借用说明。
 * @return 成功返回 0，参数为空或创建失败返回 -1。
 */
int product_ui_settings_detail_feedback(lv_obj_t *content,
                                        product_ui_feedback_state_e state,
                                        const char *title,
                                        const char *message);
/**
 * @brief 只有 SETTINGS 服务 READY 时才读取设置快照。
 * @param[out] snapshot 接收值副本，失败时不得使用。
 * @return 成功返回 0，空输出、服务未就绪或快照读取失败返回 -1。
 */
int product_ui_settings_detail_snapshot(
    product_ui_settings_snapshot_t *snapshot);
/**
 * @brief 同时检查服务 READY、命令回调及指定能力／有效位。
 * @param[in] snapshot 借用的设置快照。
 * @param[in] capability 待检查的单个设置能力位。
 * @return 全部条件成立返回非零，空输入或任一条件不满足为零。
 */
uint8_t product_ui_settings_detail_value_ready(
    const product_ui_settings_snapshot_t *snapshot, uint32_t capability);
/**
 * @brief 创建单行只读状态值行，并按需返回行对象。
 * @param[in,out] content 新行的父容器。
 * @param[in] row_spec 借用的值行规格，row_out 非空时写入新行或 NULL。
 * @return 成功返回 0，参数或创建失败返回 -1。
 * @note 本次标题及值交由共用行控件绑定；行对象属于父页面树。
 */
int product_ui_settings_detail_value_row(
    lv_obj_t *content, const product_ui_settings_value_row_t *row_spec);
/**
 * @brief 把八位无符号数写成无前导零十进制文字。
 * @param[in] value 待格式化的 0 至 255 数值。
 * @param[out] output 至少四字节的缓冲，NULL 时忽略。
 */
void product_ui_settings_detail_format_u8(uint8_t value, char output[4]);
/**
 * @brief 按四位年、两位月日输出 YYYY-MM-DD。
 * @param[in] year 调用方已验证的年份，输出保留低四位。
 * @param[in] month 调用方已验证的月份。
 * @param[in] day 调用方已验证的日期。
 * @param[out] output 至少十一字节，NULL 时忽略。
 * @note 本函数只格式化，不校验日期合法性。
 */
void product_ui_settings_detail_format_date(uint16_t year, uint8_t month,
                                            uint8_t day, char output[11]);
/**
 * @brief 按两位时分秒输出 HH:MM:SS。
 * @param[in] hour 调用方已验证的小时。
 * @param[in] minute 调用方已验证的分钟。
 * @param[in] second 调用方已验证的秒。
 * @param[out] output 至少九字节，NULL 时忽略。
 * @note 只格式化，不做范围校验。
 */
void product_ui_settings_detail_format_time(uint8_t hour, uint8_t minute,
                                            uint8_t second, char output[9]);
/**
 * @brief 把详情页周期传递给公共 UI 运行时。
 * @param[in] elapsed_ms 本轮页面经过的毫秒数。
 */
void product_ui_settings_detail_page_update(uint32_t elapsed_ms);

#endif
#endif /* __PRODUCT_UI_SETTINGS_DETAILS_COMMON_H__ */
