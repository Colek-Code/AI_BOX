/** @file launcher_demo.h @brief 全屏横向启动器演示页注册入口。 */

#ifndef LAUNCHER_DEMO_H
#define LAUNCHER_DEMO_H

/**
 * @brief 将全屏横向启动器演示页描述注册到页面管理器。
 * @return 页面管理器的注册结果，0 成功，负值失败。
 * @note 仅注册描述，不创建页面对象；实际页面由页面管理器按需创建。
 */
int launcher_demo_register(void);

#endif /* LAUNCHER_DEMO_H */
