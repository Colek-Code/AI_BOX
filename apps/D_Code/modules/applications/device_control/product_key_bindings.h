/**
 * @file product_key_bindings.h
 * @brief 声明独立于页面生命周期的产品按键装配入口，业务资源仍由服务管理。
 */
#ifndef PRODUCT_KEY_BINDINGS_H
#define PRODUCT_KEY_BINDINGS_H
#include "framework/d_error.h"

/**
 * @brief 在按键服务就绪后、SDK 应用事件派发前装配产品按键。
 * @return D_OK 为本次需要的绑定均已登记；否则返回注册错误，并只尝试回滚本次新取得的绑定。
 * @note 仅在启动装配调用，绑定持续到本次固件运行结束，不随页面切换或服务状态变化重建。
 * @note 电源、Qwen 和媒体服务在动作执行时核对各自的就绪与资源准入，装配只登记手势。
 */
d_err_t product_key_bindings_init(void);

#endif
