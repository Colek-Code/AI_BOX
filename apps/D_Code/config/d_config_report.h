/**
 * @file d_config_report.h
 * @brief 声明面向开发者的运行配置摘要输出接口，属于配置层。
 */

#ifndef D_CONFIG_REPORT_H
#define D_CONFIG_REPORT_H

/**
 * @brief 输出启动配置摘要，不修改任何功能开关。
 * @note 由启动线程调用；仅输出板型、界面、功能和鉴权模式，不输出凭据。
 */
void d_config_report_print(void);

#endif /* D_CONFIG_REPORT_H */
