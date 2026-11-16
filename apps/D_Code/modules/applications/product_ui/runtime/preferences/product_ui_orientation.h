/** @file product_ui_orientation.h @brief UI 执行器拥有的显示方向与外观存储协调接口。 */
#ifndef PRODUCT_UI_ORIENTATION_H
#define PRODUCT_UI_ORIENTATION_H
/**
 * @brief 在 UI 初始化阶段应用已加载的方向偏好并注册翻转命令。
 * @note 显示端不可用或初始方向应用失败时不注册；外观服务的初始化由启动链负责。
 */
void product_ui_orientation_init(void);
/**
 * @brief 在 UI 执行器周期中推进翻转、关联完成编号并发布方向能力。
 * @note 显示成功后才允许保存；保存超时或失败不回滚已应用的方向，不持有页面对象。
 */
void product_ui_orientation_process(void);
#endif
