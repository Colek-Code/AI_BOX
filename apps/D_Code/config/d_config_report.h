/**
 * @file d_config_report.h
 * @brief 声明面向开发者的运行配置摘要输出接口，属于配置层。
 */

#ifndef D_CONFIG_REPORT_H
#define D_CONFIG_REPORT_H

/**
 * @brief 取得已通过配置校验的根界面名称。
 * @return 静态只读名称；不分配内存，不转移所有权。
 * @note 启动报告与界面装配共用此映射，不依赖应用层或运行状态。
 */
const char *d_config_ui_name(void);

/**
 * @brief 输出启动配置摘要，不修改任何功能开关。
 * @note 由启动线程调用；仅输出板型、界面、功能和鉴权模式，不输出凭据。
 */
void d_config_report_print(void);

#endif /* D_CONFIG_REPORT_H */
