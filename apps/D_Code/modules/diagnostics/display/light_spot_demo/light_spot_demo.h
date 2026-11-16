/** @file light_spot_demo.h @brief 双光斑动态演示页注册入口。 */

#ifndef LIGHT_SPOT_DEMO_H
#define LIGHT_SPOT_DEMO_H

/**
 * @brief 将双光斑动态演示页描述注册到页面管理器。
 * @return 页面管理器的注册结果，0 成功，负值失败。
 * @note 仅注册描述，不创建页面对象；实际页面由页面管理器按需创建。
 */
int light_spot_demo_register(void);

#endif /* LIGHT_SPOT_DEMO_H */
