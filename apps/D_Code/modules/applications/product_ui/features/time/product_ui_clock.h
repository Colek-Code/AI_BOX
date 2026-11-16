/** @file product_ui_clock.h @brief 供 UI 及后台服务使用的软件墙钟，手动设置不改 SDK／RTC。 */
#ifndef PRODUCT_UI_CLOCK_H
#define PRODUCT_UI_CLOCK_H
#include "product_ui_clock_model.h"
#include "framework/d_error.h"
/**
 * @brief 一次性初始化未可信模型并建立独立周期采样。
 * @return 首次定时器建立成功返回 D_OK，失败返回 D_ERR_IO；重复调用返回 D_OK 或 D_ERR_NOT_READY。
 * @note 可选初始化失败不要求桌面退出；首次失败不自动重试。
 */
d_err_t product_ui_clock_init(void);
/**
 * @brief 在自旋锁内推进经过时间并复制墙钟快照。
 * @param[out] snapshot 接收值副本，NULL 时忽略；未就绪时 valid 清零。
 */
void product_ui_clock_get_snapshot(product_ui_clock_snapshot_t *snapshot);
/**
 * @brief 按预期代次设置经校验的手动北京时间。
 * @param[in] value 本次借用的完整北京时间，年份须在产品支持范围内。
 * @param[in] expected_revision 调用方编辑开始时观察到的墙钟代次。
 * @return 成功返回 D_OK；未就绪或静默返回 NOT_READY；代次不匹配返回 BUSY；日期无效返回 INVALID_ARG。
 * @note 自旋锁内先推进经过时间再校正；手动设置推进调度代次以使旧闹钟实例失效。
 */
d_err_t product_ui_clock_set_manual(const product_ui_datetime_t *value, uint32_t expected_revision);
/**
 * @brief 按预期代次切回自动来源，并请求一次立即网络采样。
 * @param[in] expected_revision 调用方观察的墙钟代次。
 * @return 成功受理返回 D_OK；未就绪或静默返回 NOT_READY；代次冲突返回 BUSY。
 * @note D_OK 不保证已获得可信网络样本；实际有效性及来源通过快照查询。
 */
d_err_t product_ui_clock_use_automatic(uint32_t expected_revision);
/**
 * @brief 撤销就绪状态并停止独立定时器。
 * @note 静态模型继续存在；不重置一次性初始化尝试，不提供重启语义。
 */
void product_ui_clock_stop(void);
/**
 * @brief 冻结网络及手动校正并推进采样代次，经过时间仍继续累计。
 */
void product_ui_clock_quiesce_begin(void);
/**
 * @brief 解除校正静默，推进采样代次并要求下一次立即采样。
 */
void product_ui_clock_quiesce_cancel(void);
/**
 * @brief 读取有效软件 UTC 并转换为北京时间，不使用 RTC 回退。
 * @param[out] value 成功时接收经转换的北京时间；失败不得使用。
 * @return 成功返回 NONE；空输出或转换失败返回 SOURCE_INVALID；时钟无效返回 NOT_READY。
 */
product_ui_time_unavailable_reason_e product_ui_clock_read(product_ui_datetime_t *value);
#endif
