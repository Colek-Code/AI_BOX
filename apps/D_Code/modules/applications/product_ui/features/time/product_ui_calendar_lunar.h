/** @file product_ui_calendar_lunar.h @brief 固定压缩历表转换的内部接口，无时钟或平台依赖。 */
#ifndef __PRODUCT_UI_CALENDAR_LUNAR_H__
#define __PRODUCT_UI_CALENDAR_LUNAR_H__

#include "modules/applications/product_ui/features/time/product_ui_calendar_model.h"

/**
 * @brief 按固定压缩历表把受支持公历日转换为农历日。
 * @param[in] solar 完整且位于产品查询区间的公历日期。
 * @param[out] out 成功时接收农历值，失败不修改。
 * @return 成功为 CALENDAR_OK，空输出／非法日期为 ARGUMENT，公历超范围为 RANGE，表索引或编码不一致为 DATA。
 */
product_ui_calendar_result_e product_ui_calendar_to_lunar(
    const product_ui_date_t *solar, product_ui_calendar_lunar_t *out);
/**
 * @brief 按固定压缩历表把农历日转换为受支持公历日。
 * @param[in] lunar 农历年、月、日及是否闰月；允许表中额外起始农历年。
 * @param[out] out 成功时接收公历日期，失败不修改。
 * @return 成功为 CALENDAR_OK；字段或闰月不匹配为 ARGUMENT；农历或转换后公历超区间为 RANGE。
 */
product_ui_calendar_result_e product_ui_calendar_to_solar(
    const product_ui_calendar_lunar_t *lunar, product_ui_date_t *out);
/**
 * @brief 读取给定公历日的固定节气编号。
 * @param[in] solar 借用公历日期。
 * @return 当天节气编号，无节气或日期不可查询返回 TERM_NONE。
 */
uint8_t product_ui_calendar_term_for_date(const product_ui_date_t *solar);
/**
 * @brief 用次日是否为历表春节判断当天是否除夕。
 * @param[in] solar 借用公历日期。
 * @return 是除夕返回非零，其他或不可查询日期返回零。
 */
uint8_t product_ui_calendar_is_year_end(const product_ui_date_t *solar);

#endif
