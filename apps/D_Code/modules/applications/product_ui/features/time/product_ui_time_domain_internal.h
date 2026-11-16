/** @file product_ui_time_domain_internal.h @brief 时间域内部使用的公历校验、比较及固定北京时间转换。 */

#ifndef __PRODUCT_UI_TIME_DOMAIN_INTERNAL_H__
#define __PRODUCT_UI_TIME_DOMAIN_INTERNAL_H__

#include "modules/applications/product_ui/features/time/product_ui_time_domain.h"

/**
 * @brief 按日期到秒的顺序比较两个已验证值。
 * @param[in] left 调用方保证非空且合法的左值。
 * @param[in] right 调用方保证非空且合法的右值。
 * @return left 早返回 -1，相同返回 0，晚返回 1。
 */
int product_ui_time_compare_datetime(const product_ui_datetime_t *left,
                                     const product_ui_datetime_t *right);
/**
 * @brief 将合法公历日期原地推进一天，处理月末及闰年。
 * @param[in,out] date 待推进日期，失败保持原日期。
 * @return 成功返回 TIME_OK；日期无效或已经到最大年份末日返回 INVALID_ARGUMENT。
 */
product_ui_time_result_e product_ui_time_add_one_day(product_ui_date_t *date);
/**
 * @brief 计算已验证公历日期的星期编号。
 * @param[in] date 调用方保证非空且合法的日期。
 * @return 0 表示周日，1 至 6 表示周一至周六。
 */
uint8_t product_ui_time_weekday(const product_ui_date_t *date);
/**
 * @brief 按公历闰年规则取得月份天数。
 * @param[in] year 用于闰年判断的年份。
 * @param[in] month 月份。
 * @return 合法月份为 28 至 31 天，月份不在 1 至 12 时为零。
 */
uint8_t product_ui_time_days_in_month(uint16_t year, uint8_t month);
/**
 * @brief 把非负 UTC epoch 秒转换为固定 UTC+8 公历日期时间。
 * @param[in] utc_seconds 从零至 253402271999 的 UTC 秒。
 * @param[out] out 成功时接收北京时间；失败不修改。
 * @return 成功返回 TIME_OK；空输出或超出可表示范围返回 INVALID_ARGUMENT。
 * @note 加偏移前先做上界检查，不依赖 time_t 或进程时区。
 */
product_ui_time_result_e product_ui_time_utc_to_beijing(
    int64_t utc_seconds, product_ui_datetime_t *out);
/**
 * @brief 把合法北京时间转换为非负 UTC epoch 秒。
 * @param[in] value 借用的完整北京时间。
 * @param[out] utc_seconds 成功时接收 UTC 秒；失败不修改。
 * @return 成功返回 TIME_OK；日期无效、空输出、早于 UTC epoch 或年份早于 1970 返回 INVALID_ARGUMENT。
 * @note 固定减去 UTC+8 偏移，不依赖进程时区。
 */
product_ui_time_result_e product_ui_time_beijing_to_utc(
    const product_ui_datetime_t *value, int64_t *utc_seconds);

#endif /* __PRODUCT_UI_TIME_DOMAIN_INTERNAL_H__ */
